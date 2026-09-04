#ifndef OTTER_TUNER_H
#define OTTER_TUNER_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Otter owns the policy and measurement state for one benchmark run.
 * Keeping the type opaque makes the benchmark integration a pair of thin
 * hooks around its existing outer iteration. The lifecycle and iteration
 * hooks are called serially by the benchmark's coordinating thread.
 */
typedef struct otter_tuner otter_tuner;

typedef enum {
  OTTER_TUNER_PLACEMENT_UNCONTROLLED,
  OTTER_TUNER_PLACEMENT_CONTIGUOUS,
  OTTER_TUNER_PLACEMENT_SCATTER
} otter_tuner_placement;

/* Read-only diagnostics used by correctness tests and lightweight tracing. */
typedef struct {
  int enabled;
  int tuning_done;
  int current_threads;
  int max_threads;
  int candidate_cpus;
  int placement_supported;
  otter_tuner_placement placement;
  const char *state_name;
} otter_tuner_status;

otter_tuner *otter_tuner_create(const char *benchmark_name);
void otter_tuner_begin_iteration(otter_tuner *tuner, int iteration);
void otter_tuner_end_iteration(otter_tuner *tuner);
int otter_tuner_get_status(const otter_tuner *tuner,
                           otter_tuner_status *status);
int otter_tuner_get_target_cpu(const otter_tuner *tuner, int thread);
void otter_tuner_destroy(otter_tuner *tuner);

#ifdef __cplusplus
}
#endif

#endif
