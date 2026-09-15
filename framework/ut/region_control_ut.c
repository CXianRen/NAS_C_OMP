#include "../region_control/region_control.h"
#include <assert.h>
#include <omp.h>
#include <stdio.h>
#include <string.h>

static int reads, events[64], event_count;

/* Test clock: record exact event order and reject worker-thread clock reads. */
double __wrap_omp_get_wtime(void)
{
  assert(omp_get_thread_num() == 0);
  events[event_count++] = 1;
  return ++reads;
}

/* Test callback: a step notification takes no timestamp. */
static void on_step_start(void *context, int step)
{
  (void)context;
  events[event_count++] = 100 + step;
}

/* Test callback: selection/binding overhead must precede the measured interval. */
static void on_parallel_start(void *context, int id)
{
  (void)context;
  assert(id < 2);  /* No callbacks for the internal for regions. */
  events[event_count++] = 10 + id;
  omp_get_wtime();
}

/* Test callback: inspect the single sample before adding observation overhead. */
static void on_parallel_end(void *context, int id, double seconds)
{
  int report = *(int *)context;
  assert(id < 2 && seconds == (id == 1 && report ? 5.0 : 1.0));
  events[event_count++] = 20 + id;
  omp_get_wtime();
}

/* Test callback: ending a step also takes no timestamp. */
static void on_step_end(void *context, int step)
{
  (void)context;
  events[event_count++] = 200 + step;
}

/* Execute a combined region once and verify the automatically captured location. */
static void combined(region_control *control)
{
  (void)control;
  int sum = 0;
#if REGION_INSTRUMENT
  int line = __LINE__ + 2;
#endif
  PARALLEL_START(control, 0);
  #pragma omp parallel for reduction(+:sum)
  for (int i = 0; i < 8; ++i) sum += i;
  PARALLEL_END(control, 0);
  assert(sum == 28);
#if REGION_INSTRUMENT
  assert(control->regions[0].line == line);
  assert(!strcmp(control->regions[0].name, __func__));
  assert(!strcmp(control->regions[0].file, __FILE__));
#endif
}

/* Preserve nowait endpoints at the next for and at the outer parallel join. */
static void with_loops(region_control *control)
{
  (void)control;
  int sum = 0;
  PARALLEL_START(control, 1);
  #pragma omp parallel reduction(+:sum)
  {
    FOR_START(control, 2);
    #pragma omp for nowait
    for (int i = 0; i < 8; ++i) sum += i;
    FOR_END(control, 2);
    FOR_START(control, 3);
    #pragma omp for
    for (int i = 0; i < 8; ++i) sum += i;
    FOR_END(control, 3);
    FOR_START(control, 4);
    #pragma omp for nowait
    for (int i = 0; i < 8; ++i) sum += i;
    FOR_END(control, 4);
  }
  PARALLEL_END(control, 1);
  assert(sum == 84);
}

/* Verify reporting and callback timing independently, then unregister the tuner. */
static void check(int report)
{
  region_info regions[] = {
    {NULL, -1, 1, NULL, 0, 0}, {NULL, -1, 0, NULL, 0, 0},
    {NULL, 1, 0, NULL, 0, 1}, {NULL, 1, 0, NULL, 0, 0}, {NULL, 1, 0, NULL, 0, 1}
  };
  const region_control_callbacks callbacks = {
    on_step_start, on_parallel_start, on_parallel_end, on_step_end
  };
  region_control control;
  reads = event_count = 0;
  region_control_init(&control, regions, 5, report);
  region_control_register(&control, &callbacks, &report);
  step_start(&control, 0);
  combined(&control);  /* Warmup outside the formal window has no callbacks/clocks. */
  step_end(&control, 0);
  assert(reads == 0 && event_count == 0);
  iteration_start(&control);
  step_start(&control, 7);
  combined(&control);
  with_loops(&control);
  step_end(&control, 7);
  iteration_end(&control);
  int expected_reads = REGION_INSTRUMENT ? (report ? 14 : 10) : 2;
  assert(reads == expected_reads && iteration_time(&control) == expected_reads - 1);
  int measured = REGION_INSTRUMENT && report;
  assert(control.elapsed[0] == (measured ? 1.0 : 0.0));
  assert(control.elapsed[1] == (measured ? 5.0 : 0.0));
  for (int id = 2; id < 5; ++id)
    assert(control.elapsed[id] == (measured ? 1.0 : 0.0));
  const int full[] = {1,107,10,1,1,1,20,1,11,1,1,1,1,1,1,1,21,1,207,1};
  const int sample[] = {1,107,10,1,1,1,20,1,11,1,1,1,21,1,207,1};
  const int plain[] = {1,107,207,1};
  const int *expected = REGION_INSTRUMENT ? (report ? full : sample) : plain;
  assert(event_count == (REGION_INSTRUMENT ? (report ? 20 : 16) : 4));
  for (int i = 0; i < event_count; ++i) assert(events[i] == expected[i]);
  region_report(&control);
  assert(reads == expected_reads);
  region_control_register(&control, NULL, NULL);
  reads = event_count = 0;
  double previous = iteration_time(&control);
  iteration_start(&control);
  combined(&control);  /* No registered callbacks: ordinary timing still works. */
  iteration_end(&control);
  assert(reads == (measured ? 4 : 2));
  assert(iteration_time(&control) - previous == (measured ? 3.0 : 1.0));
  region_control_register(&control, &callbacks, &report);
  reads = event_count = 0;
  iteration_start(&control);
  step_start(&control, 0);
  step_start(&control, 1);  /* step_end is optional; starts do not read time. */
  assert(reads == 1 && event_count == 3 && events[1] == 100 && events[2] == 101);
  iteration_end(&control);
  assert(reads == 2 && event_count == 4 && events[3] == 1 && !control.in_step);
}

/* Interleave independent contexts, including total-only and repeated end/report calls. */
static void check_contexts(void)
{
  region_info regions[] = {{NULL, -1, 1, NULL, 0, 0}};
  region_control a, b;
  reads = event_count = 0;
  region_control_init(&a, regions, 1, 1);
  region_control_init(&b, NULL, 0, 0);
  assert(reads == 0 && a.pending_nowait == -1 && b.pending_nowait == -1);
  iteration_start(&a);
  iteration_start(&b);
  combined(&a);
  iteration_end(&b);
  int previous_reads = reads;
  iteration_end(&b);
  assert(reads == previous_reads && a.running && !b.running);
  combined(&a);
  iteration_end(&a);
  assert(iteration_time(&a) == (REGION_INSTRUMENT ? 7.0 : 3.0));
  assert(iteration_time(&b) == (REGION_INSTRUMENT ? 3.0 : 1.0));
  assert(a.elapsed[0] == (REGION_INSTRUMENT ? 2.0 : 0.0) && b.elapsed[0] == 0.0);
  previous_reads = reads;
  region_report(&a);
  region_report(&b);
  assert(reads == previous_reads);
}

/* Run the same manually instrumented code with reports enabled and disabled. */
int main(void)
{
  check(1);
  check(0);
  check_contexts();
  puts("region_control=PASS (hooks, callbacks, samples, master, nowait, names, contexts, total-only)");
}
