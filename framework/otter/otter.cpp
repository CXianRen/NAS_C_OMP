/* Policy extracted from otter branch commit
 * 42e091c152420c3c9ce1e8fadcb43555507dbeb7, otter/otter_tuner.cpp.
 * Newton/golden search, warmups, thresholds and placement ties are preserved.
 * Topology discovery, environment parsing, OpenMP and timing live in runtime. */
#include "otter.h"

#include <cassert>
#include <math.h>
#include <new>
#include <stdio.h>
#include <vector>

#define OTTER_GOLDEN_RATIO_CONJUGATE 0.3819660112501051

typedef enum {
  OTTER_WARMUP_FIRST,
  OTTER_WARMUP_SECOND,
  OTTER_SAMPLE_FULL,
  OTTER_SAMPLE_HALF,
  OTTER_SAMPLE_THREE_QUARTERS,
  OTTER_GOLDEN_SEARCH,
  OTTER_THREAD_TUNING_DONE,
  OTTER_CONTIGUOUS_WARMUP,
  OTTER_CONTIGUOUS_MEASURE,
  OTTER_SCATTER_WARMUP,
  OTTER_SCATTER_MEASURE,
  OTTER_TUNING_DONE
} otter_state;

typedef enum {
  OTTER_CONTIGUOUS,
  OTTER_SCATTER
} otter_placement;

typedef struct {
  double metric;
  unsigned char valid;
} otter_sample;

struct otter_search_state {
  otter_state state = OTTER_WARMUP_FIRST;
  otter_placement placement = OTTER_CONTIGUOUS;
  otter_placement best_placement = OTTER_CONTIGUOUS;
  int max_threads;
  int current_threads;
  int best_threads;
  int half_threads;
  int three_quarter_threads;
  double threshold_fraction;
  int golden_distance;
  int golden_low = 0;
  int golden_high = 0;
  std::vector<otter_sample> samples;
  double contiguous_metric = 0;
  double scatter_metric = 0;
  bool placement_supported;
  int verbose = 0;
  int step = 0;
  bool stable_configuration_logged = false;

  // observe advances the next choice without changing the last selected cfg.
  hams_binding_cfg cfg{};
  otter_placement selected_placement = OTTER_CONTIGUOUS;
};

struct otter {
  std::vector<int> cpus;
  otter_search_state search;
};

static void otter_set_state(otter_search_state *tuner, otter_state state);

/* 返回搜索状态的可读名称，用于日志和状态查询。 */
static const char *otter_state_name(otter_state state)
{
  switch (state) {
    case OTTER_WARMUP_FIRST: return "WARMUP_FIRST";
    case OTTER_WARMUP_SECOND: return "WARMUP_SECOND";
    case OTTER_SAMPLE_FULL: return "SAMPLE_FULL";
    case OTTER_SAMPLE_HALF: return "SAMPLE_HALF";
    case OTTER_SAMPLE_THREE_QUARTERS: return "SAMPLE_3QUARTER";
    case OTTER_GOLDEN_SEARCH: return "GOLDEN_SEARCH";
    case OTTER_THREAD_TUNING_DONE: return "THREAD_TUNING_DONE";
    case OTTER_CONTIGUOUS_WARMUP: return "CONTIGUOUS_WARMUP";
    case OTTER_CONTIGUOUS_MEASURE: return "CONTIGUOUS_MEASURE";
    case OTTER_SCATTER_WARMUP: return "SCATTER_WARMUP";
    case OTTER_SCATTER_MEASURE: return "SCATTER_MEASURE";
    case OTTER_TUNING_DONE: return "TUNING_DONE";
  }
  return "UNKNOWN";
}

/* 返回绑定策略的可读名称。 */
static const char *otter_placement_name(otter_placement placement)
{
  return placement == OTTER_CONTIGUOUS ? "CONTIGUOUS" : "SCATTER";
}

/* 绑定不可用时显示 UNCONTROLLED，否则显示所选策略。 */
static const char *otter_effective_placement_name(const otter_search_state *tuner,
                                                   otter_placement placement)
{
  return tuner->placement_supported ? otter_placement_name(placement)
                                    : "UNCONTROLLED";
}

/* 保存某个合法线程数的测量值并标记为已采样。 */
static void otter_record_thread_metric(otter_search_state *tuner, int threads,
                                       double metric)
{
  if (threads < 1 || threads > tuner->max_threads) return;
  tuner->samples[threads].metric = metric;
  tuner->samples[threads].valid = 1;
}

/* 选取实测指标最小的线程数，相同指标优先使用更少线程。 */
static int otter_best_measured_threads(const otter_search_state *tuner)
{
  int best_threads = tuner->max_threads;
  double best_metric = HUGE_VAL;
  int threads;

  for (threads = 1; threads <= tuner->max_threads; threads++) {
    if (!tuner->samples[threads].valid) continue;
    if (tuner->samples[threads].metric < best_metric) {
      best_metric = tuner->samples[threads].metric;
      best_threads = threads;
    }
  }
  return best_threads;
}

/* 比较满线程和四分之三线程的指标，判断是否达到性能饱和阈值。 */
static int otter_performance_saturates(const otter_search_state *tuner)
{
  double full = tuner->samples[tuner->max_threads].metric;
  double reduced = tuner->samples[tuner->three_quarter_threads].metric;
  double denominator = fmin(full, reduced);

  if (denominator <= 0.0) return 0;
  return fabs(full - reduced) / denominator <= tuner->threshold_fraction;
}

/* 用三个样本的二阶 Newton 插值估算给定线程数的指标。 */
static double otter_newton_value(double x, double x0, double y0,
                                 double x1, double y1, double x2, double y2)
{
  double first = (y1 - y0) / (x1 - x0);
  double second_left = (y2 - y1) / (x2 - x1);
  double second = (second_left - first) / (x2 - x0);

  return y0 + first * (x - x0) + second * (x - x0) * (x - x1);
}

/* 选取预测性能在最优值容差内的最小线程数，插值无效时回退到实测值。 */
static int otter_newton_best_threads(const otter_search_state *tuner)
{
  int x0 = tuner->half_threads;
  int x1 = tuner->three_quarter_threads;
  int x2 = tuner->max_threads;
  double y0 = tuner->samples[x0].metric;
  double y1 = tuner->samples[x1].metric;
  double y2 = tuner->samples[x2].metric;
  double best_value = HUGE_VAL;
  double limit;
  int threads;

  if (x0 == x1 || x1 == x2 || x0 == x2) {
    return otter_best_measured_threads(tuner);
  }

  for (threads = x0; threads <= x2; threads++) {
    double predicted = otter_newton_value((double)threads, (double)x0, y0,
                                          (double)x1, y1, (double)x2, y2);
    if (isfinite(predicted) && predicted > 0.0 && predicted < best_value) {
      best_value = predicted;
    }
  }
  if (!isfinite(best_value)) return otter_best_measured_threads(tuner);

  limit = best_value * (1.0 + tuner->threshold_fraction);
  for (threads = x0; threads <= x2; threads++) {
    double predicted = otter_newton_value((double)threads, (double)x0, y0,
                                          (double)x1, y1, (double)x2, y2);
    if (isfinite(predicted) && predicted > 0.0 && predicted <= limit) {
      return threads;
    }
  }
  return otter_best_measured_threads(tuner);
}

/* 记录搜索状态转换；此处由采样结束后的反馈 callback 调用。 */
static void otter_set_state(otter_search_state *tuner, otter_state state)
{
  if (tuner->verbose && tuner->state != state) {
    printf("Otter step=%d state=%s -> %s\n", tuner->step,
           otter_state_name(tuner->state), otter_state_name(state));
  }
  tuner->state = state;
}

/* 固定选中的线程数，进入绑定策略比较或直接结束搜索。 */
static void otter_finish_thread_tuning(otter_search_state *tuner, int best_threads)
{
  if (best_threads < 1) best_threads = 1;
  if (best_threads > tuner->max_threads) best_threads = tuner->max_threads;
  tuner->best_threads = best_threads;
  otter_set_state(tuner, OTTER_THREAD_TUNING_DONE);

  /* THREAD_TUNING_DONE is an explicit transition state, not a wasted run. */
  tuner->current_threads = tuner->best_threads;
  tuner->placement = OTTER_CONTIGUOUS;
  if (tuner->placement_supported) {
    otter_set_state(tuner, OTTER_CONTIGUOUS_WARMUP);
  } else {
    otter_set_state(tuner, OTTER_TUNING_DONE);
  }
}

/* 计算区间内两个互异的整数黄金分割点，区间过小时返回 0。 */
static int otter_golden_points(int low, int high, int *left, int *right)
{
  int span = high - low;

  if (span < 3) return 0;
  *left = low + (int)floor(OTTER_GOLDEN_RATIO_CONJUGATE * span);
  *right = high - (int)floor(OTTER_GOLDEN_RATIO_CONJUGATE * span);
  if (*left <= low) *left = low + 1;
  if (*right >= high) *right = high - 1;
  if (*left >= *right) return 0;
  return 1;
}

/* Returns one when another point must be measured, zero when search is done. */
static int otter_prepare_golden_sample(otter_search_state *tuner)
{
  while (tuner->golden_high - tuner->golden_low >
         tuner->golden_distance) {
    int left;
    int right;
    int span = tuner->golden_high - tuner->golden_low;

    if (span == 2) {
      int middle = tuner->golden_low + 1;
      if (!tuner->samples[middle].valid) {
        tuner->current_threads = middle;
        return 1;
      }
      break;
    }

    if (!otter_golden_points(tuner->golden_low, tuner->golden_high,
                             &left, &right)) {
      break;
    }
    if (!tuner->samples[left].valid) {
      tuner->current_threads = left;
      return 1;
    }
    if (!tuner->samples[right].valid) {
      tuner->current_threads = right;
      return 1;
    }
    if (tuner->samples[left].metric <= tuner->samples[right].metric) {
      tuner->golden_high = right;
    } else {
      tuner->golden_low = left;
    }
  }
  if (!tuner->samples[tuner->golden_low].valid) {
    tuner->current_threads = tuner->golden_low;
    return 1;
  }
  if (!tuner->samples[tuner->golden_high].valid) {
    tuner->current_threads = tuner->golden_high;
    return 1;
  }
  return 0;
}

/* 初始化完整线程区间，并准备首个尚未测量的黄金分割点。 */
static void otter_start_golden_search(otter_search_state *tuner)
{
  tuner->golden_low = 1;
  tuner->golden_high = tuner->max_threads;
  otter_set_state(tuner, OTTER_GOLDEN_SEARCH);
  if (!otter_prepare_golden_sample(tuner)) {
    otter_finish_thread_tuning(tuner, otter_best_measured_threads(tuner));
  }
}

/* 消费本轮指标并推进搜索状态，预热轮不参与最优配置选择。 */
static void otter_advance_state(otter_search_state *tuner, double metric)
{
  switch (tuner->state) {
    case OTTER_WARMUP_FIRST:
      tuner->current_threads = tuner->max_threads;
      otter_set_state(tuner, OTTER_WARMUP_SECOND);
      break;

    case OTTER_WARMUP_SECOND:
      tuner->current_threads = tuner->max_threads;
      otter_set_state(tuner, OTTER_SAMPLE_FULL);
      break;

    case OTTER_SAMPLE_FULL:
      otter_record_thread_metric(tuner, tuner->current_threads, metric);
      tuner->current_threads = tuner->half_threads;
      otter_set_state(tuner, OTTER_SAMPLE_HALF);
      break;

    case OTTER_SAMPLE_HALF:
      otter_record_thread_metric(tuner, tuner->current_threads, metric);
      tuner->current_threads = tuner->three_quarter_threads;
      otter_set_state(tuner, OTTER_SAMPLE_THREE_QUARTERS);
      break;

    case OTTER_SAMPLE_THREE_QUARTERS:
      otter_record_thread_metric(tuner, tuner->current_threads, metric);
      if (otter_performance_saturates(tuner)) {
        if (tuner->verbose) printf("Otter step=%d search=NEWTON\n", tuner->step);
        otter_finish_thread_tuning(tuner, otter_newton_best_threads(tuner));
      } else {
        if (tuner->verbose) printf("Otter step=%d search=GOLDEN\n", tuner->step);
        otter_start_golden_search(tuner);
      }
      break;

    case OTTER_GOLDEN_SEARCH:
      otter_record_thread_metric(tuner, tuner->current_threads, metric);
      if (!otter_prepare_golden_sample(tuner)) {
        otter_finish_thread_tuning(tuner,
                                   otter_best_measured_threads(tuner));
      }
      break;

    case OTTER_THREAD_TUNING_DONE:
      /* This state is consumed immediately by otter_finish_thread_tuning. */
      otter_finish_thread_tuning(tuner, tuner->best_threads);
      break;

    case OTTER_CONTIGUOUS_WARMUP:
      otter_set_state(tuner, OTTER_CONTIGUOUS_MEASURE);
      break;

    case OTTER_CONTIGUOUS_MEASURE:
      tuner->contiguous_metric = metric;
      tuner->placement = OTTER_SCATTER;
      otter_set_state(tuner, OTTER_SCATTER_WARMUP);
      break;

    case OTTER_SCATTER_WARMUP:
      otter_set_state(tuner, OTTER_SCATTER_MEASURE);
      break;

    case OTTER_SCATTER_MEASURE:
      tuner->scatter_metric = metric;
      tuner->best_placement =
          tuner->contiguous_metric < tuner->scatter_metric
              ? OTTER_CONTIGUOUS
              : OTTER_SCATTER;
      tuner->current_threads = tuner->best_threads;
      tuner->placement = tuner->best_placement;
      otter_set_state(tuner, OTTER_TUNING_DONE);
      break;

    case OTTER_TUNING_DONE:
      tuner->current_threads = tuner->best_threads;
      tuner->placement = tuner->best_placement;
      break;
  }
}

/* CPU mapping keeps the original branch's full-pool scatter spacing. */
static hams_binding_cfg otter_make_binding_cfg(const otter *tuner,
                                               const otter_search_state &state)
{
  hams_binding_cfg cfg = {};
  cfg.thread_number = state.current_threads;
  for (int thread = 0; thread < cfg.thread_number; ++thread) {
    int slot = thread;
    if (state.placement == OTTER_SCATTER && cfg.thread_number > 1) {
      slot = (int)llround((double)thread * (tuner->cpus.size() - 1) /
                         (cfg.thread_number - 1));
    }
    int cpu = tuner->cpus[slot];
    cfg.mask[cpu] = true;
    cfg.tid_to_cpu[thread] = cpu;
  }
  return cfg;
}

otter *otter_create(int max_threads, const int *cpus, int cpu_count,
                    const otter_options &options)
{
  assert(cpus && max_threads >= 1);
  assert(max_threads <= cpu_count && cpu_count <= HAMS_CPU_COUNT);
  assert(isfinite(options.threshold_fraction) &&
         options.threshold_fraction >= 0 && options.threshold_fraction <= 1);
  assert(options.golden_distance >= 0 && options.golden_distance <= max_threads);
  for (int slot = 0; slot < cpu_count; ++slot) {
    assert(cpus[slot] >= 0 && cpus[slot] < HAMS_CPU_COUNT);
    assert(slot == 0 || cpus[slot - 1] < cpus[slot]);
  }
  auto *tuner = new (std::nothrow) otter;
  assert(tuner);
  tuner->cpus.assign(cpus, cpus + cpu_count);
  auto &state = tuner->search;
  state.max_threads = max_threads;
  state.current_threads = state.best_threads = max_threads;
  state.half_threads = max_threads / 2;
  if (state.half_threads < 1) state.half_threads = 1;
  state.three_quarter_threads = 3 * max_threads / 4;
  if (state.three_quarter_threads < 1) state.three_quarter_threads = 1;
  state.threshold_fraction = options.threshold_fraction;
  state.golden_distance = options.golden_distance == 0
                              ? (max_threads + 7) / 8
                              : options.golden_distance;
  state.placement_supported = options.placement_supported;
  state.verbose = options.verbose;
  state.samples.resize(max_threads + 1);
  if (state.verbose) {
    printf("Otter search max_threads=%d threshold=%.1f%% golden_distance=%d unit: us\n",
           max_threads, state.threshold_fraction * 100.0, state.golden_distance);
    fflush(stdout);
  }
  return tuner;
}

const hams_binding_cfg *otter_select_cfg(otter *tuner, int step)
{
  assert(tuner);
  auto &state = tuner->search;
  state.step = step;
  if (state.cfg.thread_number != state.current_threads ||
      state.selected_placement != state.placement) {
    state.cfg = otter_make_binding_cfg(tuner, state);
    state.selected_placement = state.placement;
  }
  if (state.verbose &&
      (state.state != OTTER_TUNING_DONE || !state.stable_configuration_logged)) {
    printf("Otter step=%d select state=%s threads=%d placement=%s\n",
           step, otter_state_name(state.state), state.cfg.thread_number,
           otter_effective_placement_name(&state, state.selected_placement));
    if (state.state == OTTER_TUNING_DONE) state.stable_configuration_logged = true;
    fflush(stdout);
  }
  return &state.cfg;
}

void otter_observe(otter *tuner, double seconds)
{
  assert(tuner);
  auto &state = tuner->search;
  assert(state.cfg.thread_number > 0 && isfinite(seconds) && seconds >= 0);
  bool log_sample = state.verbose && state.state != OTTER_TUNING_DONE;
  if (log_sample) {
    int warmup = state.state == OTTER_WARMUP_FIRST ||
                 state.state == OTTER_WARMUP_SECOND ||
                 state.state == OTTER_CONTIGUOUS_WARMUP ||
                 state.state == OTTER_SCATTER_WARMUP;
    printf("Otter step=%d sample state=%s threads=%d placement=%s time_us=%.3f warmup=%d\n",
           state.step, otter_state_name(state.state), state.cfg.thread_number,
           otter_effective_placement_name(&state, state.selected_placement),
           seconds * 1.0e6, warmup);
  }
  otter_advance_state(&state, seconds);
  if (log_sample) fflush(stdout);
}

void otter_destroy(otter *tuner, bool report)
{
  if (!tuner) return;
  const auto &state = tuner->search;
  if (report && state.cfg.thread_number) {
    printf("Otter final threads=%d placement=%s state=%s\n", state.cfg.thread_number,
           otter_effective_placement_name(&state, state.selected_placement),
           otter_state_name(state.state));
  }
  delete tuner;
}
