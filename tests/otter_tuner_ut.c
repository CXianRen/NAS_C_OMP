#define _GNU_SOURCE

#include "otter_tuner.h"

#include <errno.h>
#include <math.h>
#include <omp.h>
#include <sched.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum timing_kind {
  TIMING_CREATE,
  TIMING_UNCHANGED_BEGIN,
  TIMING_THREAD_GROW,
  TIMING_THREAD_SHRINK,
  TIMING_PLACEMENT_SWITCH,
  TIMING_FINALIZE_BEGIN,
  TIMING_DONE_NOOP,
  TIMING_DESTROY,
  TIMING_COUNT
};

typedef struct {
  const char *name;
  double *values;
  int count;
  int capacity;
} timing_samples;

typedef struct {
  int failures;
  int max_threads;
  int candidate_cpus;
  int final_threads;
  int placement_slots_changed;
} test_result;

static double now_seconds(void)
{
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (double)now.tv_sec + 1.0e-9 * (double)now.tv_nsec;
}

static void wait_until(double deadline)
{
  struct timespec target;
  int result;

  target.tv_sec = (time_t)deadline;
  target.tv_nsec = (long)((deadline - (double)target.tv_sec) * 1.0e9);
  do {
    result = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &target, NULL);
  } while (result == EINTR);
}

static void report_failure(test_result *result, const char *format, ...)
{
  va_list arguments;

  result->failures++;
  if (result->failures > 12) return;
  fprintf(stderr, "FAIL: ");
  va_start(arguments, format);
  vfprintf(stderr, format, arguments);
  va_end(arguments);
  fputc('\n', stderr);
}

static void add_timing(timing_samples *samples, double seconds)
{
  if (samples->count == samples->capacity) {
    int new_capacity = samples->capacity == 0 ? 64 : 2 * samples->capacity;
    double *new_values = realloc(samples->values,
                                 (size_t)new_capacity * sizeof(*new_values));
    if (new_values == NULL) {
      fprintf(stderr, "out of memory while recording timings\n");
      exit(2);
    }
    samples->values = new_values;
    samples->capacity = new_capacity;
  }
  samples->values[samples->count++] = seconds * 1.0e6;
}

static int compare_double(const void *left, const void *right)
{
  double a = *(const double *)left;
  double b = *(const double *)right;
  return (a > b) - (a < b);
}

static void print_timings(timing_samples timings[TIMING_COUNT])
{
  int kind;

  puts("\nconfiguration overhead (unit: us; begin excludes workload):");
  printf("%-20s %8s %10s %10s %10s %10s %10s\n",
         "operation", "samples", "min_us", "p50_us", "p95_us", "mean_us",
         "max_us");
  for (kind = 0; kind < TIMING_COUNT; kind++) {
    timing_samples *samples = &timings[kind];
    double sum = 0.0;
    int p50;
    int p95;
    int sample;

    if (samples->count == 0) continue;
    qsort(samples->values, (size_t)samples->count,
          sizeof(*samples->values), compare_double);
    for (sample = 0; sample < samples->count; sample++) {
      sum += samples->values[sample];
    }
    p50 = (samples->count - 1) / 2;
    p95 = (int)ceil(0.95 * (double)samples->count) - 1;
    printf("%-20s %8d %10.2f %10.2f %10.2f %10.2f %10.2f\n",
           samples->name, samples->count, samples->values[0],
           samples->values[p50], samples->values[p95],
           sum / (double)samples->count,
           samples->values[samples->count - 1]);
  }
}

static const char *openmp_runtime_name(void)
{
  static char runtime[32] = "unknown";
  FILE *maps = fopen("/proc/self/maps", "r");
  char line[1024];

  if (maps == NULL) return runtime;
  while (fgets(line, sizeof(line), maps) != NULL) {
    if (strstr(line, "libomp.so") != NULL ||
        strstr(line, "libiomp5.so") != NULL) {
      strcpy(runtime, "LLVM libomp");
      break;
    }
    if (strstr(line, "libgomp.so") != NULL) {
      strcpy(runtime, "GNU libgomp");
    }
  }
  fclose(maps);
  return runtime;
}

static const char *compiler_name(void)
{
#if defined(__clang__)
  return "Clang " __clang_version__;
#elif defined(__GNUC__)
  return "GCC " __VERSION__;
#else
  return "unknown";
#endif
}

static double state_duration(const otter_tuner_status *status)
{
  if (strcmp(status->state_name, "SAMPLE_FULL") == 0 ||
      strcmp(status->state_name, "SAMPLE_3QUARTER") == 0) {
    return 0.030;
  }
  if (strcmp(status->state_name, "SAMPLE_HALF") == 0) return 0.003;
  if (strcmp(status->state_name, "CONTIGUOUS_MEASURE") == 0) return 0.010;
  if (strcmp(status->state_name, "SCATTER_MEASURE") == 0) return 0.003;
  if (strcmp(status->state_name, "GOLDEN_SEARCH") == 0) {
    int half = status->max_threads / 2;
    return 0.003 + 0.0002 * fabs((double)(status->current_threads - half));
  }
  return 0.004;
}

static int is_placement_state(const char *state, const char *placement)
{
  return strncmp(state, placement, strlen(placement)) == 0;
}

static const char *placement_name(otter_tuner_placement placement)
{
  if (placement == OTTER_TUNER_PLACEMENT_CONTIGUOUS) return "CONTIGUOUS";
  if (placement == OTTER_TUNER_PLACEMENT_SCATTER) return "SCATTER";
  return "UNCONTROLLED";
}

/* Verify the affinity seen by the next real OpenMP region, not the apply team. */
static int probe_team(otter_tuner *tuner, const otter_tuner_status *status,
                      int *observed_cpus, test_result *result)
{
  int requested = status->current_threads;
  int actual_threads = 0;
  int *target_cpus = calloc((size_t)requested, sizeof(*target_cpus));
  int *mask_counts = calloc((size_t)requested, sizeof(*mask_counts));
  int *affinity_errors = calloc((size_t)requested, sizeof(*affinity_errors));
  int thread;

  if (target_cpus == NULL || mask_counts == NULL || affinity_errors == NULL) {
    fprintf(stderr, "out of memory while probing team affinity\n");
    exit(2);
  }
  for (thread = 0; thread < requested; thread++) {
    target_cpus[thread] = otter_tuner_get_target_cpu(tuner, thread);
    observed_cpus[thread] = -1;
  }

  #pragma omp parallel num_threads(requested) shared(actual_threads)
  {
    int tid = omp_get_thread_num();
    cpu_set_t affinity;

    #pragma omp single
    actual_threads = omp_get_num_threads();
    if (sched_getaffinity(0, sizeof(affinity), &affinity) != 0) {
      affinity_errors[tid] = errno;
    } else {
      mask_counts[tid] = CPU_COUNT(&affinity);
      observed_cpus[tid] = sched_getcpu();
      if (target_cpus[tid] >= 0 &&
          !CPU_ISSET(target_cpus[tid], &affinity)) {
        mask_counts[tid] = -mask_counts[tid];
      }
    }
  }

  if (actual_threads != requested) {
    report_failure(result, "%s requested %d threads but kernel observed %d",
                   status->state_name, requested, actual_threads);
  }
  for (thread = 0; thread < actual_threads; thread++) {
    int other;
    if (affinity_errors[thread] != 0) {
      report_failure(result, "%s thread %d: sched_getaffinity: %s",
                     status->state_name, thread,
                     strerror(affinity_errors[thread]));
    } else if (mask_counts[thread] != 1) {
      report_failure(result,
                     "%s thread %d: expected singleton CPU %d, mask_count=%d",
                     status->state_name, thread, target_cpus[thread],
                     mask_counts[thread]);
    } else if (observed_cpus[thread] != target_cpus[thread]) {
      report_failure(result, "%s thread %d: target CPU %d, observed CPU %d",
                     status->state_name, thread, target_cpus[thread],
                     observed_cpus[thread]);
    }
    for (other = 0; other < thread; other++) {
      if (observed_cpus[thread] == observed_cpus[other]) {
        report_failure(result, "%s threads %d and %d share CPU %d",
                       status->state_name, other, thread,
                       observed_cpus[thread]);
      }
    }
  }

  free(affinity_errors);
  free(mask_counts);
  free(target_cpus);
  return actual_threads;
}

static enum timing_kind classify_begin(const otter_tuner_status *applied,
                                        const otter_tuner_status *target)
{
  if (applied->placement != target->placement) return TIMING_PLACEMENT_SWITCH;
  if (applied->current_threads < target->current_threads) {
    return TIMING_THREAD_GROW;
  }
  if (applied->current_threads > target->current_threads) {
    return TIMING_THREAD_SHRINK;
  }
  return TIMING_UNCHANGED_BEGIN;
}

static void compare_placements(const int *contiguous, const int *scatter,
                               int threads, int candidate_cpus,
                               test_result *result)
{
  int changed = 0;
  int thread;

  for (thread = 0; thread < threads; thread++) {
    if (contiguous[thread] != scatter[thread]) changed++;
  }
  result->placement_slots_changed = changed;
  if (threads > 1 && candidate_cpus > threads && changed == 0) {
    report_failure(result,
                   "CONTIGUOUS and SCATTER used the same CPUs despite "
                   "%d candidates for %d threads",
                   candidate_cpus, threads);
  }
}

static void verify_restored_affinity(const cpu_set_t *original,
                                     test_result *result)
{
  int restore_failed = 0;

  #pragma omp parallel reduction(|:restore_failed)
  {
    cpu_set_t affinity;
    if (sched_getaffinity(0, sizeof(affinity), &affinity) != 0 ||
        !CPU_EQUAL(&affinity, original)) {
      restore_failed = 1;
    }
  }
  if (restore_failed) {
    report_failure(result,
                   "destroy did not restore the original worker affinity");
  }
}

static test_result run_round(int round, timing_samples timings[TIMING_COUNT])
{
  test_result result = {0};
  otter_tuner_status applied;
  otter_tuner_status status;
  otter_tuner *tuner;
  int *observed;
  int *contiguous;
  int *scatter;
  int contiguous_threads = 0;
  int scatter_threads = 0;
  int saw_thread_switch = 0;
  int saw_placement_switch = 0;
  int iteration;
  double start;
  cpu_set_t original_affinity;

  if (sched_getaffinity(0, sizeof(original_affinity),
                        &original_affinity) != 0) {
    report_failure(&result, "cannot capture original process affinity: %s",
                   strerror(errno));
    return result;
  }

  start = now_seconds();
  tuner = otter_tuner_create("otter-ut");
  add_timing(&timings[TIMING_CREATE], now_seconds() - start);
  if (tuner == NULL || !otter_tuner_get_status(tuner, &applied)) {
    report_failure(&result, "tuner creation failed");
    otter_tuner_destroy(tuner);
    return result;
  }
  result.max_threads = applied.max_threads;
  result.candidate_cpus = applied.candidate_cpus;
  if (!applied.enabled) {
    report_failure(&result, "tuner is disabled");
  }
  if (applied.max_threads < 4) {
    report_failure(&result,
                   "need at least 4 usable physical cores; detected T_max=%d",
                   applied.max_threads);
  }
  if (!applied.placement_supported) {
    report_failure(&result,
                   "placement unavailable; unset OMP_PLACES/KMP_AFFINITY/"
                   "GOMP_CPU_AFFINITY and set OMP_PROC_BIND=false");
  }
  if (result.failures != 0) {
    otter_tuner_destroy(tuner);
    return result;
  }

  if (round == 0) {
    puts("\ncorrectness configuration trace (round 1):");
  }

  observed = malloc((size_t)applied.max_threads * sizeof(*observed));
  contiguous = malloc((size_t)applied.max_threads * sizeof(*contiguous));
  scatter = malloc((size_t)applied.max_threads * sizeof(*scatter));
  if (observed == NULL || contiguous == NULL || scatter == NULL) {
    fprintf(stderr, "out of memory while preparing affinity checks\n");
    exit(2);
  }

  for (iteration = 0; iteration < 64; iteration++) {
    enum timing_kind kind;
    double iteration_start;

    if (!otter_tuner_get_status(tuner, &status)) {
      report_failure(&result, "cannot read tuner status");
      break;
    }
    if (status.tuning_done) {
      int failures_before = result.failures;
      int actual_threads;
      int probe;
      start = now_seconds();
      otter_tuner_begin_iteration(tuner, iteration);
      add_timing(&timings[TIMING_FINALIZE_BEGIN], now_seconds() - start);
      actual_threads = probe_team(tuner, &status, observed, &result);
      if (round == 0) {
        printf("enter cfg: T=%d, P=%s  state=%s actual_T=%d check=%s "
               "change=finalize-begin\n",
               status.current_threads, placement_name(status.placement),
               status.state_name, actual_threads,
               result.failures == failures_before ? "PASS" : "FAIL");
      }
      for (probe = 0; probe < 32; probe++) {
        start = now_seconds();
        otter_tuner_begin_iteration(tuner, iteration + probe + 1);
        add_timing(&timings[TIMING_DONE_NOOP], now_seconds() - start);
      }
      break;
    }

    kind = classify_begin(&applied, &status);
    if (kind == TIMING_THREAD_GROW || kind == TIMING_THREAD_SHRINK) {
      saw_thread_switch = 1;
    }
    if (kind == TIMING_PLACEMENT_SWITCH) saw_placement_switch = 1;

    start = now_seconds();
    otter_tuner_begin_iteration(tuner, iteration);
    add_timing(&timings[kind], now_seconds() - start);
    iteration_start = now_seconds();
    if (!otter_tuner_get_status(tuner, &status)) {
      report_failure(&result, "cannot read active configuration");
      break;
    }
    {
      int failures_before = result.failures;
      int actual_threads = probe_team(tuner, &status, observed, &result);
      if (round == 0) {
        printf("enter cfg: T=%d, P=%s  state=%s actual_T=%d check=%s "
               "change=%s\n",
               status.current_threads, placement_name(status.placement),
               status.state_name, actual_threads,
               result.failures == failures_before ? "PASS" : "FAIL",
               timings[kind].name);
      }
    }

    if (is_placement_state(status.state_name, "CONTIGUOUS") &&
        contiguous_threads == 0) {
      contiguous_threads = status.current_threads;
      memcpy(contiguous, observed,
             (size_t)contiguous_threads * sizeof(*contiguous));
    }
    if (is_placement_state(status.state_name, "SCATTER") &&
        scatter_threads == 0) {
      scatter_threads = status.current_threads;
      memcpy(scatter, observed,
             (size_t)scatter_threads * sizeof(*scatter));
    }

    wait_until(iteration_start + state_duration(&status));
    otter_tuner_end_iteration(tuner);
    applied = status;
  }

  if (iteration == 64) {
    report_failure(&result, "state machine did not finish within 64 iterations");
  }
  if (!otter_tuner_get_status(tuner, &status) || !status.tuning_done) {
    report_failure(&result, "state machine did not reach TUNING_DONE");
  } else {
    result.final_threads = status.current_threads;
    if (status.current_threads != status.max_threads / 2) {
      report_failure(&result, "expected deterministic T_best=%d, got %d",
                     status.max_threads / 2, status.current_threads);
    }
    if (status.placement != OTTER_TUNER_PLACEMENT_SCATTER) {
      report_failure(&result, "expected deterministic P_best=SCATTER");
    }
  }
  if (!saw_thread_switch) report_failure(&result, "no thread-count switch seen");
  if (!saw_placement_switch) report_failure(&result, "no placement switch seen");
  if (contiguous_threads == 0 || scatter_threads == 0) {
    report_failure(&result, "placement comparison states were not observed");
  } else if (contiguous_threads != scatter_threads) {
    report_failure(&result,
                   "placement comparison changed team size (%d versus %d)",
                   contiguous_threads, scatter_threads);
  } else {
    compare_placements(contiguous, scatter, contiguous_threads,
                       result.candidate_cpus, &result);
  }

  free(scatter);
  free(contiguous);
  free(observed);
  start = now_seconds();
  otter_tuner_destroy(tuner);
  add_timing(&timings[TIMING_DESTROY], now_seconds() - start);
  {
    int failures_before = result.failures;
    verify_restored_affinity(&original_affinity, &result);
    if (round == 0) {
      printf("restore worker affinity: %s\n",
             result.failures == failures_before ? "PASS" : "FAIL");
    }
  }
  return result;
}

static int parse_rounds(const char *text)
{
  char *end = NULL;
  long value;

  errno = 0;
  value = strtol(text, &end, 10);
  if (errno != 0 || end == text || *end != '\0' || value < 1 ||
      value > 10000) {
    return -1;
  }
  return (int)value;
}

int main(int argc, char **argv)
{
  timing_samples timings[TIMING_COUNT] = {
      {.name = "create+initial-bind"},
      {.name = "unchanged-begin"},
      {.name = "thread-grow"},
      {.name = "thread-shrink"},
      {.name = "placement-switch"},
      {.name = "finalize-begin"},
      {.name = "done-noop"},
      {.name = "destroy+restore"}};
  int rounds = 20;
  int require_libomp = 0;
  int failures = 0;
  int round;
  test_result summary = {0};
  const char *runtime;

  for (round = 1; round < argc; round++) {
    if (strcmp(argv[round], "--rounds") == 0 && round + 1 < argc) {
      rounds = parse_rounds(argv[++round]);
      if (rounds < 0) {
        fprintf(stderr, "--rounds must be between 1 and 10000\n");
        return 2;
      }
    } else if (strcmp(argv[round], "--require-libomp") == 0) {
      require_libomp = 1;
    } else {
      fprintf(stderr,
              "usage: %s [--rounds N] [--require-libomp]\n", argv[0]);
      return 2;
    }
  }

  setenv("OTTER_ENABLED", "1", 1);
  setenv("OTTER_PIN_THREADS", "1", 1);
  setenv("OTTER_THRESHOLD_PERCENT", "10", 1);
  if (getenv("OTTER_VERBOSE") == NULL) setenv("OTTER_VERBOSE", "0", 0);

  (void)omp_get_max_threads();
  runtime = openmp_runtime_name();
  printf("compiler=%s\nOpenMP=%d runtime=%s proc_bind=%d rounds=%d\n",
         compiler_name(), _OPENMP, runtime, (int)omp_get_proc_bind(), rounds);
  if (require_libomp && strcmp(runtime, "LLVM libomp") != 0) {
    fprintf(stderr, "FAIL: --require-libomp requested, found %s\n", runtime);
    return 1;
  }

  for (round = 0; round < rounds; round++) {
    test_result result = run_round(round, timings);
    failures += result.failures;
    if (round == 0) summary = result;
    if (result.failures != 0) break;
    if (result.max_threads != summary.max_threads ||
        result.candidate_cpus != summary.candidate_cpus ||
        result.final_threads != summary.final_threads) {
      fprintf(stderr, "FAIL: topology or selected configuration changed "
                      "between rounds\n");
      failures++;
      break;
    }
  }

  if (failures == 0) {
    printf("correctness=PASS T_max=%d candidate_cpus=%d T_best=%d "
           "P_best=SCATTER changed_slots=%d/%d\n",
           summary.max_threads, summary.candidate_cpus,
           summary.final_threads, summary.placement_slots_changed,
           summary.final_threads);
  } else {
    printf("correctness=FAIL failures=%d\n", failures);
  }
  print_timings(timings);

  for (round = 0; round < TIMING_COUNT; round++) free(timings[round].values);
  return failures == 0 ? 0 : 1;
}
