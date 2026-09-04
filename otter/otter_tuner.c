#define _GNU_SOURCE

#include "otter_tuner.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include <hwloc.h>
#include <omp.h>
#include <sched.h>

#define OTTER_DEFAULT_THRESHOLD_PERCENT 10.0
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

struct otter_tuner {
  const char *benchmark_name;
  int enabled;
  int verbose;
  int iteration_active;
  int active_iteration;
  int runtime_mismatch;
  int final_configuration_applied;

  otter_state state;
  otter_placement placement;
  otter_placement best_placement;

  int original_dynamic;
  int original_threads;
  int max_threads;
  int current_threads;
  int best_threads;
  int half_threads;
  int three_quarter_threads;

  double threshold_fraction;
  int golden_distance;
  int golden_low;
  int golden_high;

  otter_sample *samples;
  double contiguous_metric;
  double scatter_metric;

  struct timespec iteration_start;

  int placement_supported;
  int placement_failed;
  int affinity_modified;
  int *core_cpus;
  int core_count;
  cpu_set_t original_affinity;
};

static void otter_set_state(otter_tuner *tuner, otter_state state);

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

static const char *otter_placement_name(otter_placement placement)
{
  return placement == OTTER_CONTIGUOUS ? "CONTIGUOUS" : "SCATTER";
}

static const char *otter_effective_placement_name(const otter_tuner *tuner,
                                                   otter_placement placement)
{
  return tuner->placement_supported ? otter_placement_name(placement)
                                    : "UNCONTROLLED";
}

static int otter_env_is_false(const char *name)
{
  const char *value = getenv(name);

  if (value == NULL || value[0] == '\0') return 0;
  return strcmp(value, "0") == 0 || strcasecmp(value, "false") == 0 ||
         strcasecmp(value, "no") == 0 || strcasecmp(value, "off") == 0;
}

static int otter_env_int(const char *name, int fallback, int minimum,
                         int maximum)
{
  const char *value = getenv(name);
  char *end = NULL;
  long parsed;

  if (value == NULL || value[0] == '\0') return fallback;
  errno = 0;
  parsed = strtol(value, &end, 10);
  if (errno != 0 || end == value || *end != '\0') return fallback;
  if (parsed < minimum) return minimum;
  if (parsed > maximum) return maximum;
  return (int)parsed;
}

static double otter_env_double(const char *name, double fallback,
                               double minimum, double maximum)
{
  const char *value = getenv(name);
  char *end = NULL;
  double parsed;

  if (value == NULL || value[0] == '\0') return fallback;
  errno = 0;
  parsed = strtod(value, &end);
  if (errno != 0 || end == value || *end != '\0' || !isfinite(parsed)) {
    return fallback;
  }
  if (parsed < minimum) return minimum;
  if (parsed > maximum) return maximum;
  return parsed;
}

static void otter_discover_cores(otter_tuner *tuner)
{
  hwloc_topology_t topology;
  hwloc_obj_t object = NULL;
  hwloc_obj_type_t type;
  int object_count;

  if (sched_getaffinity(0, sizeof(tuner->original_affinity),
                        &tuner->original_affinity) != 0) {
    return;
  }
  if (hwloc_topology_init(&topology) != 0) return;
  if (hwloc_topology_load(topology) != 0) goto out;

  type = otter_env_is_false("OTTER_PHYSICAL_CORES")
             ? HWLOC_OBJ_PU
             : HWLOC_OBJ_CORE;
  object_count = hwloc_get_nbobjs_by_type(topology, type);
  if (object_count <= 0) goto out;
  tuner->core_cpus = calloc((size_t)object_count,
                            sizeof(*tuner->core_cpus));
  if (tuner->core_cpus == NULL) goto out;

  while ((object = hwloc_get_next_obj_by_type(topology, type, object))) {
    int cpu;
    for (cpu = hwloc_bitmap_first(object->cpuset); cpu >= 0;
         cpu = hwloc_bitmap_next(object->cpuset, cpu)) {
      if (cpu < CPU_SETSIZE && CPU_ISSET(cpu, &tuner->original_affinity)) {
        tuner->core_cpus[tuner->core_count++] = cpu;
        break;
      }
    }
  }

out:
  hwloc_topology_destroy(topology);
}

static int otter_cpu_for_thread(const otter_tuner *tuner, int thread,
                                int thread_count,
                                otter_placement placement)
{
  int slot;

  if (placement == OTTER_CONTIGUOUS || thread_count <= 1) {
    slot = thread;
  } else {
    slot = (int)llround((double)thread * (double)(tuner->core_count - 1) /
                        (double)(thread_count - 1));
  }
  return tuner->core_cpus[slot];
}

static void otter_pin_thread(otter_tuner *tuner, int thread,
                             int thread_count, otter_placement placement,
                             int *pin_failed, int *pin_succeeded)
{
  cpu_set_t affinity;
  int cpu;

  if (!tuner->placement_supported) return;
  cpu = otter_cpu_for_thread(tuner, thread, thread_count, placement);
  CPU_ZERO(&affinity);
  CPU_SET(cpu, &affinity);
  if (sched_setaffinity(0, sizeof(affinity), &affinity) != 0) {
    *pin_failed = 1;
  } else {
    *pin_succeeded = 1;
  }
}

static void otter_release_team_affinity(otter_tuner *tuner, int thread_count)
{
  cpu_set_t original_affinity = tuner->original_affinity;
  #pragma omp parallel num_threads(thread_count) firstprivate(original_affinity)
  sched_setaffinity(0, sizeof(original_affinity), &original_affinity);
}

static int otter_apply_configuration(otter_tuner *tuner)
{
  int actual_threads = 1;
  int pin_failed = 0;
  int pin_succeeded = 0;
  int requested_threads = tuner->current_threads;
  otter_placement placement = tuner->placement;

  omp_set_dynamic(0);
  omp_set_num_threads(requested_threads);
  #pragma omp parallel num_threads(requested_threads) \
                       reduction(|:pin_failed,pin_succeeded) \
                       shared(actual_threads)
  {
    int thread = omp_get_thread_num();
    #pragma omp single
    actual_threads = omp_get_num_threads();
    otter_pin_thread(tuner, thread, actual_threads, placement,
                     &pin_failed, &pin_succeeded);
  }

  if (pin_succeeded) tuner->affinity_modified = 1;
  if (pin_failed) {
    fprintf(stderr,
            "[otter][%s] warning: sched_setaffinity failed; placement "
            "results may not represent CONTIGUOUS/SCATTER\n",
            tuner->benchmark_name);
    tuner->placement_supported = 0;
    tuner->placement_failed = 1;
    otter_release_team_affinity(tuner, actual_threads);
  }
  if (actual_threads != requested_threads && tuner->verbose > 0) {
    fprintf(stderr,
            "[otter][%s] warning: requested %d threads, runtime created %d\n",
            tuner->benchmark_name, requested_threads, actual_threads);
  }
  return actual_threads;
}

static double otter_elapsed_seconds(const struct timespec *start,
                                    const struct timespec *end)
{
  return (double)(end->tv_sec - start->tv_sec) +
         1.0e-9 * (double)(end->tv_nsec - start->tv_nsec);
}

static void otter_record_thread_metric(otter_tuner *tuner, int threads,
                                       double metric)
{
  if (threads < 1 || threads > tuner->max_threads) return;
  tuner->samples[threads].metric = metric;
  tuner->samples[threads].valid = 1;
}

static int otter_best_measured_threads(const otter_tuner *tuner)
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

static int otter_performance_saturates(const otter_tuner *tuner)
{
  double full = tuner->samples[tuner->max_threads].metric;
  double reduced = tuner->samples[tuner->three_quarter_threads].metric;
  double denominator = fmin(full, reduced);

  if (denominator <= 0.0) return 0;
  return fabs(full - reduced) / denominator <= tuner->threshold_fraction;
}

static double otter_newton_value(double x, double x0, double y0,
                                 double x1, double y1, double x2, double y2)
{
  double first = (y1 - y0) / (x1 - x0);
  double second_left = (y2 - y1) / (x2 - x1);
  double second = (second_left - first) / (x2 - x0);

  return y0 + first * (x - x0) + second * (x - x0) * (x - x1);
}

static int otter_newton_best_threads(const otter_tuner *tuner)
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

static void otter_set_state(otter_tuner *tuner, otter_state state)
{
  if (tuner->verbose > 1) {
    fprintf(stderr, "[otter][%s] state %s -> %s\n", tuner->benchmark_name,
            otter_state_name(tuner->state), otter_state_name(state));
  }
  tuner->state = state;
}

static void otter_finish_thread_tuning(otter_tuner *tuner, int best_threads)
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
static int otter_prepare_golden_sample(otter_tuner *tuner)
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

static void otter_start_golden_search(otter_tuner *tuner)
{
  tuner->golden_low = 1;
  tuner->golden_high = tuner->max_threads;
  otter_set_state(tuner, OTTER_GOLDEN_SEARCH);
  if (!otter_prepare_golden_sample(tuner)) {
    otter_finish_thread_tuning(tuner, otter_best_measured_threads(tuner));
  }
}

static void otter_advance_state(otter_tuner *tuner, double metric)
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
        otter_finish_thread_tuning(tuner, otter_newton_best_threads(tuner));
      } else {
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

otter_tuner *otter_tuner_create(const char *benchmark_name)
{
  otter_tuner *tuner = calloc(1, sizeof(*tuner));
  int pin_threads;
  int proc_bind_active;
  int requested_max;
  int actual_threads;

  if (tuner == NULL) return NULL;
  tuner->benchmark_name = benchmark_name != NULL ? benchmark_name : "NPB";
  tuner->enabled = !otter_env_is_false("OTTER_ENABLED");
  tuner->verbose = otter_env_int("OTTER_VERBOSE", 1, 0, 2);
  if (!tuner->enabled) {
    if (tuner->verbose > 0) {
      fprintf(stderr, "[otter][%s] disabled by OTTER_ENABLED\n",
              tuner->benchmark_name);
    }
    return tuner;
  }

  tuner->original_dynamic = omp_get_dynamic();
  tuner->original_threads = omp_get_max_threads();
  pin_threads = !otter_env_is_false("OTTER_PIN_THREADS");
  proc_bind_active = omp_get_proc_bind() != omp_proc_bind_false;
  tuner->threshold_fraction =
      otter_env_double("OTTER_THRESHOLD_PERCENT",
                       OTTER_DEFAULT_THRESHOLD_PERCENT, 0.0, 100.0) /
      100.0;

  otter_discover_cores(tuner);
  tuner->max_threads = tuner->core_count > 0
                           ? tuner->core_count
                           : tuner->original_threads;
  if (tuner->max_threads > tuner->original_threads) {
    tuner->max_threads = tuner->original_threads;
  }
  if (omp_get_thread_limit() > 0 &&
      tuner->max_threads > omp_get_thread_limit()) {
    tuner->max_threads = omp_get_thread_limit();
  }
  if (tuner->max_threads < 1) tuner->max_threads = 1;
  requested_max = otter_env_int("OTTER_MAX_THREADS", tuner->max_threads, 1,
                                tuner->max_threads);
  tuner->max_threads = requested_max;
  tuner->placement_supported = pin_threads &&
                               tuner->core_count >= tuner->max_threads;
  if (proc_bind_active) tuner->placement_supported = 0;
  tuner->half_threads = tuner->max_threads / 2;
  if (tuner->half_threads < 1) tuner->half_threads = 1;
  tuner->three_quarter_threads = 3 * tuner->max_threads / 4;
  if (tuner->three_quarter_threads < 1) {
    tuner->three_quarter_threads = 1;
  }
  tuner->golden_distance =
      otter_env_int("OTTER_GOLDEN_DISTANCE",
                    (tuner->max_threads + 7) / 8, 1, tuner->max_threads);
  tuner->current_threads = tuner->max_threads;
  tuner->best_threads = tuner->max_threads;
  tuner->placement = OTTER_CONTIGUOUS;
  tuner->best_placement = OTTER_CONTIGUOUS;
  tuner->state = OTTER_WARMUP_FIRST;

  tuner->samples = calloc((size_t)tuner->max_threads + 1,
                          sizeof(*tuner->samples));
  if (tuner->samples == NULL) {
    fprintf(stderr, "[otter][%s] warning: allocation failed; tuner disabled\n",
            tuner->benchmark_name);
    tuner->enabled = 0;
    return tuner;
  }

  actual_threads = otter_apply_configuration(tuner);
  tuner->runtime_mismatch = actual_threads != tuner->current_threads;

  if (tuner->verbose > 0) {
    fprintf(stderr,
            "[otter][%s] initialized: T_max=%d, cores=%d, placement=%s, "
            "metric=execution_time, threshold=%.1f%%, golden_distance=%d\n",
            tuner->benchmark_name, tuner->max_threads, tuner->core_count,
            otter_effective_placement_name(tuner, tuner->placement),
            tuner->threshold_fraction * 100.0, tuner->golden_distance);
  }
  if (tuner->verbose > 1 && tuner->core_count > 0) {
    int core;
    fprintf(stderr, "[otter][%s] core CPUs:", tuner->benchmark_name);
    for (core = 0; core < tuner->core_count; core++) {
      fprintf(stderr, " %d", tuner->core_cpus[core]);
    }
    fprintf(stderr, "\n");
  }
  if (pin_threads && proc_bind_active) {
    fprintf(stderr,
            "[otter][%s] warning: OMP_PROC_BIND is active; placement tuning "
            "is disabled\n",
            tuner->benchmark_name);
  }
  return tuner;
}

void otter_tuner_begin_iteration(otter_tuner *tuner, int iteration)
{
  int actual_threads;

  if (tuner == NULL || !tuner->enabled) return;
  if (tuner->runtime_mismatch || tuner->placement_failed) return;
  if (tuner->state == OTTER_TUNING_DONE) {
    if (!tuner->final_configuration_applied) {
      actual_threads = otter_apply_configuration(tuner);
      tuner->runtime_mismatch = actual_threads != tuner->current_threads;
      tuner->final_configuration_applied = !tuner->runtime_mismatch;
    }
    return;
  }
  if (tuner->iteration_active) {
    fprintf(stderr,
            "[otter][%s] warning: begin called before previous iteration "
            "ended; ignoring nested begin\n",
            tuner->benchmark_name);
    return;
  }

  actual_threads = otter_apply_configuration(tuner);
  if (tuner->placement_failed) return;
  if (actual_threads != tuner->current_threads) {
    tuner->runtime_mismatch = 1;
    return;
  }
  tuner->active_iteration = iteration;
  clock_gettime(CLOCK_MONOTONIC, &tuner->iteration_start);
  tuner->iteration_active = 1;
}

void otter_tuner_end_iteration(otter_tuner *tuner)
{
  struct timespec end_time;
  double elapsed;

  if (tuner == NULL || !tuner->enabled) return;
  if (tuner->runtime_mismatch || tuner->placement_failed) {
    tuner->iteration_active = 0;
    return;
  }
  if (tuner->state == OTTER_TUNING_DONE && !tuner->iteration_active) return;
  if (!tuner->iteration_active) {
    fprintf(stderr, "[otter][%s] warning: end called without begin\n",
            tuner->benchmark_name);
    return;
  }

  clock_gettime(CLOCK_MONOTONIC, &end_time);

  elapsed = otter_elapsed_seconds(&tuner->iteration_start, &end_time);

  if (tuner->verbose > 0) {
    fprintf(stderr,
            "[otter][%s] iter=%d state=%s threads=%d placement=%s "
            "execution_time=%.6e\n",
            tuner->benchmark_name, tuner->active_iteration,
            otter_state_name(tuner->state), tuner->current_threads,
            otter_effective_placement_name(tuner, tuner->placement),
            elapsed);
  }

  tuner->iteration_active = 0;
  otter_advance_state(tuner, elapsed);
}

int otter_tuner_get_status(const otter_tuner *tuner,
                           otter_tuner_status *status)
{
  if (tuner == NULL || status == NULL) return 0;

  status->enabled = tuner->enabled;
  status->tuning_done = tuner->state == OTTER_TUNING_DONE;
  status->current_threads = tuner->current_threads;
  status->max_threads = tuner->max_threads;
  status->candidate_cpus = tuner->core_count;
  status->placement_supported = tuner->placement_supported;
  status->placement = !tuner->placement_supported
                          ? OTTER_TUNER_PLACEMENT_UNCONTROLLED
                          : tuner->placement == OTTER_CONTIGUOUS
                                ? OTTER_TUNER_PLACEMENT_CONTIGUOUS
                                : OTTER_TUNER_PLACEMENT_SCATTER;
  status->state_name = otter_state_name(tuner->state);
  return 1;
}

int otter_tuner_get_target_cpu(const otter_tuner *tuner, int thread)
{
  if (tuner == NULL || !tuner->placement_supported || thread < 0 ||
      thread >= tuner->current_threads) {
    return -1;
  }
  return otter_cpu_for_thread(tuner, thread, tuner->current_threads,
                              tuner->placement);
}

static void otter_restore_runtime(otter_tuner *tuner)
{
  int restore_threads = tuner->original_threads;
  int restore_failed = 0;
  omp_set_dynamic(0);
  if (tuner->affinity_modified) {
    cpu_set_t original_affinity = tuner->original_affinity;
    #pragma omp parallel num_threads(restore_threads) \
                         firstprivate(original_affinity) \
                         reduction(|:restore_failed)
    {
      if (sched_setaffinity(0, sizeof(original_affinity),
                            &original_affinity) != 0) {
        restore_failed = 1;
      }
    }
  }
  omp_set_num_threads(restore_threads);
  omp_set_dynamic(tuner->original_dynamic);
  if (restore_failed) {
    fprintf(stderr,
            "[otter][%s] warning: failed to restore affinity for at least "
            "one OpenMP worker\n",
            tuner->benchmark_name);
  }
}

void otter_tuner_destroy(otter_tuner *tuner)
{
  if (tuner == NULL) return;
  if (tuner->enabled) {
    if (tuner->iteration_active) otter_tuner_end_iteration(tuner);
    if (tuner->verbose > 0) {
      if (tuner->placement_failed) {
        fprintf(stderr,
                "[otter][%s] stopped: per-worker affinity control failed\n",
                tuner->benchmark_name);
      } else if (tuner->runtime_mismatch) {
        fprintf(stderr,
                "[otter][%s] stopped: the OpenMP runtime could not create "
                "the requested team size\n",
                tuner->benchmark_name);
      } else if (tuner->state == OTTER_TUNING_DONE &&
                 tuner->placement_supported) {
        fprintf(stderr,
                "[otter][%s] complete: T_best=%d, P_best=%s, "
                "contiguous=%.6e, scatter=%.6e\n",
                tuner->benchmark_name, tuner->best_threads,
                otter_placement_name(tuner->best_placement),
                tuner->contiguous_metric, tuner->scatter_metric);
      } else if (tuner->state == OTTER_TUNING_DONE) {
        fprintf(stderr,
                "[otter][%s] thread tuning complete: T_best=%d, "
                "P_best=N/A (placement control unavailable)\n",
                tuner->benchmark_name, tuner->best_threads);
      } else {
        fprintf(stderr,
                "[otter][%s] stopped in state %s: benchmark ended before "
                "tuning completed\n",
                tuner->benchmark_name, otter_state_name(tuner->state));
      }
    }
    otter_restore_runtime(tuner);
  }

  free(tuner->samples);
  free(tuner->core_cpus);
  free(tuner);
}
