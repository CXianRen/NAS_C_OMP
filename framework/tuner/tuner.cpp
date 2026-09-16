#include "tuner.h"
#include "../dummy/dummy.h"
#include "../hams/hams_binding.h"
#include "../j2025/j2025.h"
#include "../j2025_b/j2025_b.h"
#include "../otter/otter.h"
#include "../region_control/region_control.h"

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <strings.h>
#include <vector>

#include <hwloc.h>
#include <omp.h>

enum class tuning_scope { region, step };

/* 策略声明配置作用域；挂载、计时接口和绑定复用同一层。 */
struct tuner_operations {
  const char *name;
  tuning_scope scope;
  const hams_binding_cfg *(*select)(void *, int);
  void (*observe)(void *, int, double);
  void (*destroy)(void *, const region_info *);
};

struct tuner {
  region_control *control;
  hams_binding *binding;
  const tuner_operations *operations;
  void *policy;
  bool pin_threads;
};

static const tuner_operations dummy_operations{
    "dummy", tuning_scope::region,
    [](void *state, int id) { return dummy_select_cfg(static_cast<dummy *>(state), id); },
    [](void *state, int id, double seconds) { dummy_observe(static_cast<dummy *>(state), id, seconds); },
    [](void *state, const region_info *) { dummy_destroy(static_cast<dummy *>(state)); }};

static const tuner_operations j2025_operations{
    "j2025", tuning_scope::region,
    [](void *state, int id) { return j2025_select_cfg(static_cast<j2025 *>(state), id); },
    [](void *state, int id, double seconds) { j2025_observe(static_cast<j2025 *>(state), id, seconds); },
    [](void *state, const region_info *regions) { j2025_destroy(static_cast<j2025 *>(state), regions); }};

static const tuner_operations j2025_b_operations{
    "j2025_b", tuning_scope::region,
    [](void *state, int id) { return j2025_b_select_cfg(static_cast<j2025_b *>(state), id); },
    [](void *state, int id, double seconds) { j2025_b_observe(static_cast<j2025_b *>(state), id, seconds); },
    [](void *state, const region_info *regions) { j2025_b_destroy(static_cast<j2025_b *>(state), regions); }};

static const tuner_operations otter_operations{
    "otter", tuning_scope::step,
    [](void *state, int step) { return otter_select_cfg(static_cast<otter *>(state), step); },
    [](void *state, int, double seconds) { otter_observe(static_cast<otter *>(state), seconds); },
    [](void *state, const region_info *) { otter_destroy(static_cast<otter *>(state)); }};

[[noreturn]] static void fail(const char *message)
{
  std::fprintf(stderr, "tuner: %s\n", message);
  std::exit(EXIT_FAILURE);
}

/* Otter 分支的选项解析规则：无效值回退默认值，合法数字截断到允许范围。 */
static bool env_false(const char *name)
{
  const char *value = std::getenv(name);
  return value && (!strcasecmp(value, "0") || !strcasecmp(value, "false") ||
                   !strcasecmp(value, "no") || !strcasecmp(value, "off"));
}

static int env_int(const char *name, int fallback, int low, int high)
{
  const char *value = std::getenv(name);
  if (!value || !*value) return fallback;
  char *end = nullptr;
  errno = 0;
  long parsed = std::strtol(value, &end, 10);
  if (errno || end == value || *end) return fallback;
  return static_cast<int>(std::clamp(parsed, static_cast<long>(low), static_cast<long>(high)));
}

static double env_double(const char *name, double fallback, double low, double high)
{
  const char *value = std::getenv(name);
  if (!value || !*value) return fallback;
  char *end = nullptr;
  errno = 0;
  double parsed = std::strtod(value, &end);
  if (errno || end == value || *end || !std::isfinite(parsed)) return fallback;
  return std::clamp(parsed, low, high);
}

/* 沿用 otter:otter/otter_tuner.cpp (42e091c) 的候选 CPU 规则：每个物理核
 * 取一个 PU，或 OTTER_PHYSICAL_CORES=0 时取所有 PU，按系统 CPU ID 升序。
 * 与当前 HAMS 一致，调用方须保证所选 CPU 可绑定。只在 attach 时发现拓扑。 */
static std::vector<int> discover_otter_cpus()
{
  std::bitset<HAMS_CPU_COUNT> candidates;
  hwloc_topology_t topology;
  if (hwloc_topology_init(&topology) == 0) {
    if (hwloc_topology_load(topology) == 0) {
      hwloc_obj_type_t type = env_false("OTTER_PHYSICAL_CORES") ? HWLOC_OBJ_PU : HWLOC_OBJ_CORE;
      hwloc_obj_t object = nullptr;
      while ((object = hwloc_get_next_obj_by_type(topology, type, object))) {
        for (int cpu = hwloc_bitmap_first(object->cpuset); cpu >= 0;
             cpu = hwloc_bitmap_next(object->cpuset, cpu)) {
          if (cpu < HAMS_CPU_COUNT) {
            candidates[cpu] = true;
            break;
          }
        }
      }
    }
    hwloc_topology_destroy(topology);
  }
  std::vector<int> cpus;
  for (int cpu = 0; cpu < HAMS_CPU_COUNT; ++cpu)
    if (candidates[cpu]) cpus.push_back(cpu);
  return cpus;
}

static void apply_selected(tuner *runtime, int id)
{
  const auto *cfg = runtime->operations->select(runtime->policy, id);
  if (runtime->pin_threads) {
    // 每个策略仅在自己的调优边界调用；相同配置由共享 HAMS 去重。
    hams_binding_apply(runtime->binding, cfg);
  } else {
    // 保留 Otter 的仅线程数路径（例如 OMP_PROC_BIND 已由用户设置）。
    omp_set_dynamic(0);
    omp_set_num_threads(cfg->thread_number);
  }
}

static void on_parallel_start(void *context, int id)
{
  apply_selected(static_cast<tuner *>(context), id);
}

static void on_parallel_end(void *context, int id, double seconds)
{
  auto *runtime = static_cast<tuner *>(context);
  runtime->operations->observe(runtime->policy, id, seconds);
}

/* Otter 全局配置只在 step_start 应用；该步所有 parallel 共享配置。 */
static void on_step_start(void *context, int step)
{
  apply_selected(static_cast<tuner *>(context), step);
}

/* region_control 提供整步墙钟时间，包括 region 之间的串行部分。 */
static void on_step_sample(void *context, int, double seconds)
{
  auto *runtime = static_cast<tuner *>(context);
  runtime->operations->observe(runtime->policy, 0, seconds);
}

tuner *tuner_attach(region_control *control)
{
  return tuner_attach_named(control, std::getenv("TUNER"));
}

tuner *tuner_attach_named(region_control *control, const char *name)
{
  if (!name || !*name || !strcasecmp(name, "none")) return nullptr;
  const tuner_operations *operations = nullptr;
  if (!strcasecmp(name, "dummy")) operations = &dummy_operations;
  else if (!strcasecmp(name, "j2025")) operations = &j2025_operations;
  else if (!strcasecmp(name, "j2025_b")) operations = &j2025_b_operations;
  else if (!strcasecmp(name, "otter")) operations = &otter_operations;
  else fail("TUNER must be none, dummy, j2025, j2025_b or otter");
  if (!REGION_INSTRUMENT) fail("TUNER requires INSTRUMENT=1");
  assert(control && control->region_count > 0);
  assert(!control->context && !control->callbacks.parallel_start &&
         !control->callbacks.parallel_end && !control->callbacks.step_start &&
         !control->callbacks.step_end && !control->callbacks.step_sample);

  auto *runtime = new (std::nothrow) tuner{};
  if (!runtime) fail("cannot allocate runtime");
  runtime->control = control;
  runtime->operations = operations;
  runtime->binding = hams_binding_create();
  hams_binding_status status;
  hams_binding_get_status(runtime->binding, &status);

  if (operations == &dummy_operations) {
    if (!status.supported) fail("Dummy requires OMP_PROC_BIND=false");
    runtime->pin_threads = true;
    runtime->policy = dummy_create(status.max_threads);
  } else if (operations == &j2025_operations) {
    if (!status.supported) fail("J2025 requires OMP_PROC_BIND=false");
    if (status.max_threads < 2 || status.max_threads % 2)
      fail("J2025 requires an even initial thread limit of at least 2");
    runtime->pin_threads = true;
    runtime->policy = j2025_create(control->region_count, status.max_threads);
  } else if (operations == &j2025_b_operations) {
    if (!status.supported) fail("J2025_B requires OMP_PROC_BIND=false");
    if (status.max_threads < 2 || status.max_threads % 2)
      fail("J2025_B requires an even initial thread limit of at least 2");
    runtime->pin_threads = true;
    runtime->policy = j2025_b_create(control->region_count, status.max_threads);
  } else {
    auto cpus = discover_otter_cpus();
    int maximum = status.max_threads;
    runtime->pin_threads = !env_false("OTTER_PIN_THREADS") && status.supported && !cpus.empty();
    if (!cpus.empty()) maximum = std::min(maximum, static_cast<int>(cpus.size()));
    maximum = env_int("OTTER_MAX_THREADS", maximum, 1, maximum);
    // 无拓扑时继续调线程数；占位映射不会传给 HAMS。
    if (cpus.empty())
      for (int cpu = 0; cpu < maximum; ++cpu) cpus.push_back(cpu);
    otter_options options;
    options.threshold_fraction = env_double("OTTER_THRESHOLD_PERCENT", 10.0, 0.0, 100.0) / 100.0;
    options.golden_distance = env_int("OTTER_GOLDEN_DISTANCE", (maximum + 7) / 8, 1, maximum);
    options.placement_supported = runtime->pin_threads;
    options.verbose = env_int("OTTER_VERBOSE", 1, 0, 1);
    runtime->policy = otter_create(maximum, cpus.data(),
                                  static_cast<int>(cpus.size()), options);
  }

  static const region_control_callbacks region_callbacks{
      nullptr, on_parallel_start, on_parallel_end, nullptr, nullptr};
  static const region_control_callbacks step_callbacks{
      on_step_start, nullptr, nullptr, nullptr, on_step_sample};
  const auto *callbacks = operations->scope == tuning_scope::step
                              ? &step_callbacks : &region_callbacks;
  region_control_register(control, callbacks, runtime);
  return runtime;
}

const char *tuner_name(const tuner *runtime)
{
  return runtime ? runtime->operations->name : "none";
}

void tuner_detach(tuner *runtime)
{
  if (!runtime) return;
  region_control_register(runtime->control, nullptr, nullptr);
  runtime->operations->destroy(runtime->policy, runtime->control->regions);
  hams_binding_destroy(runtime->binding);
  delete runtime;
}
