#include "region_timers.h"

#include <assert.h>
#include <math.h>
#include <omp.h>

enum {
  R_WORK_PARALLEL, R_WORK_FOR, R_TAIL_FOR,
  R_GROUPED_PARALLEL, R_CHAIN_FOR, R_ORDINARY_FOR,
  R_COMBINED, R_EXCLUDED_PARALLEL, R_EXCLUDED_FOR, R_COUNT
};

static int sum;
static unsigned clock_calls, worker_clock_calls;

/* A worker clock read violates master-only region timing. */
double __wrap_omp_get_wtime(void)
{
  if (omp_get_thread_num() != 0) {
    #pragma omp atomic update
    worker_clock_calls++;
    return 0;
  } else {
    return ++clock_calls * 0.001; /* Deterministic accumulated-time checks. */
  }
}

/* An orphaned loop, reached twice per team. */
static void work(void)
{
  int n = omp_get_num_threads();
  NPB_FOR_BEGIN(R_WORK_FOR)
  #pragma omp for reduction(+:sum)
  for (int i = 0; i < n; ++i) {
    sum += 1;
  }
  NPB_FOR_END()
}

static void compute(void)
{
  NPB_PARALLEL_BEGIN(R_WORK_PARALLEL)
  #pragma omp parallel
  {
    for (int r = 0; r < 2; ++r) work();
    NPB_FOR_BEGIN(R_TAIL_FOR)
    #pragma omp for nowait
    for (int i = 0; i < 16; ++i) {
      (void)i;
    }
    NPB_FOR_END()
  }
  NPB_PARALLEL_END()
}

static void nowait_chain(void)
{
  int n = omp_get_num_threads();
  NPB_FOR_BEGIN(R_CHAIN_FOR)
  #pragma omp for schedule(static) nowait
  for (int i = 0; i < n; ++i) {
    (void)i;
  }
  #pragma omp for nowait
  for (int i = 0; i < 16; ++i) { (void)i; }
  NPB_FOR_END()
}

static void grouped(void)
{
  NPB_PARALLEL_BEGIN(R_GROUPED_PARALLEL)
  #pragma omp parallel
  {
    for (int r = 0; r < 2; ++r) {
      nowait_chain();
      /* The nowait pair has already ended. This ordinary for has its own
       * region; its implicit barrier is not part of the chain.
       */
      NPB_FOR_BEGIN(R_ORDINARY_FOR)
      #pragma omp for
      for (int i = 0; i < 1; ++i) {
        (void)i;
      }
      NPB_FOR_END()
    }
    for (int r = 0; r < 2; ++r) {
      nowait_chain();
      #pragma omp barrier
    }
    nowait_chain();             /* Ends before the parallel join. */
  }
  NPB_PARALLEL_END()
}

static void combined(void)
{
  int value = 0;
  NPB_PARALLEL_FOR_BEGIN(R_COMBINED)
  #pragma omp parallel for schedule(static, 2) reduction(+:value)
  for (int i = 0; i < 16; ++i) value += i;
  NPB_PARALLEL_FOR_END()
  assert(value == 120);
}

static void excluded(void)
{
  NPB_PARALLEL_BEGIN(R_EXCLUDED_PARALLEL)
  #pragma omp parallel
  {
    NPB_FOR_BEGIN(R_EXCLUDED_FOR)
    #pragma omp for
    for (int i = 0; i < 3; ++i) {
      (void)i;
    }
    NPB_FOR_END()
  }
  NPB_PARALLEL_END()
}

int main(int argc, char **argv)
{
  const int teams[] = {2, 1, 3};
  unsigned before;
  (void)argv;
  if (argc > 1) {
    npb_time_report();            /* Enabled but no measured iterations. */
    assert(clock_calls == 0);
    return 0;
  }
  omp_set_dynamic(0);
  omp_set_num_threads(2);
  before = clock_calls;
  compute();                     /* Warmup: must not increment report. */
  grouped();
  combined();
  excluded();
  assert(clock_calls - before == 0); /* Warmup is outside iteration windows. */
  for (int i = 0; i < 3; ++i) {
    omp_set_num_threads(teams[i]); /* Check shrinking and growing teams. */
    npb_time_begin();
    compute();
    grouped();
    combined();
    npb_time_end();
  }
  before = clock_calls;
  compute();                     /* Verification-like reuse. */
  grouped();
  combined();
  excluded();
  assert(clock_calls - before == 0);
  assert(sum == 4 + 2 * (2 + 1 + 3) + 6);
  npb_time_report();
  assert(worker_clock_calls == 0);
  assert(npb_time_total() > 0);
  if (npb_time_enabled()) {
    assert(clock_calls > 32);
    assert(fabs(npb_time_read(R_WORK_FOR) - 0.006) < 1e-10);
    assert(fabs(npb_time_read(R_TAIL_FOR) - 0.003) < 1e-10);
    assert(fabs(npb_time_read(R_CHAIN_FOR) - 0.015) < 1e-10);
    assert(fabs(npb_time_read(R_ORDINARY_FOR) - 0.006) < 1e-10);
    assert(fabs(npb_time_read(R_COMBINED) - 0.003) < 1e-10);
  } else {
    assert(clock_calls == 6);      /* Only two total timestamps per window. */
    for (int i = 0; i < R_COUNT; ++i) assert(npb_time_read(i) == 0);
  }
  assert(npb_time_read(R_EXCLUDED_PARALLEL) == 0);
  assert(npb_time_read(R_EXCLUDED_FOR) == 0);
  return 0;
}

const npb_region_info npb_regions[] = {
  [R_WORK_PARALLEL] = {"compute:43", -1, 0},
  [R_WORK_FOR] = {"work:33", R_WORK_PARALLEL, 0},
  [R_TAIL_FOR] = {"compute:47-51 (nowait)", R_WORK_PARALLEL, 0},
  [R_GROUPED_PARALLEL] = {"grouped:72", -1, 0},
  [R_CHAIN_FOR] = {"nowait_chain:60-66 (nowait)", R_GROUPED_PARALLEL, 0},
  [R_ORDINARY_FOR] = {"grouped:80", R_GROUPED_PARALLEL, 0},
  [R_COMBINED] = {"combined:99", -1, 1},
  [R_EXCLUDED_PARALLEL] = {"excluded:108", -1, 0},
  [R_EXCLUDED_FOR] = {"excluded:111", R_EXCLUDED_PARALLEL, 0},
};

const int npb_region_count = R_COUNT;
