#ifndef NPB_REGION_TIMERS_H
#define NPB_REGION_TIMERS_H

#include <omp.h>
#ifndef NPB_REGION_TIMING
#define NPB_REGION_TIMING 1
#endif
#define NPB_TIME_MASTER _Pragma("omp master")

#define NPB_MAX_REGIONS 256
/* Defined by Python's generated region table. parent=-1: parallel. */
typedef struct {
  const char *name;
  int parent, combined;
} npb_region_info;
extern const npb_region_info npb_regions[];
extern const int npb_region_count;
extern int npb_time_active;

int npb_time_enabled(void);
void npb_time_begin(void);
void npb_time_end(void);
void npb_time_start(int id);
void npb_time_stop(int id);
void npb_time_nowait_start(int id);
void npb_time_sync(void);
double npb_time_read(int id);
double npb_time_total(void);
void npb_time_report(void);

/* Fixed ID; one pair for a nowait chain, ending before the ordinary for.
 * No semicolons after paired macros. Only master reads the clock.
 */
#define NPB_PARALLEL_BEGIN(id) \
  { const int npb_region_id_ = (id); \
    if (npb_time_active) npb_time_start(npb_region_id_);
#define NPB_PARALLEL_END() \
    if (npb_time_active) npb_time_stop(npb_region_id_); }
#define NPB_PARALLEL_FOR_BEGIN(id) NPB_PARALLEL_BEGIN(id)
#define NPB_PARALLEL_FOR_END() NPB_PARALLEL_END()
#define NPB_FOR_BEGIN(id) \
  { const int npb_region_id_ = (id); \
    if (npb_time_active) { NPB_TIME_MASTER npb_time_start(npb_region_id_); }
#define NPB_FOR_END() \
    if (npb_time_active) { NPB_TIME_MASTER npb_time_stop(npb_region_id_); } }

#endif
