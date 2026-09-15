#include "../framework/j2025/j2025.h"
#include "../framework/timer/region_timer.h"

#include <cassert>
#include <cstdio>
#include <omp.h>
#include <vector>

/* 只显示首两步和最后一步，线程数由 control 已应用的配置决定。 */
static void trace(int step, int region, double seconds) {
  if (step < 2 || step == 31)
    std::printf("step=%d region=%d threads=%d time_us=%.3f\n",
                step, region, omp_get_max_threads(), seconds * 1e6);
}

/* 独立示例：两个有数据依赖的 region 每步各执行一次，timer 独立提供样本。 */
int main() {
  constexpr int count = 32768;
  std::vector<double> a(count, 1), b(count);
  const timer_region_info regions[] = {{"prepare", -1, 1}, {"update", -1, 1}};
  region_timer timer;
  region_timer_init(&timer, regions, 2, 1);

  region_control *control = region_control_create();
  j2025 *tuner = j2025_create(2, region_control_max_threads(control));
  region_control_register(control, j2025_callbacks(), tuner);

  region_timer_begin(&timer);
  for (int step = 0; step < 32; ++step) {
    control_step_start(control, step);

    control_region_start(control, 0);
    double begin = region_timer_sample_begin(&timer, 0);
#pragma omp parallel for
    for (int i = 0; i < count; ++i)
      b[i] = a[i] + 1;
    double elapsed = region_timer_sample_end(&timer, 0, begin);
    control_region_finish(control, 0, elapsed);
    trace(step, 0, elapsed);

    control_region_start(control, 1);
    begin = region_timer_sample_begin(&timer, 1);
#pragma omp parallel for
    for (int i = 0; i < count; ++i)
      a[i] = b[i] + 1;
    elapsed = region_timer_sample_end(&timer, 1, begin);
    control_region_finish(control, 1, elapsed);
    trace(step, 1, elapsed);

    control_step_finish(control, step);
  }
  region_timer_end(&timer);

  for (int i = 0; i < count; ++i) {
    assert(a[i] == 65);
    assert(b[i] == 64);
  }
  region_timer_report(&timer);
  std::puts("example=PASS steps=32 regions=2");
  region_control_destroy(control);
  j2025_destroy(tuner);
}
