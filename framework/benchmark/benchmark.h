#ifndef FRAMEWORK_BENCHMARK_H
#define FRAMEWORK_BENCHMARK_H

#include "region_control.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Shared application context. Only application sources are instrumented. */
extern region_control benchmark_control;

/* Call before warmup or any instrumented region. Windows and logical steps
 * remain explicit iteration_start/end and step_start/end calls in the app. */
void benchmark_init(region_info *regions, int count);

/* Call after the last measurement window, before application teardown. */
void benchmark_finish(void);

#ifdef __cplusplus
}
#endif

#endif
