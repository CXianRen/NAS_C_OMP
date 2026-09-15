#ifndef REGION_TIMER_H
#define REGION_TIMER_H

#define REGION_TIMER_MAX_REGIONS 256

/* parent=-1 denotes a parallel region; combined mirrors its time as a for row. */
typedef struct {
  const char *name;
  int parent, combined;
} timer_region_info;

/* Caller-owned context; use each context on one coordinating thread. */
typedef struct {
  const timer_region_info *regions;
  int region_count;
  int enabled, active, running, pending_nowait;
  double total_start, total_elapsed;
  double start[REGION_TIMER_MAX_REGIONS], elapsed[REGION_TIMER_MAX_REGIONS];
} region_timer;

#ifdef __cplusplus
extern "C" {
#endif

/* Clear state and retain caller-owned metadata; enabled controls report totals. */
void region_timer_init(region_timer *timer, const timer_region_info *regions,
                       int count, int enabled);
/* Begin the total timing window and activate enabled region accumulation. */
void region_timer_begin(region_timer *timer);
/* End and accumulate the total window, then deactivate region accumulation. */
void region_timer_end(region_timer *timer);
/* Start an ordinary region, only while reporting is active. */
void region_timer_start(region_timer *timer, int id);
/* Stop an ordinary region and its trailing nowait child at one timestamp. */
void region_timer_stop(region_timer *timer, int id);
/* Start a nowait region whose end will be supplied by a later boundary. */
void region_timer_nowait_start(region_timer *timer, int id);
/* End a pending nowait region at an explicit synchronization boundary. */
void region_timer_sync(region_timer *timer);
/* Read a sample start even with reports disabled; reuse it for active totals. */
double region_timer_sample_begin(region_timer *timer, int id);
/* Return one sample and reuse its end timestamp for active region totals. */
double region_timer_sample_end(region_timer *timer, int id, double begin);
/* Read an accumulated region time without accessing the clock. */
double region_timer_read(const region_timer *timer, int id);
/* Read the accumulated total window time without accessing the clock. */
double region_timer_total(const region_timer *timer);
/* Print enabled parallel/for totals and percentages of the total window. */
void region_timer_report(const region_timer *timer);

#ifdef __cplusplus
}
#endif

#endif
