#ifndef OTTER_H
#define OTTER_H

#include "../hams/hams_binding.h"

struct otter;

struct otter_options {
  double threshold_fraction = 0.10;
  int golden_distance = 0;  // 0 selects ceil(max_threads / 8).
  bool placement_supported = true;
  int verbose = 0;  // Runtime reads OTTER_VERBOSE (default 1); pure policy stays quiet.
};

/* Pure policy: one search and configuration for an entire iteration.
 * CPU IDs must be strictly increasing, in [0, HAMS_CPU_COUNT), with
 * 1 <= max_threads <= cpu_count.
 * threshold_fraction is in [0, 1], golden_distance in [0, max_threads]. */
otter *otter_create(int max_threads, const int *cpus, int cpu_count,
                    const otter_options &options = {});

/* Select once at step_start, including after tuning. The caller applies it
 * once through runtime; every region in the iteration uses that configuration. */
/* step is the application's step ID, used only for search diagnostics. */
const hams_binding_cfg *otter_select_cfg(otter *tuner, int step = 0);

/* Consume the whole iteration's elapsed seconds once; exclude warmup samples. */
void otter_observe(otter *tuner, double seconds);

/* Optionally report the last selected configuration once, then release state.
 * Does not apply a pending choice; a never-selected tuner produces no report. */
void otter_destroy(otter *tuner, bool report = true);

#endif
