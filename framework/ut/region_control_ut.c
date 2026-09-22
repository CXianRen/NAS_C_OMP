#define _POSIX_C_SOURCE 200809L
#include "../region_control/region_control.h"
#include "region_metadata.h"
#include <assert.h>
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int reads, events[64], event_count;

static void set_report_environment(int report)
{
  assert(setenv("REGION_TIME_REPORT", report ? "1" : "0", 1) == 0);
}

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
  assert(id < 2 && seconds == (id == 1 && report ? 7.0 : 1.0));
  events[event_count++] = 20 + id;
  omp_get_wtime();
}

/* Test callback: ending a step also takes no timestamp. */
static void on_step_end(void *context, int step)
{
  (void)context;
  events[event_count++] = 200 + step;
}

/* Execute a combined region without changing its compile-time metadata. */
static void combined(region_control *control)
{
  (void)control;
  int sum = 0;
  int line = __LINE__ + 1;
  PARALLEL_START(control, 0);
  #pragma omp parallel for reduction(+:sum)
  for (int i = 0; i < 8; ++i) sum += i;
  PARALLEL_END(control, 0);
  assert(sum == 28);
  assert(control->regions[0].line == line);
  assert(!strcmp(control->regions[0].name, __func__));
  assert(!strcmp(control->regions[0].file, __FILE__));
}

/* Source pairs: ordinary A, merged B+C, terminal D ending after the join. */
static void with_loops(region_control *control)
{
  (void)control;
  int sum = 0;
  PARALLEL_START(control, 1);
  #pragma omp parallel reduction(+:sum)
  {
    FOR_START(control, 2);
    #pragma omp for
    for (int i = 0; i < 8; ++i) sum += i;
    FOR_END(control, 2);
    FOR_START(control, 3);
    #pragma omp for nowait
    for (int i = 0; i < 8; ++i) sum += i;
    #pragma omp for
    for (int i = 0; i < 8; ++i) sum += i;
    FOR_END(control, 3);
    FOR_START(control, 4);
    #pragma omp for nowait
    for (int i = 0; i < 8; ++i) sum += i;
  }
  FOR_END(control, 4);
  PARALLEL_END(control, 1);
  assert(sum == 112);
}

/* Verify reporting and callback timing independently, then unregister the tuner. */
static void check(int report)
{
  region_info regions[] = {
    REGION_INFO(0, -1, 1, 0), REGION_INFO(1, -1, 0, 0),
    REGION_INFO(2, 1, 0, 0), REGION_INFO(3, 1, 0, 1), REGION_INFO(4, 1, 0, 1)
  };
  const region_control_callbacks callbacks = {
    on_step_start, on_parallel_start, on_parallel_end, on_step_end, NULL
  };
  region_control control;
  reads = event_count = 0;
  set_report_environment(report);
  region_control_init(&control, regions, 5);
  /* Every descriptor is readable before any region executes, even without hooks. */
  for (int id = 0; id < 5; ++id) {
    assert(!strcmp(control.regions[id].name, id ? "with_loops" : "combined"));
    assert(!strcmp(control.regions[id].file, __FILE__));
    assert(control.regions[id].line > 0);
  }
  region_info original[5];
  memcpy(original, regions, sizeof(original));
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
  int expected_reads = REGION_INSTRUMENT ? (report ? 16 : 10) : 2;
  assert(reads == expected_reads && iteration_time(&control) == expected_reads - 1);
  int measured = REGION_INSTRUMENT && report;
  assert(control.elapsed[0] == (measured ? 1.0 : 0.0));
  assert(control.elapsed[1] == (measured ? 7.0 : 0.0));
  assert(control.elapsed[2] == (measured ? 1.0 : 0.0));
  assert(control.elapsed[3] == (measured ? 1.0 : 0.0));
  assert(control.elapsed[4] == (measured ? 1.0 : 0.0));
  const int full[] = {1,107,10,1,1,1,20,1,11,1,1,1,1,1,1,1,1,1,21,1,207,1};
  const int sample[] = {1,107,10,1,1,1,20,1,11,1,1,1,21,1,207,1};
  const int plain[] = {1,107,207,1};
  const int *expected = REGION_INSTRUMENT ? (report ? full : sample) : plain;
  assert(event_count == (REGION_INSTRUMENT ? (report ? 22 : 16) : 4));
  for (int i = 0; i < event_count; ++i) assert(events[i] == expected[i]);
  assert(!memcmp(original, regions, sizeof(original)));
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
  region_info regions[] = {REGION_INFO(0, -1, 1, 0)};
  region_control a, b;
  reads = event_count = 0;
  set_report_environment(1);
  region_control_init(&a, regions, 1);
  set_report_environment(0);
  region_control_init(&b, NULL, 0);
  assert(reads == 0);
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

typedef struct {
  int count;
  int step[4];
  double seconds[4];
} step_samples;

/* Whole-step selection overhead belongs before the step timestamp. */
static void sample_step_start(void *context, int step)
{
  (void)context;
  events[event_count++] = 100 + step;
  omp_get_wtime();
}

/* Store one complete step, then simulate policy observation overhead. */
static void sample_step(void *context, int step, double seconds)
{
  step_samples *samples = context;
  assert(samples->count < 4);
  samples->step[samples->count] = step;
  samples->seconds[samples->count++] = seconds;
  events[event_count++] = 300 + step;
  omp_get_wtime();
}

static void check_event_suffix(int start, const int *expected, int count)
{
  assert(event_count == start + count);
  for (int i = 0; i < count; ++i) assert(events[start + i] == expected[i]);
}

/* Explicit/implicit endings observe once; unregister cancels an unfinished sample. */
static void check_step_samples(int report)
{
  region_info regions[] = {REGION_INFO(0, -1, 1, 0)};
  region_control control;
  step_samples samples = {0};
  const region_control_callbacks callbacks = {
    sample_step_start, NULL, NULL, on_step_end, sample_step
  };
  reads = event_count = 0;
  set_report_environment(report);
  region_control_init(&control, regions, 1);
  region_control_register(&control, &callbacks, &samples);
  step_start(&control, 6);
  step_end(&control, 6);
  assert(reads == 0 && event_count == 0 && samples.count == 0);
  iteration_start(&control);
  step_start(&control, 7);
  const int start[] = {1, 107, 1, 1};
  check_event_suffix(0, start, 4);
  combined(&control);
  omp_get_wtime();  /* Serial work outside any region is part of the step. */
  int begin = event_count;
  step_end(&control, 7);
  const int explicit_end[] = {1, 307, 1, 207};
  check_event_suffix(begin, explicit_end, 4);
  int measured = REGION_INSTRUMENT && report;
  assert(samples.count == 1 && samples.step[0] == 7);
  assert(samples.seconds[0] == 2.0 + 2.0 * measured);
  assert(!control.step_sample_active);

  begin = event_count;
  step_start(&control, 8);
  const int next_start[] = {108, 1, 1};
  check_event_suffix(begin, next_start, 3);  /* No duplicate sample for step 7. */
  combined(&control);
  combined(&control);
  omp_get_wtime();
  begin = event_count;
  step_start(&control, 9);
  const int implicit_end[] = {1, 308, 1, 109, 1, 1};
  check_event_suffix(begin, implicit_end, 6);
  assert(samples.count == 2 && samples.step[1] == 8);
  assert(samples.seconds[1] == 2.0 + 4.0 * measured);
  omp_get_wtime();
  omp_get_wtime();
  begin = event_count;
  iteration_end(&control);
  const int final_end[] = {1, 309, 1, 1};
  check_event_suffix(begin, final_end, 4);
  assert(samples.count == 3 && samples.step[2] == 9 && samples.seconds[2] == 3.0);
  assert(!control.step_sample_active && !control.in_step);
  int previous_reads = reads;
  iteration_end(&control);
  assert(reads == previous_reads && samples.count == 3);

  iteration_start(&control);
  step_start(&control, 10);
  assert(control.step_sample_active && samples.count == 3);
  previous_reads = reads;
  region_control_register(&control, NULL, NULL);
  assert(!control.step_sample_active && reads == previous_reads);
  iteration_end(&control);
  assert(samples.count == 3 && reads == previous_reads + 1);
}

/* Each context reads the environment once at init, with exact accepted values.
 * The disabled build must reject enabling requests without affecting sampling. */
static void check_report_environment(void)
{
  const struct { const char *value; int enabled; } cases[] = {
    {NULL, 0}, {"1", 1}, {"0", 0}, {"true", 1}, {"false", 0},
    {"yes", 1}, {"invalid", 0}, {"on", 1}, {"", 0}, {"TRUE", 0},
    {"YES", 0}, {"ON", 0}, {" 1", 0}, {"1 ", 0}, {"on", 1}, {NULL, 0}
  };
  region_info regions[] = {REGION_INFO(0, -1, 1, 0)};
  region_control retained, current;
  int previous_reads = reads;
  set_report_environment(1);
  region_control_init(&retained, regions, 1);
  for (unsigned index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
    if (cases[index].value)
      assert(setenv("REGION_TIME_REPORT", cases[index].value, 1) == 0);
    else
      assert(unsetenv("REGION_TIME_REPORT") == 0);
    region_control_init(&current, regions, 1);
    assert(current.enabled == (REGION_INSTRUMENT && cases[index].enabled));
    assert(!current.active && !current.running);
    assert(retained.enabled == !!REGION_INSTRUMENT);
  }
  assert(reads == previous_reads);  /* Parsing does not start a timing window. */
}

/* Run the same manually instrumented code with reports enabled and disabled. */
int main(void)
{
  check_report_environment();
  check(1);
  check(0);
  check_contexts();
  check_step_samples(0);
  check_step_samples(1);
  puts("region_control=PASS (hooks, callbacks, region/step samples, master, nowait, compile-time metadata, contexts, environment, total-only)");
}
