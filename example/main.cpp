#include "../framework/tuner/tuner.h"
#include "../framework/region_control/region_control.h"
#include "region_metadata.h"

#include <cassert>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <omp.h>
#include <vector>

static int env_positive(const char *name, int fallback) {
  const char *value = std::getenv(name);
  if (!value || !*value) return fallback;
  char *end = nullptr;
  errno = 0;
  long number = std::strtol(value, &end, 10);
  if (errno || end == value || *end || number < 1 || number >= INT_MAX) {
    std::fprintf(stderr, "%s must be a positive integer below %d\n", name, INT_MAX);
    std::exit(EXIT_FAILURE);
  }
  return static_cast<int>(number);
}

/* 只显示首两步和最后一步，以及当前 parallel 使用的线程数。 */
static void trace(int step, int steps, int region) {
  if (step <= 2 || step == steps)
    std::printf("step=%d region=%d threads=%d\n", step, region, omp_get_max_threads());
}

/* 三个 parallel region 每步各执行一次，最后一个包含尾部 nowait for。 */
int main() {
  const int count = env_positive("EXAMPLE_SIZE", 8 * 1024 * 1024);
  const int steps = env_positive("EXAMPLE_STEPS", 32);
  std::printf("example size=%d steps=%d\n", count, steps);
  std::vector<double> a(count, 1), b(count), c(count);
  static region_info regions[] = {REGION_INFO(0, -1, 1, 0),
                                  REGION_INFO(1, -1, 1, 0),
                                  REGION_INFO(2, -1, 0, 0),
                                  REGION_INFO(3, 2, 0, 1)};
  region_control control;
  region_control_init(&control, regions, 4, REGION_INSTRUMENT);
  tuner *runtime = tuner_attach(&control);

  iteration_start(&control);
  for (int step = 1; step <= steps; ++step) {
    step_start(&control, step);

    PARALLEL_START(&control, 0);
    #pragma omp parallel for
    for (int i = 0; i < count; ++i)
      b[i] = a[i] + 1;
    PARALLEL_END(&control, 0);
    trace(step, steps, 0);

    PARALLEL_START(&control, 1);
    #pragma omp parallel for
    for (int i = 0; i < count; ++i)
      a[i] = b[i] + 1;
    PARALLEL_END(&control, 1);
    trace(step, steps, 1);

    PARALLEL_START(&control, 2);
    #pragma omp parallel
    {
      FOR_START(&control, 3);
      #pragma omp for nowait
      for (int i = 0; i < count; ++i)
        c[i] = a[i] + b[i];
    }
    FOR_END(&control, 3);  // 尾部 nowait 在 parallel join 后结束。
    PARALLEL_END(&control, 2);
    trace(step, steps, 2);

  }
  iteration_end(&control);

  for (int i = 0; i < count; ++i) {
    assert(a[i] == 1.0 + 2.0 * steps);
    assert(b[i] == 2.0 * steps);
    assert(c[i] == 1.0 + 4.0 * steps);
  }
  region_report(&control);
  std::printf("example=PASS steps=%d regions=4\n", steps);
  tuner_detach(runtime);
}
