#ifndef NPB_REGION_CONTROL_ADAPTER_H
#define NPB_REGION_CONTROL_ADAPTER_H

#include "region_timers.h"

#ifndef NPB_REGION_CONTROL
#define NPB_REGION_CONTROL 0
#endif

#if NPB_REGION_CONTROL
#ifdef __cplusplus
extern "C" {
#endif
/* Notify the algorithm of a formal step; neither function reads the clock. */
void npb_control_step_start(int step);
void npb_control_step_finish(int step);
/* Only regions inside an explicitly marked step are controlled. */
int npb_control_active(void);
/* Select/apply a configuration, then feed back the independent timer's sample. */
void npb_control_region_start(int id);
void npb_control_region_finish(int id, double seconds);
#ifdef __cplusplus
}
#endif

/* Adapt the existing outer-region hooks; internal for/nowait hooks are unchanged. */
#undef NPB_PARALLEL_BEGIN
#undef NPB_PARALLEL_END
#define NPB_PARALLEL_BEGIN(id) \
  { const int npb_region_id_ = (id); \
    const int npb_controlled_ = npb_control_active(); \
    const int npb_sample_ = npb_time_active || npb_controlled_; \
    double npb_sample_begin_ = 0.0; \
    if (npb_controlled_) npb_control_region_start(npb_region_id_); \
    if (npb_sample_) npb_sample_begin_ = npb_time_sample_begin(npb_region_id_);
#define NPB_PARALLEL_END() \
    if (npb_sample_) { \
      double npb_seconds_ = npb_time_sample_end(npb_region_id_, npb_sample_begin_); \
      if (npb_controlled_) npb_control_region_finish(npb_region_id_, npb_seconds_); \
    } }
#else
/* Baseline builds keep the step annotations without linking the controller. */
static inline void npb_control_step_start(int step) { (void)step; }
static inline void npb_control_step_finish(int step) { (void)step; }
#endif

#endif
