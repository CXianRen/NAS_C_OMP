#include "region_timer.h"
#include <assert.h>
#include <omp.h>
#include <stdio.h>
#include <string.h>

/* Initialize state without reading the clock or owning the region metadata. */
void region_timer_init(region_timer *timer, const timer_region_info *regions,
                       int count, int enabled)
{
  assert(timer && count >= 0 && count <= REGION_TIMER_MAX_REGIONS);
  assert(count == 0 || regions);
  memset(timer, 0, sizeof(*timer));
  timer->regions = regions;
  timer->region_count = count;
  timer->enabled = enabled;
  timer->pending_nowait = -1;
}

/* Start a total window; report activation is decided by the caller. */
void region_timer_begin(region_timer *timer)
{
  timer->running = 1;
  timer->total_start = omp_get_wtime();
  timer->active = timer->enabled;
}

/* Accumulate one total window and disable region accumulation outside it. */
void region_timer_end(region_timer *timer)
{
  if (!timer->running) return;
  timer->total_elapsed += omp_get_wtime() - timer->total_start;
  timer->active = 0;
  timer->running = 0;
}

/* Close a pending nowait region using an already captured timestamp. */
static void finish_nowait(region_timer *timer, double end)
{
  if (timer->pending_nowait < 0) return;
  int id = timer->pending_nowait;
  timer->elapsed[id] += end - timer->start[id];
  timer->pending_nowait = -1;
}

/* Begin a region at the same timestamp that closes a preceding nowait region. */
static void start_at(region_timer *timer, int id, double begin)
{
  finish_nowait(timer, begin);
  timer->start[id] = begin;
}

/* End a region and any trailing nowait child with one shared timestamp. */
static void stop_at(region_timer *timer, int id, double end)
{
  timer->elapsed[id] += end - timer->start[id];
  if (timer->pending_nowait == id) timer->pending_nowait = -1;
  if (timer->pending_nowait >= 0 &&
      timer->regions[timer->pending_nowait].parent == id)
    finish_nowait(timer, end);
}

/* Read one start timestamp only when region reporting is active. */
void region_timer_start(region_timer *timer, int id)
{
  if (timer->active) start_at(timer, id, omp_get_wtime());
}

/* Read one end timestamp only when region reporting is active. */
void region_timer_stop(region_timer *timer, int id)
{
  if (timer->active) stop_at(timer, id, omp_get_wtime());
}

/* Take a sample regardless of report activation, reusing its start for totals. */
double region_timer_sample_begin(region_timer *timer, int id)
{
  double begin = omp_get_wtime();
  if (timer->active) start_at(timer, id, begin);
  return begin;
}

/* Return a single sample and reuse its endpoint for active accumulated totals. */
double region_timer_sample_end(region_timer *timer, int id, double begin)
{
  double end = omp_get_wtime();
  if (timer->active) stop_at(timer, id, end);
  return end - begin;
}

/* Defer a nowait region's end until its next region or synchronization boundary. */
void region_timer_nowait_start(region_timer *timer, int id)
{
  if (timer->active) {
    region_timer_start(timer, id);
    timer->pending_nowait = id;
  }
}

/* Read an explicit synchronization timestamp only if a nowait region is pending. */
void region_timer_sync(region_timer *timer)
{
  if (timer->active && timer->pending_nowait >= 0)
    finish_nowait(timer, omp_get_wtime());
}

/* Read a region's accumulated report time. */
double region_timer_read(const region_timer *timer, int id)
{
  return timer->elapsed[id];
}

/* Read accumulated total-window time. */
double region_timer_total(const region_timer *timer)
{
  return timer->total_elapsed;
}

/* Express region time as a percentage of total windows, including empty reports. */
static double step_percent(const region_timer *timer, double seconds)
{
  return timer->total_elapsed > 0 ? 100.0 * seconds / timer->total_elapsed : 0.0;
}

/* Retain the existing parallel/for report format without reading time. */
void region_timer_report(const region_timer *timer)
{
  if (!timer->enabled) return;
  puts("\ntime report\nunit: seconds");
  printf("iteration total: %.9f s\n", timer->total_elapsed);
  puts("step %: accumulated region time / iteration total (average time-step basis)");
  for (int p = 0; p < timer->region_count; ++p) {
    const timer_region_info *region = &timer->regions[p];
    if (region->parent != -1 || timer->elapsed[p] == 0) continue;
    printf("parallel region %s  %.9f s  step: %.3f%%\n", region->name,
           timer->elapsed[p], step_percent(timer, timer->elapsed[p]));
    if (region->combined)
      printf("    for region %s  %.9f s  step: %.3f%%\n", region->name,
             timer->elapsed[p], step_percent(timer, timer->elapsed[p]));
    for (int f = 0; f < timer->region_count; ++f)
      if (timer->regions[f].parent == p && timer->elapsed[f] != 0)
        printf("    for region %s  %.9f s  step: %.3f%%\n", timer->regions[f].name,
               timer->elapsed[f], step_percent(timer, timer->elapsed[f]));
  }
}
