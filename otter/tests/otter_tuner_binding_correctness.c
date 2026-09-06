#define _GNU_SOURCE

/* White-box test: reuse the real configuration path without running search.
 * This translation unit owns the implementation; do not link otter_tuner.o. */
#include "../otter_tuner.c"
#include "otter_tuner_test_common.h"

#include <stdarg.h>

typedef struct {
  int failures;
  int *observed;
} correctness_context;

static void report_failure(correctness_context *context,
                           const char *format, ...)
{
  va_list arguments;

  context->failures++;
  if (context->failures > 12) return;
  fprintf(stderr, "FAIL: ");
  va_start(arguments, format);
  vfprintf(stderr, format, arguments);
  va_end(arguments);
  fputc('\n', stderr);
}

/* Verify the affinity seen by the next real OpenMP region, not the apply team. */
static int probe_team(otter_tuner *tuner, const otter_tuner_status *status,
                      correctness_context *context)
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
    context->observed[thread] = -1;
  }

  #pragma omp parallel shared(actual_threads)
  {
    int tid = omp_get_thread_num();
    cpu_set_t affinity;

    #pragma omp single
    actual_threads = omp_get_num_threads();
    if (tid >= requested) {
      /* A wrong default team size must fail without overrunning buffers. */
    } else if (sched_getaffinity(0, sizeof(affinity), &affinity) != 0) {
      affinity_errors[tid] = errno;
    } else {
      mask_counts[tid] = CPU_COUNT(&affinity);
      context->observed[tid] = sched_getcpu();
      if (target_cpus[tid] >= 0 &&
          !CPU_ISSET(target_cpus[tid], &affinity)) {
        mask_counts[tid] = -mask_counts[tid];
      }
    }
  }

  if (actual_threads != requested) {
    report_failure(context, "%s requested %d threads but kernel observed %d",
                   status->state_name, requested, actual_threads);
  }
  for (thread = 0; thread < actual_threads && thread < requested; thread++) {
    int other;
    if (affinity_errors[thread] != 0) {
      report_failure(context, "%s thread %d: sched_getaffinity: %s",
                     status->state_name, thread,
                     strerror(affinity_errors[thread]));
    } else if (mask_counts[thread] != 1) {
      report_failure(context,
                     "%s thread %d: expected singleton CPU %d, mask_count=%d",
                     status->state_name, thread, target_cpus[thread],
                     mask_counts[thread]);
    } else if (context->observed[thread] != target_cpus[thread]) {
      report_failure(context, "%s thread %d: target CPU %d, observed CPU %d",
                     status->state_name, thread, target_cpus[thread],
                     context->observed[thread]);
    }
    for (other = 0; other < thread; other++) {
      if (context->observed[thread] == context->observed[other]) {
        report_failure(context, "%s threads %d and %d share CPU %d",
                       status->state_name, other, thread,
                       context->observed[thread]);
      }
    }
  }

  free(affinity_errors);
  free(mask_counts);
  free(target_cpus);
  return actual_threads;
}

static void check_configuration(otter_tuner *tuner,
                                correctness_context *context,
                                int threads, otter_placement placement,
                                const char *transition)
{
  otter_tuner_status status;
  int before = context->failures;
  int actual;

  tuner->current_threads = threads;
  tuner->placement = placement;
  actual = otter_apply_configuration(tuner);
  if (actual != threads || tuner->placement_failed) {
    report_failure(context, "%s: configuration application failed", transition);
  }
  otter_tuner_get_status(tuner, &status);
  actual = probe_team(tuner, &status, context);
  printf("enter cfg: T=%d, P=%s actual_T=%d check=%s change=%s\n",
         threads, otter_test_placement_name(status.placement), actual,
         context->failures == before ? "PASS" : "FAIL", transition);
}

int main(int argc, char **argv)
{
  correctness_context context = {0};
  otter_tuner *tuner;
  cpu_set_t original_affinity;
  int original_threads;
  int original_dynamic;
  int require_libomp = 0;
  int restore_failed = 0;
  int *contiguous;
  int half;
  int changed = 0;
  int thread;
  const char *runtime;

  if (argc == 2 && strcmp(argv[1], "--require-libomp") == 0) {
    require_libomp = 1;
  } else if (argc != 1) {
    fprintf(stderr, "usage: %s [--require-libomp]\n", argv[0]);
    return 2;
  }
  otter_test_configure_environment();
  original_threads = omp_get_max_threads();
  original_dynamic = omp_get_dynamic();
  runtime = otter_test_openmp_runtime_name();
  printf("compiler=%s\nOpenMP=%d runtime=%s proc_bind=%d\n",
         otter_test_compiler_name(), _OPENMP, runtime,
         (int)omp_get_proc_bind());
  if (require_libomp && strcmp(runtime, "LLVM libomp") != 0) {
    fprintf(stderr, "FAIL: --require-libomp requested, found %s\n", runtime);
    return 1;
  }
  if (sched_getaffinity(0, sizeof(original_affinity), &original_affinity) != 0) {
    perror("cannot capture original affinity");
    return 1;
  }
  tuner = otter_tuner_create("otter-binding-correctness");
  if (tuner == NULL || !tuner->enabled || tuner->max_threads < 4 ||
      !tuner->placement_supported || tuner->placement_failed ||
      tuner->runtime_mismatch) {
    fprintf(stderr, "FAIL: need at least 4 usable cores and working placement; "
                    "unset OMP_PLACES/KMP_AFFINITY/GOMP_CPU_AFFINITY and "
                    "set OMP_PROC_BIND=false\n");
    otter_tuner_destroy(tuner);
    return 1;
  }
  context.observed = calloc((size_t)tuner->max_threads,
                             sizeof(*context.observed));
  contiguous = calloc((size_t)tuner->max_threads, sizeof(*contiguous));
  if (context.observed == NULL || contiguous == NULL) {
    fprintf(stderr, "out of memory\n");
    otter_tuner_destroy(tuner);
    free(context.observed);
    free(contiguous);
    return 2;
  }
  half = tuner->max_threads / 2;

  check_configuration(tuner, &context, tuner->max_threads,
                      OTTER_CONTIGUOUS, "initial/full");
  check_configuration(tuner, &context, half,
                      OTTER_CONTIGUOUS, "thread-shrink");
  memcpy(contiguous, context.observed, (size_t)half * sizeof(*contiguous));
  check_configuration(tuner, &context, half,
                      OTTER_SCATTER, "contiguous-to-scatter");
  for (thread = 0; thread < half; thread++) {
    if (contiguous[thread] != context.observed[thread]) changed++;
  }
  if (changed == 0) {
    report_failure(&context, "placement switch did not change any CPU slots");
  }
  printf("placement changed_slots=%d/%d\n", changed, half);
  check_configuration(tuner, &context, tuner->max_threads,
                      OTTER_SCATTER, "thread-grow/scatter");
  check_configuration(tuner, &context, 1,
                      OTTER_SCATTER, "thread-shrink/single");
  check_configuration(tuner, &context, half,
                      OTTER_SCATTER, "thread-grow/scatter");
  check_configuration(tuner, &context, half,
                      OTTER_CONTIGUOUS, "scatter-to-contiguous");
  for (thread = 0; thread < half; thread++) {
    if (contiguous[thread] != context.observed[thread]) {
      report_failure(&context, "reverse placement changed contiguous CPU %d",
                     thread);
    }
  }
  check_configuration(tuner, &context, tuner->max_threads,
                      OTTER_CONTIGUOUS, "thread-grow/contiguous");
  check_configuration(tuner, &context, tuner->max_threads,
                      OTTER_CONTIGUOUS, "unchanged");

  otter_tuner_destroy(tuner);
  if (omp_get_max_threads() != original_threads ||
      omp_get_dynamic() != original_dynamic) {
    report_failure(&context, "destroy did not restore OpenMP settings");
  }
  /* Disable dynamic sizing only for this probe to revisit the full team. */
  omp_set_dynamic(0);
  #pragma omp parallel reduction(|:restore_failed)
  {
    cpu_set_t affinity;
    if (sched_getaffinity(0, sizeof(affinity), &affinity) != 0 ||
        !CPU_EQUAL(&affinity, &original_affinity)) {
      restore_failed = 1;
    }
  }
  omp_set_dynamic(original_dynamic);
  if (restore_failed) {
    report_failure(&context, "destroy did not restore worker affinity");
  }
  printf("restore worker affinity: %s\n", restore_failed ? "FAIL" : "PASS");
  printf("binding_correctness=%s failures=%d\n",
         context.failures == 0 ? "PASS" : "FAIL", context.failures);
  free(contiguous);
  free(context.observed);
  return context.failures == 0 ? 0 : 1;
}
