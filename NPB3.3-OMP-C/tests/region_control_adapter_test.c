#include "region_control_adapter.h"
#include <assert.h>
#include <stdio.h>

const npb_region_info npb_regions[] = {
  {"combined:1", -1, 1}, {"tail:2", -1, 0}, {"tail:3 (nowait)", 1, 0}
};
const int npb_region_count = 3;
static int reads, report;

/* Test clock: exact integer timestamps, with no worker-thread reads. */
double __wrap_omp_get_wtime(void)
{
  assert(omp_get_thread_num() == 0);
  return ++reads;
}

#if NPB_REGION_CONTROL
static int active, events[16], event_count;

/* Test bridge: step notifications change activity without reading time. */
void npb_control_step_start(int step)
{
  active = 1;
  events[event_count++] = 100 + step;
}

/* Test bridge: close the step without timing or clearing region history. */
void npb_control_step_finish(int step)
{
  active = 0;
  events[event_count++] = 200 + step;
}

/* Test bridge: only explicitly marked steps control their regions. */
int npb_control_active(void) { return active; }

/* Test bridge: consume one timestamp to model selection/binding overhead. */
void npb_control_region_start(int id)
{
  events[event_count++] = 10 + id;
  omp_get_wtime();
}

/* Test bridge: validate one sample, then model observation overhead. */
void npb_control_region_finish(int id, double seconds)
{
  assert(seconds == (id == 1 && report ? 2.0 : 1.0));
  events[event_count++] = 20 + id;
  omp_get_wtime();
}
#endif

/* Execute the original combined region exactly once per invocation. */
static void combined(void)
{
  int sum = 0;
  NPB_PARALLEL_FOR_BEGIN(0)
  #pragma omp parallel for reduction(+:sum)
  for (int i = 0; i < 8; ++i) sum += i;
  NPB_PARALLEL_FOR_END()
  assert(sum == 28);
}

/* A trailing nowait child must share the outer region's final timestamp. */
static void tail(void)
{
  int sum = 0;
  NPB_PARALLEL_BEGIN(1)
  #pragma omp parallel
  {
    #pragma omp master
    npb_time_nowait_start(2);
    #pragma omp for nowait reduction(+:sum)
    for (int i = 0; i < 8; ++i) sum += i;
  }
  NPB_PARALLEL_END()
  assert(sum == 28);
}

/* Verify independent report/control switches, step order and sample reuse. */
int main(void)
{
  report = npb_time_enabled();
  combined();
  assert(reads == 0);  /* Warmup is outside both timing and control windows. */
  npb_time_begin();
  npb_control_step_start(0);
  combined();
  tail();
  npb_control_step_finish(0);
  npb_control_step_start(1);
  combined();
  npb_control_step_finish(1);
  npb_time_end();
#if NPB_REGION_CONTROL
  const int expected[] = {100, 10, 20, 11, 21, 200, 101, 10, 20, 201};
  assert(event_count == (int)(sizeof(expected) / sizeof(expected[0])));
  for (int i = 0; i < event_count; ++i) assert(events[i] == expected[i]);
#endif
  int expected_reads = 2 + 6 * NPB_REGION_CONTROL
                          + 6 * (NPB_REGION_CONTROL || report) + report;
  assert(reads == expected_reads);
  assert(npb_time_total() == expected_reads - 1);
  assert(npb_time_read(0) == (report ? 2.0 : 0.0));
  assert(npb_time_read(1) == (report ? 2.0 : 0.0));
  assert(npb_time_read(2) == (report ? 1.0 : 0.0));
  combined();  /* Verification after the formal loop also stays uncontrolled. */
  assert(reads == expected_reads);
  npb_time_report();
  puts("region_control_adapter=PASS");
}
