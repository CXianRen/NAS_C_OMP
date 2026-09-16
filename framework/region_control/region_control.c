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
  for (int id = 0; id < count; ++id) {
    assert(regions[id].name && regions[id].name[0]);
    assert(regions[id].file && regions[id].file[0] && regions[id].line > 0);
  }
  memset(control, 0, sizeof(*control));
  control->regions = regions;
  control->region_count = count;
  control->enabled = report;
  control->parallel_id = -1;
}

/* 复制可选 callback/context；NULL 注册用于注销。 */
void region_control_register(region_control *control,
                             const region_control_callbacks *callbacks,
                             void *context)
{
  control->callbacks = callbacks ? *callbacks : (region_control_callbacks){0};
  control->context = context;
  control->step_sample_active = 0;
}

/* 每步只反馈一次。先清标记，使显式结束、下一步和窗口结束不会重复消费。 */
static void finish_step_sample(region_control *control)
{
  if (!control->step_sample_active) return;
  double seconds = omp_get_wtime() - control->step_begin;
  control->step_sample_active = 0;
  control->callbacks.step_sample(control->context, control->step_id, seconds);
}

/* 开始总计时窗口，region 历史累计值保留。 */
void iteration_start(region_control *control)
{
  control->running = 1;
  control->total_start = omp_get_wtime();
  control->active = control->enabled;
}

/* 最后一个 step 的样本在窗口结束时反馈，不补发旧的 step_end 通知。 */
void iteration_end(region_control *control)
{
  assert(control->parallel_id == -1);
  if (!control->running) return;
  finish_step_sample(control);
  control->total_elapsed += omp_get_wtime() - control->total_start;
  control->active = control->running = 0;
  control->in_step = 0;
}

/* 配置调整发生于 start callback；整步计时从 callback 完成后开始。 */
void step_start(region_control *control, int step)
{
  if (!control->running) return;
  assert(control->parallel_id == -1);
  finish_step_sample(control);
  control->in_step = 1;
  control->step_id = step;
  if (control->callbacks.step_start)
    control->callbacks.step_start(control->context, step);
  if (control->callbacks.step_sample) {
    control->step_begin = omp_get_wtime();
    control->step_sample_active = 1;
  }
}

/* 显式结束同一步；未注册整步采样时仍只发送通知，不读取时间。 */
void step_end(region_control *control, int step)
{
  if (!control->running) return;
  assert(control->in_step && control->parallel_id == -1);
  assert(!control->step_sample_active || control->step_id == step);
  finish_step_sample(control);
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

/* callback 完成后采样；关闭报告时仍为 parallel_end 提供单次耗时。 */
void region_parallel_start(region_control *control, int id,
                           const char *file, const char *name, int line)
{
  (void)file; (void)name; (void)line;
  assert(id >= 0 && id < control->region_count);
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

/* 结束当前 parallel 的计时，再调用耗时 callback。 */
void region_parallel_end(region_control *control, int id)
{
  assert(control->parallel_id == id);
  if (control->sample_active) {
    double end = omp_get_wtime();
    double seconds = end - control->parallel_begin;
    if (control->active)
      control->elapsed[id] += seconds;
    if (control->running && control->in_step && control->callbacks.parallel_end)
      control->callbacks.parallel_end(control->context, id, seconds);
  }
  control->parallel_id = -1;
}

/* 源码中一对 START/END 对应一个完整 for 区间。 */
void region_for_start(region_control *control, int id,
                      const char *file, const char *name, int line)
{
  (void)file; (void)name; (void)line;
  assert(id >= 0 && id < control->region_count);
  if (!control->active) return;
  control->start[id] = omp_get_wtime();
}

/* END 显式放在末个普通 for 的 barrier 后，或尾部 nowait 的 parallel 外。 */
void region_for_end(region_control *control, int id)
{
  assert(id >= 0 && id < control->region_count);
  if (control->active)
    control->elapsed[id] += omp_get_wtime() - control->start[id];
}
