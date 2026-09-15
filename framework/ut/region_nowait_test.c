#define _POSIX_C_SOURCE 200809L
#include "../region_control/region_control.h"
#include <assert.h>
#include <omp.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <time.h>

enum { P_TAIL, F_TAIL, P_NEXT, F_NEXT, F_ORDINARY,
       P_SYNC, F_HELPER, P_CONDITIONAL, F_CONDITIONAL, REGION_COUNT };
static region_info regions[REGION_COUNT] = {
  [P_TAIL] = {.parent = -1},
  [F_TAIL] = {.parent = P_TAIL, .nowait = 1},
  [P_NEXT] = {.parent = -1},
  [F_NEXT] = {.parent = P_NEXT, .nowait = 1},
  [F_ORDINARY] = {.parent = P_NEXT},
  [P_SYNC] = {.parent = -1},
  [F_HELPER] = {.parent = P_SYNC, .nowait = 1},
  [P_CONDITIONAL] = {.parent = -1},
  [F_CONDITIONAL] = {.parent = P_CONDITIONAL, .nowait = 1}
};
static region_control control;
static atomic_int released, phase;
static int clock_calls;

/* Only master reads time; completed worker work advances a deterministic clock. */
double __wrap_omp_get_wtime(void) {
  assert(omp_get_thread_num() == 0);
  return atomic_load(&phase) + 0.001 * ++clock_calls;
}

/* Wait for a post-loop action: an inserted loop-end barrier would deadlock. */
static void worker(int delay) {
  while (!atomic_load(&released)) sched_yield();
  if (delay) {
    struct timespec wait = {0, 20000000};
    nanosleep(&wait, 0);
  }
  atomic_fetch_add(&phase, 1);
}

/* The final nowait interval must include the enclosing parallel join. */
static void tail(void) {
  atomic_store(&released, 0);
  PARALLEL_START(&control, P_TAIL);
#pragma omp parallel
  {
    FOR_START(&control, F_TAIL);
#pragma omp for schedule(static, 1) nowait
    for (int i = 0; i < 2; ++i) if (i == 1) worker(1);
    FOR_END(&control, F_TAIL);
#pragma omp master
    atomic_store(&released, 1);
  }
  PARALLEL_END(&control, P_TAIL);
}

/* A following ordinary for closes the prior group before releasing its worker. */
static void next_for(void) {
  atomic_store(&released, 0);
  PARALLEL_START(&control, P_NEXT);
#pragma omp parallel
  {
    FOR_START(&control, F_NEXT);
#pragma omp for schedule(static, 1) nowait
    for (int i = 0; i < 2; ++i) if (i == 1) worker(0);
    FOR_END(&control, F_NEXT);
    FOR_START(&control, F_ORDINARY);
#pragma omp for schedule(static, 1)
    for (int i = 0; i < 2; ++i) if (i == 0) atomic_store(&released, 1);
    FOR_END(&control, F_ORDINARY);
  }
  PARALLEL_END(&control, P_NEXT);
}

/* A helper's nowait interval remains open after the helper returns. */
static void helper(void) {
  FOR_START(&control, F_HELPER);
#pragma omp for schedule(static, 1) nowait
  for (int i = 0; i < 2; ++i) if (i == 1) worker(1);
  FOR_END(&control, F_HELPER);
}

/* The explicit barrier closes pending work before the subsequent phase. */
static void explicit_sync(void) {
  atomic_store(&released, 0);
  PARALLEL_START(&control, P_SYNC);
#pragma omp parallel
  {
    helper();
#pragma omp master
    atomic_store(&released, 1);
#pragma omp barrier
    REGION_SYNC(&control);
#pragma omp master
    atomic_fetch_add(&phase, 10);
  }
  PARALLEL_END(&control, P_SYNC);
}

/* Skipping a previously measured group must not reuse its pending start. */
static void conditional(int execute) {
  atomic_store(&released, 0);
  PARALLEL_START(&control, P_CONDITIONAL);
#pragma omp parallel
  {
    if (execute) {
      FOR_START(&control, F_CONDITIONAL);
#pragma omp for schedule(static, 1) nowait
      for (int i = 0; i < 2; ++i) if (i == 1) worker(1);
      FOR_END(&control, F_CONDITIONAL);
    }
#pragma omp master
    atomic_store(&released, 1);
  }
  PARALLEL_END(&control, P_CONDITIONAL);
}

/* Check real synchronization with logical time, then repeat with reporting off. */
int main(int argc, char **argv) {
  (void)argv;
  int report = argc == 1;
  omp_set_dynamic(0);
  omp_set_num_threads(2);
  region_control_init(&control, regions, REGION_COUNT, report);
  iteration_start(&control);
  tail();
  tail();
  next_for();
  explicit_sync();
  conditional(1);
  conditional(0);
  iteration_end(&control);
  assert(atomic_load(&phase) == 15);
  if (report) {
    double tail_time = control.elapsed[F_TAIL];
    assert(tail_time >= 2 && tail_time < 3);
    double next_time = control.elapsed[F_NEXT];
    assert(next_time > 0 && next_time < 0.5);
    int completed[] = {F_ORDINARY, F_HELPER, F_CONDITIONAL};
    for (int i = 0; i < 3; ++i) {
      double seconds = control.elapsed[completed[i]];
      assert(seconds >= 1 && seconds < 2);
    }
  } else {
    assert(clock_calls == 2);
    for (int id = 0; id < REGION_COUNT; ++id)
      assert(control.elapsed[id] == 0);
  }
  region_report(&control);
  puts("manual_nowait=PASS");
}
