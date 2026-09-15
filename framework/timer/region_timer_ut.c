#include "region_timer.h"
#include <assert.h>
#include <omp.h>
#include <stdio.h>

static int reads;

/* Test-only clock: exact timestamps and primary-thread access. */
double __wrap_omp_get_wtime(void)
{
  assert(omp_get_thread_num() == 0);
  return ++reads;
}

/* Verify isolated contexts, disabled reports, single samples and nowait edges. */
int main(void)
{
  const timer_region_info regions[] = {
    {"parallel", -1, 1}, {"nowait", 0, 0}, {"ordinary", 0, 0}
  };
  region_timer a, b, c;
  region_timer_init(&a, regions, 3, 1);
  region_timer_init(&b, regions, 3, 0);
  assert(reads == 0 && a.pending_nowait == -1 && !a.active);

  region_timer_begin(&a);                         /* 1 */
  double begin = region_timer_sample_begin(&a, 0); /* 2 */
  region_timer_nowait_start(&a, 1);                /* 3 */
  assert(region_timer_sample_end(&a, 0, begin) == 2.0); /* 4 */
  region_timer_end(&a);                           /* 5 */
  assert(region_timer_read(&a, 0) == 2.0);
  assert(region_timer_read(&a, 1) == 1.0 && a.pending_nowait == -1);
  assert(region_timer_total(&a) == 4.0 && !a.active);
  begin = region_timer_sample_begin(&a, 0);        /* 6 */
  assert(region_timer_sample_end(&a, 0, begin) == 1.0); /* 7 */
  assert(region_timer_read(&a, 0) == 2.0);  /* Sampling outside the report window. */

  region_timer_begin(&b);                         /* 8 */
  region_timer_start(&b, 0);
  region_timer_stop(&b, 0);
  region_timer_nowait_start(&b, 1);
  region_timer_sync(&b);
  assert(reads == 8 && !b.active);
  begin = region_timer_sample_begin(&b, 0);        /* 9 */
  assert(region_timer_sample_end(&b, 0, begin) == 1.0); /* 10 */
  region_timer_end(&b);                           /* 11 */
  assert(region_timer_read(&b, 0) == 0.0 && region_timer_total(&b) == 3.0);
  assert(region_timer_read(&a, 0) == 2.0 && region_timer_total(&a) == 4.0);

  region_timer_init(&c, regions, 3, 1);
  region_timer_begin(&c);                         /* 12 */
  region_timer_start(&c, 0);                       /* 13 */
  region_timer_nowait_start(&c, 1);                /* 14 */
  region_timer_start(&c, 2);                       /* 15: closes nowait */
  region_timer_stop(&c, 2);                        /* 16 */
  region_timer_nowait_start(&c, 1);                /* 17 */
  region_timer_sync(&c);                          /* 18 */
  region_timer_sync(&c);                          /* No pending region, no read. */
  region_timer_stop(&c, 0);                        /* 19 */
  region_timer_end(&c);                           /* 20 */
  assert(reads == 20 && region_timer_total(&c) == 8.0);
  assert(region_timer_read(&c, 0) == 6.0 && region_timer_read(&c, 1) == 2.0);
  assert(region_timer_read(&c, 2) == 1.0);

  region_timer_init(&c, NULL, 0, 0);  /* A build without any instrumented regions. */
  region_timer_begin(&c);                         /* 21 */
  region_timer_end(&c);                           /* 22 */
  region_timer_end(&c);                          /* An ended window does not reread. */
  assert(region_timer_total(&c) == 1.0 && reads == 22);
  region_timer_report(&a);
  region_timer_report(&b);
  assert(reads == 22);
  puts("region_timer=PASS (contexts, sample/report, nowait, total-only, no globals)");
}
