#include "region_control.h"
#include <assert.h>
#include <omp.h>
#include <stdio.h>
#include <string.h>

/* 初始化调用方持有的上下文，不分配内存、不读取时钟。 */
void region_control_init(region_control *control, region_info *regions,
                         int count, int report)
{
  assert(control && count >= 0 && count <= REGION_CONTROL_MAX_REGIONS);
  assert(count == 0 || regions);
  memset(control, 0, sizeof(*control));
  control->regions = regions;
  control->region_count = count;
  control->enabled = report;
  control->pending_nowait = -1;
  control->parallel_id = -1;
}

/* 复制可选 callback/context；NULL 注册用于注销。 */
void region_control_register(region_control *control,
                             const region_control_callbacks *callbacks,
                             void *context)
{
  control->callbacks = callbacks ? *callbacks : (region_control_callbacks){0};
  control->context = context;
}

/* 开始总计时窗口，region 历史累计值保留。 */
void iteration_start(region_control *control)
{
  control->running = 1;
  control->total_start = omp_get_wtime();
  control->active = control->enabled;
}

/* 结束总窗口并退出 step，不补发可选的 step_end callback。 */
void iteration_end(region_control *control)
{
  assert(control->parallel_id == -1);
  if (!control->running) return;
  control->total_elapsed += omp_get_wtime() - control->total_start;
  control->active = control->running = 0;
  control->in_step = 0;
}

/* 正式窗口内仅通知新 step，不读取时间。 */
void step_start(region_control *control, int step)
{
  if (!control->running) return;
  control->in_step = 1;
  if (control->callbacks.step_start)
    control->callbacks.step_start(control->context, step);
}

/* 正式窗口内仅通知 step 结束，不读取时间。 */
void step_end(region_control *control, int step)
{
  if (!control->running) return;
  assert(control->in_step);
  if (control->callbacks.step_end)
    control->callbacks.step_end(control->context, step);
  control->in_step = 0;
}

/* 返回正式窗口累计时间。 */
double iteration_time(const region_control *control)
{
  return control->total_elapsed;
}

/* 仅在输出时格式化一行名称和占比，不读取时钟。 */
static void report_row(const region_control *control, int id, const char *kind)
{
  const region_info *region = &control->regions[id];
  double seconds = control->elapsed[id];
  double percent = control->total_elapsed > 0 ? 100.0 * seconds / control->total_elapsed : 0.0;
  printf("%s region %s", kind, region->name);
  if (region->line) printf(":%d", region->line);
  if (region->nowait) printf(" (nowait)");
  printf("  %.9f s  step: %.3f%%\n", seconds, percent);
}

/* 输出内置计时的累计值；combined parallel/for 复用同一份样本。 */
void region_report(const region_control *control)
{
  if (!control->enabled) return;
  puts("\ntime report\nunit: seconds");
  printf("iteration total: %.9f s\n", control->total_elapsed);
  puts("step %: accumulated region time / iteration total (average time-step basis)");
  for (int p = 0; p < control->region_count; ++p) {
    const region_info *region = &control->regions[p];
    if (region->parent != -1 || control->elapsed[p] == 0) continue;
    report_row(control, p, "parallel");
    if (region->combined) report_row(control, p, "    for");
    for (int f = 0; f < control->region_count; ++f)
      if (control->regions[f].parent == p && control->elapsed[f] != 0)
        report_row(control, f, "    for");
  }
}

/* 首次执行只保存编译器字符串指针；后续调用无需字符串处理。 */
static void locate(region_control *control, int id,
                   const char *file, const char *name, int line)
{
  assert(id >= 0 && id < control->region_count);
  region_info *region = &control->regions[id];
  if (!region->name) {
    region->name = name;
    region->file = file;
    region->line = line;
  }
}

/* callback 完成后采样；关闭报告时仍为 parallel_end 提供单次耗时。 */
void region_parallel_start(region_control *control, int id,
                           const char *file, const char *name, int line)
{
  locate(control, id, file, name, line);
  assert(control->parallel_id == -1);
  control->parallel_id = id;
  int callbacks_active = control->running && control->in_step;
  if (callbacks_active && control->callbacks.parallel_start)
    control->callbacks.parallel_start(control->context, id);
  control->sample_active = control->active ||
                          (callbacks_active && control->callbacks.parallel_end);
  if (control->sample_active)
    control->parallel_begin = omp_get_wtime();
}

/* 先结束当前 parallel/尾部 nowait 的计时，再调用耗时 callback。 */
void region_parallel_end(region_control *control, int id)
{
  assert(control->parallel_id == id);
  if (control->sample_active) {
    double end = omp_get_wtime();
    double seconds = end - control->parallel_begin;
    if (control->active) {
      control->elapsed[id] += seconds;
      int pending = control->pending_nowait;
      /* 尾部 nowait 与外层 parallel 共用 join 后的结束时间，不额外读钟。 */
      if (pending >= 0 && control->regions[pending].parent == id) {
        control->elapsed[pending] += end - control->start[pending];
        control->pending_nowait = -1;
      }
    }
    if (control->running && control->in_step && control->callbacks.parallel_end)
      control->callbacks.parallel_end(control->context, id, seconds);
  }
  control->parallel_id = -1;
}

/* 仅记录 for 的计时；nowait 延后到下一边界结束。 */
void region_for_start(region_control *control, int id,
                      const char *file, const char *name, int line)
{
  locate(control, id, file, name, line);
  if (!control->active) return;
  double begin = omp_get_wtime();
  int pending = control->pending_nowait;
  /* 当前 for 开始的同一时间点，结算前一组 nowait。 */
  if (pending >= 0)
    control->elapsed[pending] += begin - control->start[pending];
  control->start[id] = begin;
  control->pending_nowait = control->regions[id].nowait ? id : -1;
}

/* 普通 for 立即结束；nowait 的 END 保留待结束状态。 */
void region_for_end(region_control *control, int id)
{
  if (control->active && !control->regions[id].nowait)
    control->elapsed[id] += omp_get_wtime() - control->start[id];
}

/* 利用应用已有同步边界结束 pending nowait。 */
void region_sync(region_control *control)
{
  int pending = control->pending_nowait;
  if (control->active && pending >= 0) {
    control->elapsed[pending] += omp_get_wtime() - control->start[pending];
    control->pending_nowait = -1;
  }
}
