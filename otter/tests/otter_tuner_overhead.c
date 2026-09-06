#define _GNU_SOURCE

#include "otter_tuner_test_common.h"

#include <math.h>
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
  timing_samples timings[TIMING_COUNT];
} profiling_context;

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

static enum timing_kind timing_kind_for_begin(otter_test_begin_kind kind)
{
  switch (kind) {
    case OTTER_TEST_BEGIN_UNCHANGED: return TIMING_UNCHANGED_BEGIN;
    case OTTER_TEST_BEGIN_THREAD_GROW: return TIMING_THREAD_GROW;
    case OTTER_TEST_BEGIN_THREAD_SHRINK: return TIMING_THREAD_SHRINK;
    case OTTER_TEST_BEGIN_PLACEMENT_SWITCH: return TIMING_PLACEMENT_SWITCH;
    case OTTER_TEST_BEGIN_FINALIZE: return TIMING_FINALIZE_BEGIN;
    case OTTER_TEST_BEGIN_DONE_NOOP: return TIMING_DONE_NOOP;
  }
  return TIMING_UNCHANGED_BEGIN;
}

static void created(const otter_tuner_status *status, double seconds,
                    void *opaque)
{
  profiling_context *context = opaque;
  (void)status;
  add_timing(&context->timings[TIMING_CREATE], seconds);
}

static void began(otter_tuner *tuner, const otter_tuner_status *status,
                  otter_test_begin_kind kind, double seconds, int iteration,
                  void *opaque)
{
  profiling_context *context = opaque;
  (void)tuner;
  (void)status;
  (void)iteration;
  add_timing(&context->timings[timing_kind_for_begin(kind)], seconds);
}

static void destroyed(double seconds, void *opaque)
{
  profiling_context *context = opaque;
  add_timing(&context->timings[TIMING_DESTROY], seconds);
}

int main(int argc, char **argv)
{
  profiling_context context = {.timings = {
      {.name = "create+initial-bind"},
      {.name = "unchanged-begin"},
      {.name = "thread-grow"},
      {.name = "thread-shrink"},
      {.name = "placement-switch"},
      {.name = "finalize-begin"},
      {.name = "done-noop"},
      {.name = "destroy+restore"}}};
  otter_test_observer observer = {
      .created = created, .began = began, .destroyed = destroyed};
  otter_test_run_result summary = {0};
  int rounds = 20;
  int require_libomp = 0;
  int failures = 0;
  int argument;
  int round;
  const char *runtime;

  for (argument = 1; argument < argc; argument++) {
    if (strcmp(argv[argument], "--rounds") == 0 && argument + 1 < argc) {
      rounds = otter_test_parse_positive_int(argv[++argument], 10000);
      if (rounds < 0) {
        fprintf(stderr, "--rounds must be between 1 and 10000\n");
        return 2;
      }
    } else if (strcmp(argv[argument], "--require-libomp") == 0) {
      require_libomp = 1;
    } else {
      fprintf(stderr, "usage: %s [--rounds N] [--require-libomp]\n",
              argv[0]);
      return 2;
    }
  }

  otter_test_configure_environment();
  (void)omp_get_max_threads();
  runtime = otter_test_openmp_runtime_name();
  printf("compiler=%s\nOpenMP=%d runtime=%s proc_bind=%d rounds=%d\n",
         otter_test_compiler_name(), _OPENMP, runtime,
         (int)omp_get_proc_bind(), rounds);
  if (require_libomp && strcmp(runtime, "LLVM libomp") != 0) {
    fprintf(stderr, "FAIL: --require-libomp requested, found %s\n", runtime);
    return 1;
  }

  for (round = 0; round < rounds; round++) {
    otter_test_run_result run = {0};
    if (!otter_test_run_round("otter-overhead", 32, &observer, &context,
                              &run)) {
      failures++;
      break;
    }
    if (round == 0) {
      summary = run;
    } else if (run.max_threads != summary.max_threads ||
               run.candidate_cpus != summary.candidate_cpus ||
               run.final_threads != summary.final_threads ||
               run.final_placement != summary.final_placement) {
      fprintf(stderr, "FAIL: topology or selected configuration changed "
                      "between profiling rounds\n");
      failures++;
      break;
    }
  }

  if (failures == 0) {
    printf("profiling=PASS T_max=%d candidate_cpus=%d T_best=%d P_best=%s\n",
           summary.max_threads, summary.candidate_cpus,
           summary.final_threads,
           otter_test_placement_name(summary.final_placement));
  } else {
    printf("profiling=FAIL failures=%d\n", failures);
  }
  print_timings(context.timings);

  for (round = 0; round < TIMING_COUNT; round++) {
    free(context.timings[round].values);
  }
  return failures == 0 ? 0 : 1;
}
