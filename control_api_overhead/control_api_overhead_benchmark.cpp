#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "hams_binding.h"
#include "region_control.h"
#include "tuner.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <omp.h>
#include <sched.h>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

constexpr int maximum_threads = 64;
constexpr int transition_repetitions = 3000;
using clock_type = std::chrono::steady_clock;

enum class layout { close, spread };

[[noreturn]] void fail(const char *message)
{
  std::fprintf(stderr, "control-api-overhead: %s\n", message);
  std::exit(EXIT_FAILURE);
}

hams_binding_cfg make_config(int threads, layout placement)
{
  hams_binding_cfg cfg{};
  cfg.thread_number = threads;
  for (int tid = 0; tid < threads; ++tid) {
    int cpu = placement == layout::close
                  ? tid
                  : tid * maximum_threads / threads;
    cfg.mask[cpu] = true;
    cfg.tid_to_cpu[tid] = cpu;
  }
  return cfg;
}

layout parse_layout(const char *value)
{
  if (!std::strcmp(value, "close")) return layout::close;
  if (!std::strcmp(value, "spread")) return layout::spread;
  fail("layout must be close or spread");
}

int parse_threads(const char *value, bool allow_zero)
{
  char *end = nullptr;
  long parsed = std::strtol(value, &end, 10);
  int minimum = allow_zero ? 0 : 1;
  if (end == value || *end || parsed < minimum || parsed > maximum_threads)
    fail("thread count is outside the supported range");
  return static_cast<int>(parsed);
}

struct temporary_config {
  std::string path;

  explicit temporary_config(const char *contents)
  {
    char pattern[] = "/tmp/phams-control-api-XXXXXX";
    int descriptor = mkstemp(pattern);
    if (descriptor < 0) fail("cannot create temporary Offline config");
    path = pattern;
    std::size_t length = std::strlen(contents);
    FILE *file = fdopen(descriptor, "w");
    if (!file) {
      close(descriptor);
      unlink(path.c_str());
      fail("cannot open temporary Offline config");
    }
    bool write_failed = std::fwrite(contents, 1, length, file) != length;
    bool close_failed = std::fclose(file) != 0;
    if (write_failed || close_failed) {
      unlink(path.c_str());
      fail("cannot write temporary Offline config");
    }
  }

  ~temporary_config() { unlink(path.c_str()); }

  temporary_config(const temporary_config &) = delete;
  temporary_config &operator=(const temporary_config &) = delete;
};

double quantile(const std::vector<double> &sorted, double fraction)
{
  std::size_t index =
      static_cast<std::size_t>(fraction * (sorted.size() - 1));
  return sorted[index];
}

void report(const char *name, std::vector<double> samples)
{
  std::sort(samples.begin(), samples.end());
  double sum = std::accumulate(samples.begin(), samples.end(), 0.0);
  std::size_t trim = samples.size() / 20;
  double trimmed = std::accumulate(samples.begin() + trim,
                                   samples.end() - trim, 0.0) /
                   static_cast<double>(samples.size() - 2 * trim);
  std::printf("%-24s n=%zu mean_us=%.3f trim_us=%.3f min_us=%.3f "
              "p50_us=%.3f p90_us=%.3f p99_us=%.3f max_us=%.3f\n",
              name, samples.size(), sum / samples.size(), trimmed,
              samples.front(), quantile(samples, 0.50),
              quantile(samples, 0.90), quantile(samples, 0.99),
              samples.back());
}

void native_apply(const hams_binding_cfg &cfg)
{
  omp_set_dynamic(0);
  omp_set_num_threads(cfg.thread_number);
#pragma omp parallel num_threads(cfg.thread_number)
  {
    int tid = omp_get_thread_num();
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(cfg.tid_to_cpu[tid], &mask);
    int result = sched_setaffinity(0, sizeof(mask), &mask);
    assert(result == 0);
    (void)result;
  }
}

double native_apply_us(const hams_binding_cfg &cfg)
{
  auto begin = clock_type::now();
  native_apply(cfg);
  auto end = clock_type::now();
  return std::chrono::duration<double, std::micro>(end - begin).count();
}

double hams_apply_us(hams_binding *binding, const hams_binding_cfg &cfg)
{
  auto begin = clock_type::now();
  hams_binding_apply(binding, &cfg);
  auto end = clock_type::now();
  return std::chrono::duration<double, std::micro>(end - begin).count();
}

__attribute__((noinline)) void native_boundary()
{
  asm volatile("" ::: "memory");
}

void control_region(region_control *control, int id)
{
  region_parallel_start(control, id, __FILE__, __func__, __LINE__);
  region_parallel_end(control, id);
}

struct tuner_fixture {
  region_info regions[2]{{"A", -1, 0, "demo.cpp", 10, 0},
                         {"B", -1, 0, "demo.cpp", 20, 0}};
  region_control control{};
  tuner *runtime = nullptr;

  tuner_fixture(const char *config_path, int auxiliary_threads,
                const char *auxiliary_binding)
  {
    setenv("REGION_TIME_REPORT", "0", 1);
    setenv("OFFLINE_CONFIG", config_path, 1);
    std::string threads = std::to_string(auxiliary_threads);
    setenv("PHAMS_AUX_NUM_THREADS", threads.c_str(), 1);
    setenv("PHAMS_AUX_PROC_BIND", auxiliary_binding, 1);
    omp_set_dynamic(0);
    omp_set_num_threads(maximum_threads);
    region_control_init(&control, regions, 2);
    runtime = tuner_attach_named(&control, "offline");
    iteration_start(&control);
    step_start(&control, 0);
  }

  ~tuner_fixture()
  {
    step_end(&control, 0);
    iteration_end(&control);
    tuner_detach(runtime);
  }
};

void measure_native_hit()
{
  constexpr int batches = 100;
  constexpr int calls_per_batch = 10000;
  std::vector<double> samples;
  samples.reserve(batches);
  for (int batch = 0; batch < batches; ++batch) {
    auto begin = clock_type::now();
    for (int call = 0; call < calls_per_batch; ++call) native_boundary();
    auto end = clock_type::now();
    samples.push_back(
        std::chrono::duration<double, std::micro>(end - begin).count() /
        calls_per_batch);
  }
  report("native_hit", std::move(samples));
}

void measure_tuner_hit(const char *config_path)
{
  tuner_fixture fixture(config_path, 32, "close");
  control_region(&fixture.control, 1);
  constexpr int batches = 100;
  constexpr int calls_per_batch = 10000;
  std::vector<double> samples;
  samples.reserve(batches);
  for (int batch = 0; batch < batches; ++batch) {
    auto begin = clock_type::now();
    for (int call = 0; call < calls_per_batch; ++call)
      control_region(&fixture.control, 1);
    auto end = clock_type::now();
    samples.push_back(
        std::chrono::duration<double, std::micro>(end - begin).count() /
        calls_per_batch);
  }
  report("control_hit_32C", std::move(samples));
}

void measure_native_transition(const char *name,
                               const hams_binding_cfg &from,
                               const hams_binding_cfg &to)
{
  native_apply(make_config(maximum_threads, layout::close));
  native_apply(from);
  for (int iteration = 0; iteration < 20; ++iteration) {
    native_apply(to);
    native_apply(from);
  }
  std::vector<double> samples;
  samples.reserve(transition_repetitions);
  for (int iteration = 0; iteration < transition_repetitions; ++iteration) {
    samples.push_back(native_apply_us(to));
    native_apply(from);
  }
  report(name, std::move(samples));
}

void measure_tuner_transition(const char *name, const char *config_path,
                              int auxiliary_threads,
                              const char *auxiliary_binding, int from_id,
                              int to_id)
{
  tuner_fixture fixture(config_path, auxiliary_threads, auxiliary_binding);
  control_region(&fixture.control, from_id);
  for (int iteration = 0; iteration < 20; ++iteration) {
    control_region(&fixture.control, to_id);
    control_region(&fixture.control, from_id);
  }
  std::vector<double> samples;
  samples.reserve(transition_repetitions);
  for (int iteration = 0; iteration < transition_repetitions; ++iteration) {
    auto begin = clock_type::now();
    control_region(&fixture.control, to_id);
    auto end = clock_type::now();
    samples.push_back(
        std::chrono::duration<double, std::micro>(end - begin).count());
    control_region(&fixture.control, from_id);
  }
  report(name, std::move(samples));
}

void run_comparison(bool reverse, const char *count_path,
                    const char *layout_path)
{
  const auto c32 = make_config(32, layout::close);
  const auto c64 = make_config(64, layout::close);
  const auto s32 = make_config(32, layout::spread);

  if (reverse) {
    measure_tuner_hit(count_path);
    measure_native_hit();
  } else {
    measure_native_hit();
    measure_tuner_hit(count_path);
  }

  auto compare = [&](const char *native_name, const char *control_name,
                     const hams_binding_cfg &from,
                     const hams_binding_cfg &to, const char *path,
                     int auxiliary_threads, const char *auxiliary_binding,
                     int from_id, int to_id) {
    if (reverse) {
      measure_tuner_transition(control_name, path, auxiliary_threads,
                               auxiliary_binding, from_id, to_id);
      measure_native_transition(native_name, from, to);
    } else {
      measure_native_transition(native_name, from, to);
      measure_tuner_transition(control_name, path, auxiliary_threads,
                               auxiliary_binding, from_id, to_id);
    }
  };

  compare("native_64C_to_32C", "control_64C_to_32C", c64, c32,
          count_path, 64, "close", 0, 1);
  compare("native_32C_to_64C", "control_32C_to_64C", c32, c64,
          count_path, 32, "close", 1, 0);
  compare("native_32C_to_32S", "control_32C_to_32S", c32, s32,
          layout_path, 32, "close", 0, 1);
  compare("native_32S_to_32C", "control_32S_to_32C", s32, c32,
          layout_path, 32, "spread", 1, 0);
}

void run_single(bool native, int from_threads, layout from_layout,
                int to_threads, layout to_layout, const char *label)
{
  hams_binding *binding = native ? nullptr : hams_binding_create();
  if (native) {
    // Match the OpenMP runtime queries performed by hams_binding_create().
    (void)omp_get_thread_limit();
    (void)omp_get_proc_bind();
    (void)omp_get_max_threads();
  }
  if (from_threads) {
    auto from = make_config(from_threads, from_layout);
    if (native)
      native_apply(from);
    else
      hams_binding_apply(binding, &from);
  }
  auto to = make_config(to_threads, to_layout);
  double elapsed = native ? native_apply_us(to) : hams_apply_us(binding, to);
  std::printf("%s %.3f\n", label, elapsed);
  hams_binding_destroy(binding);
}

void usage(const char *program)
{
  std::fprintf(stderr,
               "usage:\n"
               "  %s compare [reverse]\n"
               "  %s single native|hams FROM_THREADS FROM_LAYOUT "
               "TO_THREADS TO_LAYOUT LABEL\n",
               program, program);
}

}  // namespace

int main(int argc, char **argv)
{
  if (argc >= 2 && !std::strcmp(argv[1], "compare")) {
    bool reverse = argc == 3 && !std::strcmp(argv[2], "reverse");
    if (argc > 3 || (argc == 3 && !reverse)) {
      usage(argv[0]);
      return 2;
    }
    temporary_config count_config(
        "A:10;64;0xffffffffffffffff\n"
        "B:20;32;0xffffffff\n");
    temporary_config layout_config(
        "A:10;32;0xffffffff\n"
        "B:20;32;0x5555555555555555\n");
    run_comparison(reverse, count_config.path.c_str(),
                   layout_config.path.c_str());
    return 0;
  }

  if (argc == 8 && !std::strcmp(argv[1], "single")) {
    bool native = !std::strcmp(argv[2], "native");
    if (!native && std::strcmp(argv[2], "hams")) {
      usage(argv[0]);
      return 2;
    }
    int from_threads = parse_threads(argv[3], true);
    layout from_layout = parse_layout(argv[4]);
    int to_threads = parse_threads(argv[5], false);
    layout to_layout = parse_layout(argv[6]);
    run_single(native, from_threads, from_layout, to_threads, to_layout,
               argv[7]);
    return 0;
  }

  usage(argv[0]);
  return 2;
}
