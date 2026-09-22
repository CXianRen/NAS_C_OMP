#include "benchmark.h"
#include "../tuner/tuner.h"
#include <cassert>

region_control benchmark_control;
static tuner *application_tuner;
static bool initialized;

void benchmark_init(region_info *regions, int count)
{
  assert(!initialized);
  region_control_init(&benchmark_control, regions, count);
  application_tuner = tuner_attach(&benchmark_control);
  initialized = true;
}

void benchmark_finish(void)
{
  assert(initialized && !benchmark_control.running);
  region_report(&benchmark_control);
  tuner_detach(application_tuner);
  application_tuner = nullptr;
  initialized = false;
}
