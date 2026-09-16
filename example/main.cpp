#include "../framework/tuner/tuner.h"
#include "../framework/region_control/region_control.h"
#include "region_metadata.h"

#include <cassert>
#include <cstdio>
#include <omp.h>
#include <vector>

/* 只显示首两步和最后一步，以及当前 parallel 使用的线程数。 */
static void trace(int step, int region) {
  if (step <= 2 || step == 32)
    std::printf("step=%d region=%d threads=%d\n", step, region, omp_get_max_threads());
}

/* 两个 region 每步各执行一次；control 内置计时，环境变量选择 tuner。 */
int main() {
  constexpr int count = 32768;
  std::vector<double> a(count, 1), b(count);
  static region_info regions[] = {REGION_INFO(0, -1, 1, 0),
                                  REGION_INFO(1, -1, 1, 0)};
  region_control control;
  region_control_init(&control, regions, 2, REGION_INSTRUMENT);
  tuner *runtime = tuner_attach(&control);

  iteration_start(&control);
  for (int step = 1; step <= 32; ++step) {
    step_start(&control, step);

    PARALLEL_START(&control, 0);
    #pragma omp parallel for
    for (int i = 0; i < count; ++i)
      b[i] = a[i] + 1;
    PARALLEL_END(&control, 0);
    trace(step, 0);

    PARALLEL_START(&control, 1);
    #pragma omp parallel for
    for (int i = 0; i < count; ++i)
      a[i] = b[i] + 1;
    PARALLEL_END(&control, 1);
    trace(step, 1);

  }
  iteration_end(&control);

  for (int i = 0; i < count; ++i) {
    assert(a[i] == 65);
    assert(b[i] == 64);
  }
  region_report(&control);
  std::puts("example=PASS steps=32 regions=2");
  tuner_detach(runtime);
}
