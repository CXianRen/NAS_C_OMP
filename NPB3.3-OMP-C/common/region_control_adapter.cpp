#include "region_control_adapter.h"
#include "../../framework/region_control/region_control.h"
#include "../../framework/j2025/j2025.h"

#include <cassert>
#include <cstdlib>

static region_control *control;
static j2025 *tuner;
static bool in_step;

/* Release only algorithm/controller memory after the application has finished. */
static void release_control()
{
  region_control_destroy(control);
  j2025_destroy(tuner);
}

/* Capture the initial thread limit once; a step boundary is only a notification. */
void npb_control_step_start(int step)
{
  assert(!in_step);
  if (!control) {
    control = region_control_create();
    tuner = j2025_create(npb_region_count, region_control_max_threads(control));
    region_control_register(control, j2025_callbacks(), tuner);
    int result = std::atexit(release_control);
    assert(result == 0);
    (void)result;
  }
  in_step = true;
  control_step_start(control, step);
}

/* Preserve all per-region search states across successive iterations. */
void npb_control_step_finish(int step)
{
  assert(in_step);
  control_step_finish(control, step);
  in_step = false;
}

/* Exclude application initialization, native warmup and verification. */
int npb_control_active(void)
{
  return in_step;
}

/* The generic controller applies the selected cfg through its shared binding. */
void npb_control_region_start(int id)
{
  assert(in_step);
  control_region_start(control, id);
}

/* Timing supplies a single invocation's elapsed time, independently of reports. */
void npb_control_region_finish(int id, double seconds)
{
  assert(in_step);
  control_region_finish(control, id, seconds);
}
