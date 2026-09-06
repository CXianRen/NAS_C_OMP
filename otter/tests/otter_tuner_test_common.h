#ifndef OTTER_TUNER_TEST_COMMON_H
#define OTTER_TUNER_TEST_COMMON_H

#include "otter_tuner.h"

typedef enum {
  OTTER_TEST_BEGIN_UNCHANGED,
  OTTER_TEST_BEGIN_THREAD_GROW,
  OTTER_TEST_BEGIN_THREAD_SHRINK,
  OTTER_TEST_BEGIN_PLACEMENT_SWITCH,
  OTTER_TEST_BEGIN_FINALIZE,
  OTTER_TEST_BEGIN_DONE_NOOP
} otter_test_begin_kind;

typedef struct {
  int max_threads;
  int candidate_cpus;
  int final_threads;
  otter_tuner_placement final_placement;
  int completed;
} otter_test_run_result;

typedef struct {
  void (*created)(const otter_tuner_status *status, double seconds,
                  void *context);
  void (*began)(otter_tuner *tuner, const otter_tuner_status *status,
                otter_test_begin_kind kind, double seconds, int iteration,
                void *context);
  void (*destroyed)(double seconds, void *context);
} otter_test_observer;

void otter_test_configure_environment(void);
const char *otter_test_compiler_name(void);
const char *otter_test_openmp_runtime_name(void);
const char *otter_test_placement_name(otter_tuner_placement placement);
const char *otter_test_begin_kind_name(otter_test_begin_kind kind);
int otter_test_parse_positive_int(const char *text, int maximum);

int otter_test_run_round(const char *benchmark_name, int done_noop_calls,
                         const otter_test_observer *observer, void *context,
                         otter_test_run_result *result);

#endif
