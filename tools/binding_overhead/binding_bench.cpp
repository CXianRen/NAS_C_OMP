#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifdef NDEBUG
#error "Build the binding comparison with assertions enabled."
#endif

#include "hams/hams_binding.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cinttypes>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <omp.h>
#include <sched.h>
#include <string>
#include <vector>

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
  std::string scenario = "fixed";
  std::string map_path;
  int threads = 0;
  int other_threads = 0;
  int iterations = 1000;
  int batches = 10;
  int warmup = 100;
  bool probe = false;
};

options parse_options(int argc, char **argv)
{
  options o;
  for (int i = 1; i < argc; ++i) {
    const std::string name = argv[i];
    if (name == "--probe") {
      o.probe = true;
      continue;
    }
    if (i + 1 == argc) fail("missing value for " + name);
    const std::string value = argv[++i];
    if (name == "--mode") o.mode = value;
    else if (name == "--scenario") o.scenario = value;
    else if (name == "--threads") o.threads = integer(value, "threads");
    else if (name == "--other-threads") o.other_threads = integer(value, "other-threads");
    else if (name == "--iterations") o.iterations = integer(value, "iterations");
    else if (name == "--batches") o.batches = integer(value, "batches");
    else if (name == "--warmup") o.warmup = integer(value, "warmup", 0);
    else if (name == "--map") o.map_path = value;
    else fail("unknown option " + name);
  }
  if (o.mode != "native" && o.mode != "framework")
    fail("--mode must be native or framework");
  if (o.scenario != "fixed" && o.scenario != "switch")
    fail("--scenario must be fixed or switch");
  if (!o.threads) fail("--threads is required");
  if (!o.other_threads) o.other_threads = o.threads;
  if (o.threads > HAMS_CPU_COUNT || o.other_threads > HAMS_CPU_COUNT)
    fail("thread count exceeds HAMS_CPU_COUNT");
  if (o.scenario == "switch") {
    if (o.threads == o.other_threads) fail("switch requires different thread counts");
    if (o.iterations % 2 || o.warmup % 2)
      fail("switch requires even iterations and warmup");
  }
  if (o.mode == "framework" && o.map_path.empty())
    fail("framework mode requires --map");
  return o;
}

std::map<int, hams_binding_cfg> read_maps(const std::string &path)
{
  std::ifstream input(path);
  if (!input) fail("cannot open map file " + path);
  std::map<int, hams_binding_cfg> maps;
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) continue;
    const auto separator = line.find(';');
    if (separator == std::string::npos) fail("map rows must use T;cpu0,cpu1,...");
    const int threads = integer(line.substr(0, separator), "map threads");
    if (threads > HAMS_CPU_COUNT || maps.count(threads))
      fail("duplicate or oversized thread count in map");
    hams_binding_cfg cfg{};
    cfg.thread_number = threads;
    std::size_t start = separator + 1;
    int previous = -1;
    for (int tid = 0; tid < threads; ++tid) {
      const auto end = line.find(',', start);
      const int cpu = integer(line.substr(start, end - start), "map CPU", 0);
      if (cpu <= previous || cpu >= HAMS_CPU_COUNT || cpu >= CPU_SETSIZE)
        fail("map CPUs must be strictly ascending and below HAMS_CPU_COUNT/CPU_SETSIZE");
      cfg.tid_to_cpu[tid] = cpu;
      cfg.mask.set(static_cast<std::size_t>(cpu));
      previous = cpu;
      if (tid + 1 == threads) {
        if (end != std::string::npos) fail("too many CPUs in map row");
      } else {
        if (end == std::string::npos) fail("too few CPUs in map row");
        start = end + 1;
      }
    }
    maps.emplace(threads, cfg);
  }
  if (!input.eof()) fail("cannot read map file " + path);
  return maps;
}

// Shared setup runs before the first sample, without launching a team.
struct backend {
  const options &o;
  int initial_threads;
  int current_threads;
  std::map<int, hams_binding_cfg> maps;
  const hams_binding_cfg *cfg_a = nullptr;
  const hams_binding_cfg *cfg_b = nullptr;
  hams_binding *binding = nullptr;

  explicit backend(const options &configuration) :
      o(configuration), initial_threads(omp_get_max_threads()),
      current_threads(initial_threads)
  {
    const int maximum = o.scenario == "switch"
                            ? std::max(o.threads, o.other_threads) : o.threads;
    const int limit = omp_get_thread_limit();
    const auto placement = omp_get_proc_bind();
    if (initial_threads != maximum)
      fail("OMP_NUM_THREADS must equal the scenario's maximum thread count");
    if (limit < maximum || omp_get_dynamic())
      fail("thread limit is too small or OMP_DYNAMIC is enabled");
    if (!o.map_path.empty()) {
      maps = read_maps(o.map_path);
      const auto a = maps.find(o.threads);
      const auto b = maps.find(o.scenario == "switch" ? o.other_threads : o.threads);
      if (a == maps.end() || b == maps.end()) fail("map file lacks a requested thread count");
      cfg_a = &a->second;
      cfg_b = &b->second;
    }
    if (o.mode == "native") {
      if (placement != omp_proc_bind_spread && placement != omp_proc_bind_close)
        fail("native mode requires OMP_PROC_BIND=spread or close");
    } else {
      binding = hams_binding_create();
      if (!binding) fail("cannot allocate production binding context");
      hams_binding_status status{};
      hams_binding_get_status(binding, &status);
      if (!status.supported || maximum > status.max_threads)
        fail("framework requires OMP_PROC_BIND=false and a sufficient thread limit");
    }
  }

  ~backend() { hams_binding_destroy(binding); }

  void apply(int threads)
  {
    if (binding) {
      hams_binding_apply(binding, threads == o.threads ? cfg_a : cfg_b);
    } else if (threads != current_threads) {
      omp_set_num_threads(threads);
      current_threads = threads;
    }
  }
};

struct affinity {
  cpu_set_t allowed{};
  int cpu = -1;
  int error = 0;
};

std::vector<int> probe_team(int requested)
{
  std::array<affinity, HAMS_CPU_COUNT> rows{};
  int actual = 0;
  #pragma omp parallel shared(rows, actual)
  {
    const int tid = omp_get_thread_num();
    #pragma omp single
    actual = omp_get_num_threads();
    if (tid < HAMS_CPU_COUNT) {
      auto &row = rows[static_cast<std::size_t>(tid)];
      CPU_ZERO(&row.allowed);
      if (sched_getaffinity(0, sizeof(row.allowed), &row.allowed) != 0)
        row.error = errno;
      row.cpu = sched_getcpu();
    }
  }
  if (actual != requested) fail("probe observed an unexpected team size");
  std::vector<int> cpus;
  for (int tid = 0; tid < requested; ++tid) {
    const auto &row = rows[static_cast<std::size_t>(tid)];
    if (row.error || row.cpu < 0 || row.cpu >= CPU_SETSIZE ||
        CPU_COUNT(&row.allowed) != 1 || !CPU_ISSET(row.cpu, &row.allowed))
      fail("probe requires one allowed CPU per thread matching sched_getcpu");
    if (row.cpu >= HAMS_CPU_COUNT || (!cpus.empty() && row.cpu <= cpus.back()))
      fail("native mapping is outside HAMS_CPU_COUNT or is not strictly ascending by tid");
    cpus.push_back(row.cpu);
  }
  return cpus;
}

int selected_threads(const options &o, std::uint64_t operation)
{
  return o.scenario == "switch" && operation % 2 ? o.other_threads : o.threads;
}

void probe(backend &state)
{
  std::map<int, std::vector<int>> observed;
  const int operations = state.o.scenario == "switch" ? 4 : 2;
  for (int i = 0; i < operations; ++i) {
    const int threads = selected_threads(state.o, static_cast<std::uint64_t>(i));
    state.apply(threads);
    const auto cpus = probe_team(threads);
    const auto previous = observed.find(threads);
    if (previous != observed.end() && previous->second != cpus)
      fail("the same thread count produced different mappings during the probe");
    if (!state.maps.empty()) {
      const auto &expected = state.maps.at(threads);
      for (int tid = 0; tid < threads; ++tid)
        if (cpus[static_cast<std::size_t>(tid)] != expected.tid_to_cpu[tid])
          fail("observed mapping differs from the reference map");
    }
    observed[threads] = cpus;
  }
  for (const auto &entry : observed) {
    std::printf("MAP threads=%d cpus=", entry.first);
    for (std::size_t i = 0; i < entry.second.size(); ++i)
      std::printf("%s%d", i ? "," : "", entry.second[i]);
    std::printf("\n");
  }
}

struct alignas(64) counter {
  volatile std::uint64_t value = 0;
};
static_assert(sizeof(counter) >= 64, "each thread needs a separate cache line");

void kernel(counter *counters)
{
  // Ordinary parallel region; only its implicit join barrier is present.
  #pragma omp parallel
  {
    const int tid = omp_get_thread_num();
    counters[tid].value += 1;
  }
}

void run(backend &state)
{
  std::array<counter, HAMS_CPU_COUNT> counters{};
  std::uint64_t operation = 0;
  const auto execute = [&]() {
    state.apply(selected_threads(state.o, operation));
    kernel(counters.data());
    ++operation;
  };
  const double first_start = omp_get_wtime();
  execute();
  const double first_seconds = omp_get_wtime() - first_start;
  std::printf("SAMPLE phase=first batch=0 operations=1 seconds=%.9f\n", first_seconds);

  for (int i = 0; i < state.o.warmup; ++i) execute();
  for (int batch = 0; batch < state.o.batches; ++batch) {
    const double start = omp_get_wtime();
    for (int i = 0; i < state.o.iterations; ++i) execute();
    const double seconds = omp_get_wtime() - start;
    std::printf("SAMPLE phase=steady batch=%d operations=%d seconds=%.9f\n",
                batch + 1, state.o.iterations, seconds);
  }

  // Per-thread counts detect omitted work or an unexpected team size. All
  // validation and output are outside the measured parallel-region batches.
  const std::uint64_t runs_a = state.o.scenario == "switch" ? (operation + 1) / 2 : operation;
  const std::uint64_t runs_b = state.o.scenario == "switch" ? operation / 2 : 0;
  std::uint64_t actual_sum = 0;
  std::uint64_t expected_sum = 0;
  for (int tid = 0; tid < HAMS_CPU_COUNT; ++tid) {
    const std::uint64_t expected = (tid < state.o.threads ? runs_a : 0) +
                                  (tid < state.o.other_threads ? runs_b : 0);
    const std::uint64_t actual = counters[static_cast<std::size_t>(tid)].value;
    if (actual != expected) fail("per-thread checksum mismatch at tid=" + std::to_string(tid));
    actual_sum += actual;
    expected_sum += expected;
  }
  std::printf("CHECKSUM actual=%" PRIu64 " expected=%" PRIu64 " status=pass\n",
              actual_sum, expected_sum);
}

}  // namespace

int main(int argc, char **argv)
{
  const options o = parse_options(argc, argv);
  backend state(o);
  std::printf("CONFIG mode=%s scenario=%s threads=%d other_threads=%d iterations=%d "
              "batches=%d warmup=%d probe=%d initial_threads=%d\n",
              o.mode.c_str(), o.scenario.c_str(), o.threads, o.other_threads,
              o.iterations, o.batches, o.warmup, o.probe ? 1 : 0, state.initial_threads);
  if (o.probe) probe(state);
  else {
    run(state);
    // Recheck this process after all timers and checksum validation. These
    // extra teams never enter a sample; this is a post-run mapping snapshot.
    probe(state);
    std::printf("VALIDATION mapping=pass\n");
  }
  return EXIT_SUCCESS;
}
