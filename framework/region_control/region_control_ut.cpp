#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "region_control.h"
#include <cassert>
#include <cstdio>
#include <omp.h>
#include <sched.h>
#include <vector>

static int clock_reads;

/* 仅用于测试：捕获控制层误加的 OpenMP 时钟读取。 */
extern "C" double __wrap_omp_get_wtime()
{
  ++clock_reads;
  return 0;
}

struct mock_tuner {
  hams_binding_cfg cfg[2];
  std::vector<int> events;
  double elapsed;
};

/* 仅用于测试：记录 step start 通知及 step 编号。 */
static void step_start(void *context, int step)
{
  static_cast<mock_tuner *>(context)->events.push_back(100 + step);
}

/* 仅用于测试：每个 region 固定返回自己的配置。 */
static const hams_binding_cfg *select_cfg(void *context, int id)
{
  auto &mock = *static_cast<mock_tuner *>(context);
  mock.events.push_back(10 + id);
  return &mock.cfg[id];
}

/* 仅用于测试：保存原样传入的单次耗时。 */
static void observe(void *context, int id, double seconds)
{
  auto &mock = *static_cast<mock_tuner *>(context);
  mock.events.push_back(20 + id);
  mock.elapsed = seconds;
}

/* 仅用于测试：记录 step finish 通知及 step 编号。 */
static void step_finish(void *context, int step)
{
  static_cast<mock_tuner *>(context)->events.push_back(200 + step);
}

/* 仅用于测试：在真正的下一次 parallel 中检查线程数和每线程实际 affinity。 */
static void run_region(region_control *control, mock_tuner &mock, int id,
                       double seconds)
{
  control_region_start(control, id);
  const auto &cfg = mock.cfg[id];
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
  control_region_finish(control, id, seconds);
  assert(mock.elapsed == seconds);
}

/* 仅用于测试：step callback 不改变调用线程的 affinity 或默认线程数。 */
static void check_step(region_control *control, int step, bool start)
{
  cpu_set_t before, after;
  int threads = omp_get_max_threads();
  int result = sched_getaffinity(0, sizeof(before), &before);
  assert(result == 0);
  if (start) control_step_start(control, step);
  else control_step_finish(control, step);
  result = sched_getaffinity(0, sizeof(after), &after);
  assert(result == 0);
  assert(CPU_EQUAL(&before, &after));
  assert(omp_get_max_threads() == threads && clock_reads == 0);
}

/* 仅用于测试：跨 step 验证共享 binding 的 A -> B -> A 恢复及固定配置复用。 */
int main()
{
  region_control *control = region_control_create();
  int full = region_control_max_threads(control);
  assert(full >= 2);
  mock_tuner mock{};
  for (int id = 0; id < 2; ++id) {
    auto &cfg = mock.cfg[id];
    cfg.thread_number = id == 0 ? full : full / 2;
    for (int tid = 0; tid < cfg.thread_number; ++tid) {
      int cpu = tid * (id + 1);
      cfg.mask[cpu] = true;
      cfg.tid_to_cpu[tid] = cpu;
    }
  }
  region_control_callbacks callbacks{step_start, select_cfg, observe, step_finish};
  region_control_register(control, &callbacks, &mock);
  callbacks = {};  // 注册后保存的是函数指针副本。
  check_step(control, 0, true);
  run_region(control, mock, 0, 0.125);
  run_region(control, mock, 1, 0.25);
  assert(region_control_max_threads(control) == full);
  check_step(control, 0, false);
  check_step(control, 1, true);
  run_region(control, mock, 0, 0.5);
  run_region(control, mock, 0, 0.75);
  check_step(control, 1, false);
  assert((mock.events == std::vector<int>{100, 10, 20, 11, 21, 200,
                                        101, 10, 20, 10, 20, 201}));
  callbacks = {nullptr, select_cfg, observe, nullptr};
  region_control_register(control, &callbacks, &mock);
  check_step(control, 2, true);
  check_step(control, 2, false);
  assert(mock.events.size() == 12 && clock_reads == 0);
  region_control_destroy(control);
  assert(mock.elapsed == 0.75);
  std::printf("region_control=PASS (callbacks, no clock, A->B->A, stable, elapsed)\n");
}
