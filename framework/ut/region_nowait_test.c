#define _POSIX_C_SOURCE 200809L
#include "../region_control/region_control.h"
#include "region_metadata.h"
#include <assert.h>
#include <omp.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <time.h>

enum { P_TAIL, F_TAIL,
       P_NEXT, F_NEXT, F_AFTER,
       P_SYNC, F_HELPER, P_CONDITIONAL, F_CONDITIONAL, REGION_COUNT };
static region_info regions[REGION_COUNT] = {
  [P_TAIL] = REGION_INFO(P_TAIL, -1, 0, 0),
  [F_TAIL] = REGION_INFO(F_TAIL, P_TAIL, 0, 1),
  [P_NEXT] = REGION_INFO(P_NEXT, -1, 0, 0),
  [F_NEXT] = REGION_INFO(F_NEXT, P_NEXT, 0, 1),
  [F_AFTER] = REGION_INFO(F_AFTER, P_NEXT, 0, 0),
  [P_SYNC] = REGION_INFO(P_SYNC, -1, 0, 0),
  [F_HELPER] = REGION_INFO(F_HELPER, P_SYNC, 0, 1),
  [P_CONDITIONAL] = REGION_INFO(P_CONDITIONAL, -1, 0, 0),
  [F_CONDITIONAL] = REGION_INFO(F_CONDITIONAL, P_CONDITIONAL, 0, 1)
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

/* A terminal chain is one interval, ending after the enclosing parallel join. */
static void tail(void) {
  atomic_store(&released, 0);
  PARALLEL_START(&control, P_TAIL);
#pragma omp parallel
  {
    FOR_START(&control, F_TAIL);
#pragma omp for schedule(static, 1) nowait
    for (int i = 0; i < 2; ++i) if (i == 1) worker(1);
#pragma omp for schedule(static, 1) nowait
    for (int i = 0; i < 2; ++i) if (i == 1) worker(0);
#pragma omp master
    atomic_store(&released, 1);
  }
  FOR_END(&control, F_TAIL);
  PARALLEL_END(&control, P_TAIL);
}

/* One source pair covers two nowait loops and the following ordinary loop. */
static void next_for(void) {
  atomic_store(&released, 0);
  PARALLEL_START(&control, P_NEXT);
#pragma omp parallel
  {
    FOR_START(&control, F_NEXT);
#pragma omp for schedule(static, 1) nowait
    for (int i = 0; i < 2; ++i) if (i == 1) worker(1);
#pragma omp for schedule(static, 1) nowait
    for (int i = 0; i < 2; ++i) if (i == 1) worker(1);
#pragma omp for schedule(static, 1)
    for (int i = 0; i < 2; ++i) {
      if (i == 0) atomic_store(&released, 1);
      else worker(0);
    }
    FOR_END(&control, F_NEXT);
    FOR_START(&control, F_AFTER);
#pragma omp for schedule(static, 1)
    for (int i = 0; i < 2; ++i) if (i == 0) atomic_fetch_add(&phase, 1);
    FOR_END(&control, F_AFTER);
  }
  PARALLEL_END(&control, P_NEXT);
}

/* A helper's nowait interval remains open after the helper returns. */
static void helper(void) {
  FOR_START(&control, F_HELPER);
#pragma omp for schedule(static, 1) nowait
  for (int i = 0; i < 2; ++i) if (i == 1) worker(1);
}

/* The existing explicit barrier is followed by the helper interval's end. */
static void explicit_sync(void) {
  atomic_store(&released, 0);
  PARALLEL_START(&control, P_SYNC);
#pragma omp parallel
  {
    helper();
#pragma omp master
    atomic_store(&released, 1);
#pragma omp barrier
    FOR_END(&control, F_HELPER);
#pragma omp master
    atomic_fetch_add(&phase, 10);
  }
  PARALLEL_END(&control, P_SYNC);
}

/* A conditional group must execute either both source hooks or neither. */
static void conditional(int execute) {
  atomic_store(&released, 0);
  PARALLEL_START(&control, P_CONDITIONAL);
#pragma omp parallel
  {
    if (execute) {
      FOR_START(&control, F_CONDITIONAL);
#pragma omp for schedule(static, 1) nowait
      for (int i = 0; i < 2; ++i) if (i == 1) worker(1);
    }
#pragma omp master
    atomic_store(&released, 1);
  }
  if (execute) FOR_END(&control, F_CONDITIONAL);
  PARALLEL_END(&control, P_CONDITIONAL);
}

/* Check real synchronization with logical time, then repeat with reporting off. */
int main(int argc, char **argv) {
  (void)argv;
  int report = argc == 1;
  omp_set_dynamic(0);
  omp_set_num_threads(2);
  region_control_init(&control, regions, REGION_COUNT, report);
  for (int id = 0; id < REGION_COUNT; ++id)
    assert(control.regions[id].name && control.regions[id].file && control.regions[id].line > 0);
  iteration_start(&control);
  tail();
  tail();
  next_for();
  next_for();
  explicit_sync();
  conditional(1);
  double conditional_time = control.elapsed[F_CONDITIONAL];
  conditional(0);
  assert(control.elapsed[F_CONDITIONAL] == conditional_time);
  iteration_end(&control);
  assert(atomic_load(&phase) == 24);
  if (report) {
    double tail_time = control.elapsed[F_TAIL];
    assert(tail_time >= 4 && tail_time < 5);
    double next_time = control.elapsed[F_NEXT];
    assert(next_time >= 6 && next_time < 7);
    assert(control.elapsed[F_AFTER] >= 2 && control.elapsed[F_AFTER] < 3);
    int completed[] = {F_HELPER, F_CONDITIONAL};
    for (int i = 0; i < 2; ++i) {
      double seconds = control.elapsed[completed[i]];
      assert(seconds >= 1 && seconds < 2);
    }
    assert(tail_time < control.elapsed[P_TAIL]);
    assert(next_time + control.elapsed[F_AFTER] < control.elapsed[P_NEXT]);
  } else {
    assert(clock_calls == 2);
    for (int id = 0; id < REGION_COUNT; ++id)
      assert(control.elapsed[id] == 0);
  }
  region_report(&control);
  puts("manual_nowait=PASS");
}
