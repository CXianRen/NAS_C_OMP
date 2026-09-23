#include "region_control/region_control.h"
#include "tuner/tuner.h"

#include <cerrno>
#include <cinttypes>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <omp.h>
#include <string>
#include <vector>

#if !REGION_INSTRUMENT
#error "Build the control-layer comparison with REGION_INSTRUMENT=1."
#endif

namespace {

[[noreturn]] void fail(const std::string &message)
{
  std::fprintf(stderr, "ERROR %s\n", message.c_str());
  std::exit(EXIT_FAILURE);
}

int integer(const std::string &value, const char *label, int minimum = 1)
{
  if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
    fail(std::string(label) + " must be a decimal integer");
  char *end = nullptr;
  errno = 0;
  const long result = std::strtol(value.c_str(), &end, 10);
  if (errno || *end || result < minimum || result > INT_MAX)
    fail(std::string(label) + " is out of range");
  return static_cast<int>(result);
}

struct options {
  std::string mode;
  int iterations = 1000;
  int batches = 10;
  int warmup = 100;
};

options parse_options(int argc, char **argv)
{
  options o;
  for (int i = 1; i < argc; ++i) {
    const std::string name = argv[i];
    if (i + 1 == argc) fail("missing value for " + name);
    const std::string value = argv[++i];
    if (name == "--mode") o.mode = value;
    else if (name == "--iterations") o.iterations = integer(value, "iterations");
    else if (name == "--batches") o.batches = integer(value, "batches");
    else if (name == "--warmup") o.warmup = integer(value, "warmup", 0);
    else fail("unknown option " + name);
  }
  if (o.mode != "native" && o.mode != "control")
    fail("--mode must be native or control");
  return o;
}

struct alignas(64) counter {
  volatile std::uint64_t value = 0;
};
static_assert(sizeof(counter) >= 64, "each thread needs a separate cache line");

// This path contains no control hooks or hook-related master conditionals.
void native_operations(region_control *, counter *counters, int operations)
{
  for (int operation = 0; operation < operations; ++operation) {
    #pragma omp parallel
    {
      const int tid = omp_get_thread_num();
      counters[tid].value += 1;
    }
  }
}

void control_operations(region_control *control, counter *counters, int operations)
{
  for (int operation = 0; operation < operations; ++operation) {
    step_start(control, operation);
    PARALLEL_START(control, 0);
    #pragma omp parallel
    {
      // Exercise the ordinary for-hook pair without an extra worksharing
      // barrier. The timed workload remains one volatile update per worker;
      // the child-region timer measures the master's local interval only.
      FOR_START(control, 1);
      const int tid = omp_get_thread_num();
      counters[tid].value += 1;
      FOR_END(control, 1);
    }
    PARALLEL_END(control, 0);
    step_end(control, operation);
  }
}

using operation_function = void (*)(region_control *, counter *, int);

double measure(region_control *control, counter *counters,
               operation_function execute, int operations)
{
  const double before = iteration_time(control);
  // Both paths pay the same total-window boundaries. With reporting disabled
  // and no callbacks, these are the only two framework clock reads per batch.
  iteration_start(control);
  execute(control, counters, operations);
  iteration_end(control);
  return iteration_time(control) - before;
}

void verify_no_tuner(const region_control &control, tuner *runtime)
{
  const auto &callbacks = control.callbacks;
  if (runtime || control.context || callbacks.step_start || callbacks.parallel_start ||
      callbacks.parallel_end || callbacks.step_end || callbacks.step_sample)
    fail("this comparison requires TUNER=none and no registered callbacks");
}

void run(const options &o)
{
  const char *name = std::getenv("TUNER");
  if (name && *name && std::string(name) != "none")
    fail("this comparison requires TUNER=none");

  region_info regions[] = {
      {"control_bench", -1, 0, __FILE__, 1, 0},
      {"control_bench", 0, 0, __FILE__, 2, 0},
  };
  region_control control;
  region_control_init(&control, regions, 2);
  // Link and call the production attach path in both modes. none must return
  // immediately without binding validation, topology discovery, or a tuner.
  tuner *runtime = tuner_attach(&control);
  verify_no_tuner(control, runtime);

  // This query is only for counter allocation; all OpenMP ICVs and affinity
  // settings come unchanged from the invoking environment. Dynamic teams are
  // allowed, and no CPU map or actual binding is queried or validated.
  const int maximum_threads = omp_get_max_threads();
  if (maximum_threads < 1) fail("OpenMP reported no available thread slots");
  std::vector<counter> counters(static_cast<std::size_t>(maximum_threads));
  std::vector<double> steady(static_cast<std::size_t>(o.batches));
  const operation_function execute = o.mode == "control"
                                         ? control_operations : native_operations;

  const double first = measure(&control, counters.data(), execute, 1);
  if (o.warmup) measure(&control, counters.data(), execute, o.warmup);
  for (int batch = 0; batch < o.batches; ++batch)
    steady[static_cast<std::size_t>(batch)] =
        measure(&control, counters.data(), execute, o.iterations);

  const std::uint64_t operations = 1 + static_cast<std::uint64_t>(o.warmup) +
      static_cast<std::uint64_t>(o.batches) * static_cast<std::uint64_t>(o.iterations);
  const std::uint64_t main_count = counters[0].value;
  if (main_count != operations) fail("primary-thread checksum mismatch");
  for (std::size_t tid = 1; tid < counters.size(); ++tid)
    if (counters[tid].value > operations)
      fail("worker checksum exceeds the number of parallel regions");

  // No output or checksum processing occurs inside a measured window.
  std::printf("CONFIG mode=%s tuner=none report=%d startup_max_threads=%d "
              "iterations=%d batches=%d warmup=%d\n",
              o.mode.c_str(), control.enabled, maximum_threads,
              o.iterations, o.batches, o.warmup);
  std::printf("SAMPLE phase=first batch=0 operations=1 seconds=%.9f\n", first);
  for (int batch = 0; batch < o.batches; ++batch)
    std::printf("SAMPLE phase=steady batch=%d operations=%d seconds=%.9f\n",
                batch + 1, o.iterations, steady[static_cast<std::size_t>(batch)]);
  std::printf("CHECKSUM operations=%" PRIu64 " main=%" PRIu64 " status=pass\n",
              operations, main_count);
  tuner_detach(runtime);
}

}  // namespace

int main(int argc, char **argv)
{
  run(parse_options(argc, argv));
  return EXIT_SUCCESS;
}
