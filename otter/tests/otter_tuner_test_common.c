#define _GNU_SOURCE

#include "otter_tuner_test_common.h"

#include <errno.h>
#include <math.h>
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

static otter_test_begin_kind classify_begin(
    const otter_tuner_status *applied, const otter_tuner_status *target)
{
  if (applied->placement != target->placement) {
    return OTTER_TEST_BEGIN_PLACEMENT_SWITCH;
  }
  if (applied->current_threads < target->current_threads) {
    return OTTER_TEST_BEGIN_THREAD_GROW;
  }
  if (applied->current_threads > target->current_threads) {
    return OTTER_TEST_BEGIN_THREAD_SHRINK;
  }
  return OTTER_TEST_BEGIN_UNCHANGED;
}

void otter_test_configure_environment(void)
{
  setenv("OTTER_ENABLED", "1", 1);
  setenv("OTTER_PIN_THREADS", "1", 1);
  setenv("OTTER_THRESHOLD_PERCENT", "10", 1);
  if (getenv("OTTER_VERBOSE") == NULL) setenv("OTTER_VERBOSE", "0", 0);
}

const char *otter_test_compiler_name(void)
{
#if defined(__clang__)
  return "Clang " __clang_version__;
#elif defined(__GNUC__)
  return "GCC " __VERSION__;
#else
  return "unknown";
#endif
}

const char *otter_test_openmp_runtime_name(void)
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
    if (strstr(line, "libgomp.so") != NULL) strcpy(runtime, "GNU libgomp");
  }
  fclose(maps);
  return runtime;
}

const char *otter_test_placement_name(otter_tuner_placement placement)
{
  if (placement == OTTER_TUNER_PLACEMENT_CONTIGUOUS) return "CONTIGUOUS";
  if (placement == OTTER_TUNER_PLACEMENT_SCATTER) return "SCATTER";
  return "UNCONTROLLED";
}

const char *otter_test_begin_kind_name(otter_test_begin_kind kind)
{
  switch (kind) {
    case OTTER_TEST_BEGIN_UNCHANGED: return "unchanged-begin";
    case OTTER_TEST_BEGIN_THREAD_GROW: return "thread-grow";
    case OTTER_TEST_BEGIN_THREAD_SHRINK: return "thread-shrink";
    case OTTER_TEST_BEGIN_PLACEMENT_SWITCH: return "placement-switch";
    case OTTER_TEST_BEGIN_FINALIZE: return "finalize-begin";
    case OTTER_TEST_BEGIN_DONE_NOOP: return "done-noop";
  }
  return "unknown";
}

int otter_test_parse_positive_int(const char *text, int maximum)
{
  char *end = NULL;
  long value;

  errno = 0;
  value = strtol(text, &end, 10);
  if (errno != 0 || end == text || *end != '\0' || value < 1 ||
      value > maximum) {
    return -1;
  }
  return (int)value;
}

int otter_test_run_round(const char *benchmark_name, int done_noop_calls,
                         const otter_test_observer *observer, void *context,
                         otter_test_run_result *result)
{
  otter_tuner_status applied;
  otter_tuner_status status;
  otter_tuner *tuner;
  int iteration;
  double start;
  double elapsed;
  int success = 1;

  memset(result, 0, sizeof(*result));
  start = now_seconds();
  tuner = otter_tuner_create(benchmark_name);
  elapsed = now_seconds() - start;
  if (tuner == NULL || !otter_tuner_get_status(tuner, &applied)) {
    fprintf(stderr, "FAIL: tuner creation failed\n");
    otter_tuner_destroy(tuner);
    return 0;
  }
  if (observer != NULL && observer->created != NULL) {
    observer->created(&applied, elapsed, context);
  }

  result->max_threads = applied.max_threads;
  result->candidate_cpus = applied.candidate_cpus;
  if (!applied.enabled) {
    fprintf(stderr, "FAIL: tuner is disabled\n");
    success = 0;
    goto destroy;
  }
  if (applied.max_threads < 4) {
    fprintf(stderr, "FAIL: need at least 4 usable physical cores; "
                    "detected T_max=%d\n", applied.max_threads);
    success = 0;
    goto destroy;
  }
  if (!applied.placement_supported) {
    fprintf(stderr, "FAIL: placement unavailable; unset OMP_PLACES/"
                    "KMP_AFFINITY/GOMP_CPU_AFFINITY and set "
                    "OMP_PROC_BIND=false\n");
    success = 0;
    goto destroy;
  }

  for (iteration = 0; iteration < 64; iteration++) {
    otter_test_begin_kind kind;
    double iteration_start;

    if (!otter_tuner_get_status(tuner, &status)) {
      fprintf(stderr, "FAIL: cannot read tuner status\n");
      success = 0;
      break;
    }
    if (status.tuning_done) {
      int probe;
      start = now_seconds();
      otter_tuner_begin_iteration(tuner, iteration);
      elapsed = now_seconds() - start;
      if (!otter_tuner_get_status(tuner, &status)) {
        fprintf(stderr, "FAIL: cannot read final tuner status\n");
        success = 0;
        break;
      }
      if (observer != NULL && observer->began != NULL) {
        observer->began(tuner, &status, OTTER_TEST_BEGIN_FINALIZE,
                        elapsed, iteration, context);
      }
      for (probe = 0; probe < done_noop_calls; probe++) {
        start = now_seconds();
        otter_tuner_begin_iteration(tuner, iteration + probe + 1);
        elapsed = now_seconds() - start;
        if (observer != NULL && observer->began != NULL) {
          observer->began(tuner, &status, OTTER_TEST_BEGIN_DONE_NOOP,
                          elapsed, iteration + probe + 1,
                          context);
        }
      }
      result->completed = 1;
      result->final_threads = status.current_threads;
      result->final_placement = status.placement;
      break;
    }

    kind = classify_begin(&applied, &status);
    start = now_seconds();
    otter_tuner_begin_iteration(tuner, iteration);
    elapsed = now_seconds() - start;
    iteration_start = now_seconds();
    if (!otter_tuner_get_status(tuner, &status)) {
      fprintf(stderr, "FAIL: cannot read active tuner status\n");
      success = 0;
      break;
    }
    if (observer != NULL && observer->began != NULL) {
      observer->began(tuner, &status, kind, elapsed, iteration, context);
    }
    wait_until(iteration_start + state_duration(&status));
    otter_tuner_end_iteration(tuner);
    applied = status;
  }

  if (iteration == 64) {
    fprintf(stderr, "FAIL: state machine did not finish within 64 iterations\n");
    success = 0;
  }
  if (!result->completed) success = 0;

destroy:
  start = now_seconds();
  otter_tuner_destroy(tuner);
  if (observer != NULL && observer->destroyed != NULL) {
    observer->destroyed(now_seconds() - start, context);
  }
  return success;
}
