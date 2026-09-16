#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "../j2025/j2025.h"
#include "../j2025_b/j2025_b.h"
#include "../otter/otter.h"
#include "../tuner/tuner.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <omp.h>
#include <sched.h>

static bool count_allocations;
static unsigned allocations;
static int clock_reads;
static double clock_now;

/* 关闭 tuner 的路径不能创建 runtime、binding 或搜索状态。 */
void *operator new(std::size_t size)
{
  if (count_allocations) ++allocations;
  if (void *p = std::malloc(size ? size : 1)) return p;
  throw std::bad_alloc();
}

void *operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
void operator delete[](void *p, std::size_t) noexcept { std::free(p); }

void *operator new(std::size_t size, const std::nothrow_t &) noexcept
{
  try { return ::operator new(size); }
  catch (...) { return nullptr; }
}

void *operator new[](std::size_t size, const std::nothrow_t &) noexcept
{
  try { return ::operator new[](size); }
  catch (...) { return nullptr; }
}

void operator delete(void *p, const std::nothrow_t &) noexcept { std::free(p); }
void operator delete[](void *p, const std::nothrow_t &) noexcept { std::free(p); }

/* 整数时钟把 select/observe 的模拟开销与 region 样本分开。 */
extern "C" double __wrap_omp_get_wtime()
{
  assert(omp_get_thread_num() == 0);
  ++clock_reads;
  return ++clock_now;
}

struct mock_policy {
  hams_binding_cfg cfg[2];
  int events[16];
  int event_count;
  int maximum;
  double elapsed;
};

struct j2025 { mock_policy state; };
struct j2025_b { mock_policy state; };
struct otter {
  mock_policy state;
  int selections, observations;
  double samples[4];
};

static mock_policy *mock;
static j2025 *j2025_mock;
static j2025_b *j2025_b_mock;
static otter *otter_mock;
static double expected_otter_seconds;
static int j2025_created, j2025_b_created, otter_created, destroyed;
static int j2025_selected, j2025_observed, j2025_destroyed;
static int j2025_b_selected, j2025_b_observed, j2025_b_destroyed;

/* 搜索策略都只给出固定配置；UT 不依赖各自搜索算法。 */
static void init_mock(mock_policy *state, int max_threads,
                      const int *cpus, int cpu_count)
{
  assert(!mock && max_threads >= 2);
  assert(cpu_count >= max_threads);
  mock = state;
  mock->maximum = max_threads;
  for (int i = 0; i < cpu_count; ++i) {
    assert(cpus[i] >= 0 && cpus[i] < HAMS_CPU_COUNT);
    assert(i == 0 || cpus[i - 1] < cpus[i]);
  }
  for (int id = 0; id < 2; ++id) {
    auto &cfg = mock->cfg[id];
    cfg.thread_number = id == 0 ? max_threads : max_threads / 2;
    for (int tid = 0; tid < cfg.thread_number; ++tid) {
      int cpu = cpus[tid * (id + 1)];
      cfg.mask[cpu] = true;
      cfg.tid_to_cpu[tid] = cpu;
    }
  }
}

template <typename Policy>
static Policy *create_region_mock(int region_count, int max_threads)
{
  assert(region_count == 2);
  cpu_set_t allowed;
  int rc = sched_getaffinity(0, sizeof(allowed), &allowed);
  assert(rc == 0);
  int cpus[HAMS_CPU_COUNT], count = 0;
  for (int cpu = 0; cpu < HAMS_CPU_COUNT && cpu < CPU_SETSIZE; ++cpu)
    if (CPU_ISSET(cpu, &allowed)) cpus[count++] = cpu;
  auto *policy = new Policy{};
  init_mock(&policy->state, max_threads, cpus, count);
  return policy;
}

j2025 *j2025_create(int region_count, int max_threads)
{
  ++j2025_created;
  j2025_mock = create_region_mock<j2025>(region_count, max_threads);
  return j2025_mock;
}

j2025_b *j2025_b_create(int region_count, int max_threads)
{
  ++j2025_b_created;
  j2025_b_mock = create_region_mock<j2025_b>(region_count, max_threads);
  return j2025_b_mock;
}

otter *otter_create(int max_threads, const int *cpus,
                    int cpu_count, const otter_options &options)
{
  ++otter_created;
  assert(options.placement_supported);
  auto *policy = new otter{};
  init_mock(&policy->state, max_threads, cpus, cpu_count);
  otter_mock = policy;
  return policy;
}

static const hams_binding_cfg *select(mock_policy *state, int id)
{
  assert(state == mock && id >= 0 && id < 2);
  assert(mock->event_count < 16);
  mock->events[mock->event_count++] = 10 + id;
  omp_get_wtime();
  return &mock->cfg[id];
}

static void observe(mock_policy *state, int id, double seconds)
{
  assert(state == mock && id >= 0 && id < 2 && seconds == 1.0);
  assert(mock->event_count < 16);
  mock->events[mock->event_count++] = 20 + id;
  mock->elapsed = seconds;
  omp_get_wtime();
}

const hams_binding_cfg *j2025_select_cfg(j2025 *policy, int id)
{
  assert(policy == j2025_mock && !j2025_b_mock);
  ++j2025_selected;
  return select(&policy->state, id);
}

const hams_binding_cfg *j2025_b_select_cfg(j2025_b *policy, int id)
{
  assert(policy == j2025_b_mock && !j2025_mock);
  ++j2025_b_selected;
  return select(&policy->state, id);
}

const hams_binding_cfg *otter_select_cfg(otter *policy, int step)
{
  assert(policy == otter_mock && policy->selections == policy->observations);
  assert(step == 10 * (policy->selections + 1));  // 保留应用的 step 编号。
  int cfg = policy->selections++ % 2;
  clock_now += 100.0;  // 故意放大策略开销，不能进入 step 样本。
  return select(&policy->state, cfg);
}

void j2025_observe(j2025 *policy, int id, double seconds)
{
  assert(policy == j2025_mock && !j2025_b_mock);
  ++j2025_observed;
  observe(&policy->state, id, seconds);
}

void j2025_b_observe(j2025_b *policy, int id, double seconds)
{
  assert(policy == j2025_b_mock && !j2025_mock);
  ++j2025_b_observed;
  observe(&policy->state, id, seconds);
}

void otter_observe(otter *policy, double seconds)
{
  assert(policy == otter_mock && &policy->state == mock);
  assert(policy->selections == policy->observations + 1);
  assert(seconds == expected_otter_seconds && policy->observations < 4);
  mock->events[mock->event_count++] = 30 + policy->observations;
  policy->samples[policy->observations++] = seconds;
  mock->elapsed = seconds;
  clock_now += 200.0;
  omp_get_wtime();
}

static void check_destroy(mock_policy *state, const region_info *regions)
{
  assert(state == mock);
  assert(regions && std::strcmp(regions[0].name, "A") == 0);
  assert(std::strcmp(regions[1].name, "B") == 0);
  mock = nullptr;
  ++destroyed;
}

void j2025_destroy(j2025 *policy, const region_info *regions)
{
  assert(policy == j2025_mock && !j2025_b_mock);
  ++j2025_destroyed;
  j2025_mock = nullptr;
  check_destroy(&policy->state, regions);
  delete policy;
}

void j2025_b_destroy(j2025_b *policy, const region_info *regions)
{
  assert(policy == j2025_b_mock && !j2025_mock);
  ++j2025_b_destroyed;
  j2025_b_mock = nullptr;
  check_destroy(&policy->state, regions);
  delete policy;
}

void otter_destroy(otter *policy, bool report)
{
  assert(policy == otter_mock && &policy->state == mock && report);
  assert(policy->selections == 3 && policy->observations == 3);
  mock = nullptr;
  otter_mock = nullptr;
  ++destroyed;
  delete policy;
}

static void check_unregistered(const region_control &control)
{
  assert(!control.context && !control.callbacks.parallel_start);
  assert(!control.callbacks.parallel_end && !control.callbacks.step_start);
  assert(!control.callbacks.step_end && !control.callbacks.step_sample);
}

struct team_snapshot {
  int count;
  cpu_set_t masks[HAMS_CPU_COUNT];
};

static void capture_team(team_snapshot &snapshot)
{
  #pragma omp parallel
  {
    int tid = omp_get_thread_num();
    assert(tid < HAMS_CPU_COUNT);
    int rc = sched_getaffinity(0, sizeof(cpu_set_t), &snapshot.masks[tid]);
    assert(rc == 0);
    #pragma omp single
    snapshot.count = omp_get_num_threads();
  }
}

static void check_same_team(const team_snapshot &before,
                            const team_snapshot &after)
{
  assert(before.count == after.count);
  for (int tid = 0; tid < before.count; ++tid)
    assert(CPU_EQUAL(&before.masks[tid], &after.masks[tid]));
}

/* 同时覆盖已有固定线程和已有 dynamic 设置，提前创建线程池。 */
static void check_disabled(const char *mode)
{
  for (int dynamic = 0; dynamic <= 1; ++dynamic) {
    omp_set_dynamic(dynamic);
    int initial_dynamic = omp_get_dynamic();
    int full = omp_get_max_threads();
    assert(full <= HAMS_CPU_COUNT);
    team_snapshot before{}, after{};
    capture_team(before);
    region_info regions[] = {{"A", -1, 0, nullptr, 0, 0},
                             {"B", -1, 0, nullptr, 0, 0}};
    region_control control;
    region_control_init(&control, regions, 2, 0);
    int reads = clock_reads;
    allocations = 0;
    count_allocations = true;
    tuner *runtime = tuner_attach(&control);
    assert(!runtime && std::strcmp(tuner_name(runtime), "none") == 0);
    check_unregistered(control);
    iteration_start(&control);
    step_start(&control, 0);
    for (int id = 0; id < 2; ++id) {
      PARALLEL_START(&control, id);
      capture_team(after);
      check_same_team(before, after);
      PARALLEL_END(&control, id);
    }
    step_end(&control, 0);
    iteration_end(&control);
    tuner_detach(runtime);
    count_allocations = false;
    assert(allocations == 0 && !mock);
    assert(!j2025_created && !j2025_b_created && !otter_created && !destroyed);
    assert(clock_reads == reads + 2);  // 只有应用总窗口读钟。
    assert(omp_get_dynamic() == initial_dynamic && omp_get_max_threads() == full);
    check_unregistered(control);
    capture_team(after);
    check_same_team(before, after);
  }
  std::printf("tuner_runtime=%s PASS (no allocation, callbacks, affinity or OpenMP changes)\n",
              mode);
}

static void check_binding(const hams_binding_cfg &cfg)
{
  #pragma omp parallel
  {
    int tid = omp_get_thread_num();
    assert(omp_get_num_threads() == cfg.thread_number);
    int cpu = cfg.tid_to_cpu[tid];
    cpu_set_t mask;
    int rc = sched_getaffinity(0, sizeof(mask), &mask);
    assert(rc == 0 && CPU_COUNT(&mask) == 1 && CPU_ISSET(cpu, &mask));
    assert(sched_getcpu() == cpu);
  }
}

static void check_step(region_control *control, int step, bool start)
{
  cpu_set_t before, after;
  int threads = omp_get_max_threads(), reads = clock_reads;
  int events = mock->event_count;
  int rc = sched_getaffinity(0, sizeof(before), &before);
  assert(rc == 0);
  if (start) step_start(control, step);
  else step_end(control, step);
  rc = sched_getaffinity(0, sizeof(after), &after);
  assert(rc == 0 && CPU_EQUAL(&before, &after));
  assert(omp_get_max_threads() == threads && clock_reads == reads);
  assert(mock->event_count == events);
}

static void run_region(region_control *control, int id)
{
  PARALLEL_START(control, id);
  check_binding(mock->cfg[id]);
  PARALLEL_END(control, id);
  assert(mock->elapsed == 1.0);
}

/* 两个独立策略均按 region 选择 A -> B -> A，回调只能访问各自 API。 */
static void check_j2025(const char *name)
{
  region_info regions[] = {{"A", -1, 0, nullptr, 0, 0},
                           {"B", -1, 0, nullptr, 0, 0}};
  region_control control;
  region_control_init(&control, regions, 2, 0);
  int full = omp_get_max_threads();
  tuner *runtime = tuner_attach(&control);
  assert(runtime && std::strcmp(tuner_name(runtime), name) == 0);
  assert(mock && mock->maximum == full && clock_reads == 0);
  int maximum = mock->maximum;
  bool variant_b = std::strcmp(name, "j2025_b") == 0;
  assert(j2025_created == !variant_b && j2025_b_created == variant_b);
  assert(otter_created == 0);
  assert(control.context && control.callbacks.parallel_start && control.callbacks.parallel_end);
  assert(!control.callbacks.step_start && !control.callbacks.step_end);
  assert(!control.callbacks.step_sample);
  iteration_start(&control);
  check_step(&control, 0, true);
  run_region(&control, 0);
  run_region(&control, 1);
  check_step(&control, 1, true);  // step_end 可选，不重置 region 配置。
  run_region(&control, 0);
  run_region(&control, 0);       // 相同配置重复执行仍须正常采样。
  check_step(&control, 1, false);
  iteration_end(&control);
  assert(clock_reads == 18 && mock->maximum == maximum);
  assert(j2025_selected == (variant_b ? 0 : 4));
  assert(j2025_observed == (variant_b ? 0 : 4));
  assert(j2025_b_selected == (variant_b ? 4 : 0));
  assert(j2025_b_observed == (variant_b ? 4 : 0));
  const int expected[] = {10, 20, 11, 21, 10, 20, 10, 20};
  assert(mock->event_count == 8);
  for (int i = 0; i < 8; ++i) assert(mock->events[i] == expected[i]);
  hams_binding_cfg last = mock->cfg[0];
  tuner_detach(runtime);
  assert(destroyed == 1 && !mock && clock_reads == 18);
  assert(j2025_destroyed == !variant_b && j2025_b_destroyed == variant_b);
  assert(!j2025_mock && !j2025_b_mock);
  check_unregistered(control);
  assert(omp_get_max_threads() == maximum && omp_get_dynamic() == 0);
  check_binding(last);
  iteration_start(&control);
  step_start(&control, 2);
  PARALLEL_START(&control, 1);
  check_binding(last);         // 注销之后不再选择 B，也不访问已释放状态。
  PARALLEL_END(&control, 1);
  step_end(&control, 2);
  iteration_end(&control);
  assert(clock_reads == 20 && destroyed == 1 && !mock);
  std::printf("tuner_runtime=%s PASS (independent API, A%d->B%d->A%d, sample, step, detach)\n",
              name, maximum, maximum / 2, maximum);
}

/* 记录真实 dummy runtime 的回调，同时继续执行原始选择和反馈流程。 */
static region_control_callbacks dummy_callbacks;
static int dummy_selections, dummy_observations, dummy_region;
static double dummy_seconds;

static void dummy_parallel_start(void *context, int id)
{
  assert(id == dummy_region && dummy_selections == dummy_observations);
  ++dummy_selections;
  dummy_callbacks.parallel_start(context, id);
}

static void dummy_parallel_end(void *context, int id, double seconds)
{
  assert(id == dummy_region && dummy_selections == dummy_observations + 1);
  assert(seconds == dummy_seconds);
  ++dummy_observations;
  dummy_callbacks.parallel_end(context, id, seconds);
}

/* dummy 始终使用挂载时的满线程配置，报告开关不关闭选择、绑定和采样。 */
static void check_dummy(int report)
{
  region_info regions[] = {{"A", -1, 0, nullptr, 0, 0},
                           {"B", -1, 0, nullptr, 0, 0}};
  region_control control;
  region_control_init(&control, regions, 2, report);
  int full = omp_get_max_threads();
  if (full > omp_get_thread_limit()) full = omp_get_thread_limit();
  if (full > HAMS_CPU_COUNT) full = HAMS_CPU_COUNT;
  hams_binding_cfg expected{};
  expected.thread_number = full;
  for (int tid = 0; tid < full; ++tid) {
    expected.mask[tid] = true;
    expected.tid_to_cpu[tid] = tid;
  }
  int rc = setenv("OTTER_MAX_THREADS", "1", 1);  // Otter 限制不能影响 dummy。
  assert(rc == 0);
  tuner *runtime = tuner_attach(&control);
  assert(runtime && std::strcmp(tuner_name(runtime), "dummy") == 0);
  assert(!mock && !j2025_created && !j2025_b_created && !otter_created && !destroyed);
  assert(control.context && control.callbacks.parallel_start && control.callbacks.parallel_end);
  assert(!control.callbacks.step_start && !control.callbacks.step_end);
  assert(!control.callbacks.step_sample && clock_reads == 0);
  dummy_callbacks = control.callbacks;
  control.callbacks.parallel_start = dummy_parallel_start;
  control.callbacks.parallel_end = dummy_parallel_end;

  const int ids[] = {0, 1, 0, 1};
  const double work[] = {1.0, 7.0, 0.0, 20.0};
  iteration_start(&control);
  for (int i = 0; i < 4; ++i) {
    if (i % 2 == 0) {
      int reads = clock_reads;
      step_start(&control, i / 2);  // 前一步可省略 step_end。
      assert(clock_reads == reads && dummy_selections == i);
    }
    dummy_region = ids[i];
    dummy_seconds = work[i] + 1.0;
    PARALLEL_START(&control, ids[i]);
    assert(dummy_selections == i + 1 && dummy_observations == i);
    check_binding(expected);
    clock_now += work[i];
    PARALLEL_END(&control, ids[i]);
    assert(dummy_observations == i + 1);
    check_binding(expected);
  }
  int reads = clock_reads;
  step_end(&control, 1);
  assert(clock_reads == reads);
  iteration_end(&control);
  assert(clock_reads == 10 && dummy_selections == 4 && dummy_observations == 4);
  assert(control.elapsed[0] == (report ? 3.0 : 0.0));
  assert(control.elapsed[1] == (report ? 29.0 : 0.0));
  assert(omp_get_max_threads() == full && omp_get_dynamic() == 0);
  tuner_detach(runtime);
  check_unregistered(control);
  check_binding(expected);
  assert(!mock && !j2025_created && !j2025_b_created && !otter_created && !destroyed);
  assert(clock_reads == 10);
  std::printf("tuner_runtime=dummy report=%d PASS (full=%d, select, binding, sample, detach)\n",
              report, full);
}

/* Otter 的同一 step 内，所有 region（包括重复 region）共享当前配置。 */
static void run_otter_region(region_control *control, int id, int cfg,
                              double work)
{
  int selections = otter_mock->selections, observations = otter_mock->observations;
  int events = mock->event_count;
  PARALLEL_START(control, id);
  assert(otter_mock->selections == selections && otter_mock->observations == observations);
  check_binding(mock->cfg[cfg]);
  clock_now += work;
  PARALLEL_END(control, id);
  assert(otter_mock->selections == selections && otter_mock->observations == observations);
  assert(mock->event_count == events);
  check_binding(mock->cfg[cfg]);
}

/* 整步样本包含串行间隙，排除选择/反馈；报告开关不控制 Otter 采样。 */
static void check_otter(int report)
{
  region_info regions[] = {{"A", -1, 0, nullptr, 0, 0},
                           {"B", -1, 0, nullptr, 0, 0}};
  region_control control;
  region_control_init(&control, regions, 2, report);
  int full = omp_get_max_threads();
  tuner *runtime = tuner_attach(&control);
  assert(runtime && std::strcmp(tuner_name(runtime), "otter") == 0);
  assert(mock && mock->maximum <= full && clock_reads == 0);
  assert(otter_created == 1 && j2025_created == 0 && j2025_b_created == 0);
  assert(control.context && control.callbacks.step_start && control.callbacks.step_sample);
  assert(!control.callbacks.parallel_start && !control.callbacks.parallel_end);
  assert(!control.callbacks.step_end && !otter_mock->selections);
  step_start(&control, 0);  // 正式窗口外不选择配置。
  step_end(&control, 0);
  assert(!otter_mock->selections && clock_reads == 0);
  iteration_start(&control);

  step_start(&control, 10);
  assert(otter_mock->selections == 1 && !otter_mock->observations);
  check_binding(mock->cfg[0]);
  run_otter_region(&control, 0, 0, 2.0);
  clock_now += 11.0;  // 串行工作，不属于任何 region。
  run_otter_region(&control, 1, 0, 3.0);
  run_otter_region(&control, 0, 0, 5.0);
  expected_otter_seconds = 22.0 + report * 6.0;
  step_end(&control, 10);
  assert(otter_mock->observations == 1 && otter_mock->selections == 1);
  assert(control.elapsed[0] + control.elapsed[1] == (report ? 13.0 : 0.0));
  check_binding(mock->cfg[0]);

  step_start(&control, 20);  // 上个 step 显式结束过，不能重复反馈。
  assert(otter_mock->observations == 1 && otter_mock->selections == 2);
  check_binding(mock->cfg[1]);
  run_otter_region(&control, 1, 1, 4.0);
  clock_now += 7.0;
  run_otter_region(&control, 1, 1, 6.0);  // 本 step 跳过 A，重复 B。
  expected_otter_seconds = 18.0 + report * 4.0;
  step_start(&control, 30);  // 缺省 step_end：先反馈上个 step，再选择新配置。
  assert(otter_mock->observations == 2 && otter_mock->selections == 3);
  check_binding(mock->cfg[0]);
  run_otter_region(&control, 0, 0, 8.0);
  clock_now += 13.0;
  run_otter_region(&control, 1, 0, 9.0);
  expected_otter_seconds = 31.0 + report * 4.0;
  iteration_end(&control);  // 最后一步缺省 step_end 也只反馈一次。
  assert(otter_mock->observations == 3 && otter_mock->selections == 3);
  assert(otter_mock->samples[0] == 22.0 + report * 6.0);
  assert(otter_mock->samples[1] == 18.0 + report * 4.0);
  assert(otter_mock->samples[2] == 31.0 + report * 4.0);
  const int expected[] = {10, 30, 11, 31, 10, 32};
  assert(mock->event_count == 6);
  for (int i = 0; i < 6; ++i) assert(mock->events[i] == expected[i]);
  check_binding(mock->cfg[0]);
  int reads = clock_reads;
  iteration_end(&control);
  assert(clock_reads == reads && otter_mock->observations == 3);
  hams_binding_cfg last = mock->cfg[0];
  tuner_detach(runtime);
  assert(destroyed == 1 && !mock && !otter_mock && clock_reads == reads);
  check_unregistered(control);
  check_binding(last);
  std::printf("tuner_runtime=otter report=%d PASS (one config per step, serial gaps, optional end, detach)\n",
              report);
}

/* Makefile 为每种模式启动新进程，避免一个模式的线程绑定污染另一个。 */
int main(int argc, char **argv)
{
  assert(argc == 2);
  const char *mode = argv[1];
  bool default_mode = std::strcmp(mode, "default") == 0;
  bool otter_report = std::strcmp(mode, "otter-report") == 0;
  bool dummy_report = std::strcmp(mode, "dummy-report") == 0;
  bool j2025_b_case = std::strcmp(mode, "j2025_b-case") == 0;
  const char *selection = otter_report ? "otter" : dummy_report ? "DuMmY" :
                          j2025_b_case ? "J2025_b" : mode;
  int rc = default_mode ? unsetenv("TUNER") : setenv("TUNER", selection, 1);
  assert(rc == 0);
  if (default_mode || std::strcmp(mode, "none") == 0) check_disabled(mode);
  else if (std::strcmp(mode, "j2025") == 0) check_j2025("j2025");
  else if (std::strcmp(mode, "j2025_b") == 0 || j2025_b_case) check_j2025("j2025_b");
  else if (std::strcmp(mode, "dummy") == 0 || dummy_report) check_dummy(dummy_report);
  else if (std::strcmp(mode, "otter") == 0 || otter_report) check_otter(otter_report);
  else assert(false);
}
