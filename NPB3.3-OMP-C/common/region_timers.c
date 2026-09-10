#include "region_timers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static double start[NPB_MAX_REGIONS], elapsed[NPB_MAX_REGIONS];
static double iteration_start, iteration_elapsed;
static int enabled = -1, iteration_running;
/* Accessed only by the primary thread. Nested parallel timing is unsupported. */
static int pending_nowait = -1;
int npb_time_active;

int npb_time_enabled(void)
{
  if (enabled < 0) {
    const char *v = getenv("NPB_TIME_REPORT");
    enabled = v && (!strcmp(v, "1") || !strcmp(v, "true") ||
                    !strcmp(v, "yes") || !strcmp(v, "on"));
  }
  return enabled;
}

void npb_time_begin(void)
{
  iteration_running = 1;
  iteration_start = omp_get_wtime();
  npb_time_active = npb_time_enabled();
}

void npb_time_end(void)
{
  if (!iteration_running) return;
  iteration_elapsed += omp_get_wtime() - iteration_start;
  npb_time_active = 0;
  iteration_running = 0;
}

static void finish_nowait(double end)
{
  if (pending_nowait < 0) return;
  elapsed[pending_nowait] += end - start[pending_nowait];
  pending_nowait = -1;
}

void npb_time_start(int id)
{
  if (npb_time_active) {
    double begin = omp_get_wtime();
    /* A new for starts a new interval, including when called from a helper. */
    finish_nowait(begin);
    start[id] = begin;
  }
}

void npb_time_stop(int id)
{
  if (npb_time_active) {
    double end = omp_get_wtime();
    elapsed[id] += end - start[id];
    if (pending_nowait == id) pending_nowait = -1;
    /* The parallel stop runs after its existing implicit barrier/join.
     * Reuse this timestamp for its last nowait child and its own total.
     */
    if (pending_nowait >= 0 && npb_regions[pending_nowait].parent == id)
      finish_nowait(end);
  }
}

void npb_time_nowait_start(int id)
{
  if (npb_time_active) {
    npb_time_start(id);
    pending_nowait = id;
  }
}

void npb_time_sync(void)
{
  if (npb_time_active && pending_nowait >= 0)
    finish_nowait(omp_get_wtime());
}

double npb_time_read(int id)
{
  return elapsed[id];
}

double npb_time_total(void)
{
  return iteration_elapsed;
}

static double step_percent(double seconds)
{
  return iteration_elapsed > 0 ? 100.0 * seconds / iteration_elapsed : 0.0;
}

void npb_time_report(void)
{
  int p, f;
  if (!npb_time_enabled()) return;
  puts("\ntime report\nunit: seconds");
  printf("iteration total: %.9f s\n", iteration_elapsed);
  puts("step %: accumulated region time / iteration total (average time-step basis)");
  for (p = 0; p < npb_region_count; ++p) {
    const npb_region_info *r = &npb_regions[p];
    if (r->parent != -1 || elapsed[p] == 0) continue;
    printf("parallel region %s  %.9f s  step: %.3f%%\n",
           r->name, elapsed[p], step_percent(elapsed[p]));
    if (r->combined)
      printf("    for region %s  %.9f s  step: %.3f%%\n",
             r->name, elapsed[p], step_percent(elapsed[p]));
    for (f = 0; f < npb_region_count; ++f)
      if (npb_regions[f].parent == p && elapsed[f] != 0)
        printf("    for region %s  %.9f s  step: %.3f%%\n",
               npb_regions[f].name, elapsed[f], step_percent(elapsed[f]));
  }
}
