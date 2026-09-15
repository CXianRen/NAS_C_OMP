#include "region_timers.h"
#include <stdlib.h>
#include <string.h>

static region_timer timer;
static int initialized;
#if NPB_REGION_TIMING
int npb_time_active;
#endif

/* Keep NPB's environment and generated table in the application adapter. */
static void initialize(void)
{
  if (initialized) return;
#if NPB_REGION_TIMING
  const char *v = getenv("NPB_TIME_REPORT");
  int enabled = v && (!strcmp(v, "1") || !strcmp(v, "true") ||
                     !strcmp(v, "yes") || !strcmp(v, "on"));
  region_timer_init(&timer, npb_regions, npb_region_count, enabled);
#else
  region_timer_init(&timer, NULL, 0, 0);
#endif
  initialized = 1;
}

/* Query NPB report activation without reading a clock. */
#if NPB_REGION_TIMING
int npb_time_enabled(void)
{
  initialize();
  return timer.enabled;
}
#endif

/* Preserve the application's original formal-iteration timing window. */
void npb_time_begin(void)
{
  initialize();
  region_timer_begin(&timer);
#if NPB_REGION_TIMING
  npb_time_active = timer.active;
#endif
}

/* Close the existing total window and disable subsequent report accumulation. */
void npb_time_end(void)
{
  region_timer_end(&timer);
#if NPB_REGION_TIMING
  npb_time_active = 0;
#endif
}

#if NPB_REGION_TIMING
/* Forward an ordinary region's start to the shared timer. */
void npb_time_start(int id) { region_timer_start(&timer, id); }

/* Forward an ordinary region's end, including its trailing nowait child. */
void npb_time_stop(int id) { region_timer_stop(&timer, id); }

/* Take a tuning sample even when report output is disabled. */
double npb_time_sample_begin(int id)
{
  return region_timer_sample_begin(&timer, id);
}

/* Return one invocation's elapsed time, using the same report timestamps. */
double npb_time_sample_end(int id, double begin)
{
  return region_timer_sample_end(&timer, id, begin);
}

/* Keep a nowait chain pending until its existing synchronization boundary. */
void npb_time_nowait_start(int id) { region_timer_nowait_start(&timer, id); }

/* Close a pending nowait chain at an explicit barrier. */
void npb_time_sync(void) { region_timer_sync(&timer); }

/* Read a region's accumulated time without taking a new sample. */
double npb_time_read(int id)
{
  initialize();
  return region_timer_read(&timer, id);
}
#endif

/* Read the original benchmark total, including all formal timing windows. */
double npb_time_total(void) { return region_timer_total(&timer); }

/* Preserve the existing NPB report format through the shared timer. */
void npb_time_report(void)
{
  initialize();
  region_timer_report(&timer);
}
