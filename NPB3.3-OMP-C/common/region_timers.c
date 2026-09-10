#include "region_timers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static double start[NPB_MAX_REGIONS], elapsed[NPB_MAX_REGIONS];
static double iteration_start, iteration_elapsed;
static int enabled = -1;
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
  if (!npb_time_enabled()) return;
  iteration_start = omp_get_wtime();
  npb_time_active = 1;
}

void npb_time_end(void)
{
  if (!npb_time_active) return;
  iteration_elapsed += omp_get_wtime() - iteration_start;
  npb_time_active = 0;
}

void npb_time_start(int id)
{
  if (npb_time_active) start[id] = omp_get_wtime();
}

void npb_time_stop(int id)
{
  if (npb_time_active) elapsed[id] += omp_get_wtime() - start[id];
}

double npb_time_read(int id)
{
  return elapsed[id];
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
