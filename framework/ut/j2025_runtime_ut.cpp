#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "../j2025/j2025.h"
#include "../j2025/j2025_runtime.h"
#include "../region_control/region_control.h"
#include <cassert>
#include <cstdio>
#include <omp.h>
#include <sched.h>
#include <vector>

static int clock_reads;

/* 仅测试：整数时钟验证回调开销在样本外，step 不读钟。 */
extern "C" double __wrap_omp_get_wtime()
{
  assert(omp_get_thread_num() == 0);
  return ++clock_reads;
}

struct j2025 {
  hams_binding_cfg cfg[2];
  std::vector<int> events;
  int maximum;
  double elapsed;
};

static j2025 *mock;
static bool destroyed;

/* 仅测试：模拟纯算法，生成固定的满线程 A 和半线程 spread B。 */
j2025 *j2025_create(int region_count, int max_threads)
{
  assert(region_count == 2 && max_threads >= 2);
  mock = new j2025{};
  mock->maximum = max_threads;
  for (int id = 0; id < 2; ++id) {
    auto &cfg = mock->cfg[id];
    cfg.thread_number = id == 0 ? max_threads : max_threads / 2;
    for (int tid = 0; tid < cfg.thread_number; ++tid) {
      int cpu = tid * (id + 1);
      cfg.mask[cpu] = true;
      cfg.tid_to_cpu[tid] = cpu;
    }
  }
  return mock;
}

/* 仅测试：返回预生成配置并模拟选择开销，实际绑定由 runtime 执行。 */
const hams_binding_cfg *j2025_select_cfg(j2025 *tuner, int id)
{
  assert(tuner == mock);
  tuner->events.push_back(10 + id);
  omp_get_wtime();
  return &tuner->cfg[id];
}

/* 仅测试：保存单次样本，再模拟反馈开销。 */
void j2025_observe(j2025 *tuner, int id, double seconds)
{
  assert(tuner == mock && seconds == 1.0);
  tuner->events.push_back(20 + id);
  tuner->elapsed = seconds;
  omp_get_wtime();
}

/* 仅测试：确认 runtime 传递 region 名称并释放纯算法状态。 */
void j2025_destroy(j2025 *tuner, const region_info *regions)
{
  assert(tuner == mock);
  assert(regions && regions[0].name[0] == 'A' && regions[1].name[0] == 'B');
  delete tuner;
  mock = nullptr;
  destroyed = true;
}

/* 仅测试：读取真实下一次 parallel 的线程数和每线程单 CPU affinity。 */
static void check_binding(const hams_binding_cfg &cfg)
{
  #pragma omp parallel
  {
    cpu_set_t mask;
    int cpu = cfg.tid_to_cpu[omp_get_thread_num()];
    int result = sched_getaffinity(0, sizeof(mask), &mask);
    assert(result == 0);
    assert(omp_get_num_threads() == cfg.thread_number);
    assert(CPU_COUNT(&mask) == 1 && CPU_ISSET(cpu, &mask));
    assert(sched_getcpu() == cpu);
  }
}

/* 仅测试：通过真实手动桩调用 runtime，验证配置和样本转发。 */
static void run_region(region_control *control, int id)
{
  PARALLEL_START(control, id);
  check_binding(mock->cfg[id]);
  PARALLEL_END(control, id);
  assert(mock->elapsed == 1.0);
}

/* 仅测试：step 不读取时钟、不触发算法选择、不改变线程配置。 */
static void check_step(region_control *control, int step, bool start)
{
  cpu_set_t before, after;
  int threads = omp_get_max_threads(), reads = clock_reads;
  auto events = mock->events.size();
  int result = sched_getaffinity(0, sizeof(before), &before);
  assert(result == 0);
  if (start) step_start(control, step);
  else step_end(control, step);
  result = sched_getaffinity(0, sizeof(after), &after);
  assert(result == 0 && CPU_EQUAL(&before, &after));
  assert(omp_get_max_threads() == threads && clock_reads == reads);
  assert(mock->events.size() == events);
}

/* 仅测试：真实 A -> B -> A 恢复、固定配置复用及 detach 释放/注销。 */
int main()
{
  region_info regions[] = {{"A", -1, 0, nullptr, 0, 0},
                           {"B", -1, 0, nullptr, 0, 0}};
  region_control control;
  region_control_init(&control, regions, 2, 0);
  int full = omp_get_max_threads();
  j2025_runtime *runtime = j2025_attach(&control);
  assert(mock->maximum == full && clock_reads == 0);
  assert(!control.callbacks.step_start && !control.callbacks.step_end);
  iteration_start(&control);
  check_step(&control, 0, true);
  run_region(&control, 0);
  run_region(&control, 1);
  check_step(&control, 1, true);  // step_end 可选，搜索状态跨 step 保留。
  run_region(&control, 0);
  run_region(&control, 0);
  check_step(&control, 1, false);
  iteration_end(&control);
  assert(mock->maximum == full && clock_reads == 18);
  assert((mock->events == std::vector<int>{10, 20, 11, 21, 10, 20, 10, 20}));
  hams_binding_cfg last_cfg = mock->cfg[0];
  j2025_detach(runtime);
  assert(destroyed && !mock && !control.context);
  assert(!control.callbacks.parallel_start && !control.callbacks.parallel_end);
  assert(!control.callbacks.step_start && !control.callbacks.step_end);
  assert(omp_get_max_threads() == full && clock_reads == 18);
  check_binding(last_cfg);  // 释放后保留默认线程数和各工作线程的 affinity。
  std::printf("j2025_runtime=PASS (A%d->B%d->A%d, callbacks, step, sample, detach)\n",
              full, full / 2, full);
}
