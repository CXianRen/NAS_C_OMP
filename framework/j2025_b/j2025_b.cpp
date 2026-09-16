#include "j2025_b.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <limits>
#include <new>
#include <vector>

namespace {

enum phase { WARMUP, NT_SEARCH, TM_SCATTER, TM_CLOSE, APPLY_BEST, STABLE };
enum mapping { SCATTER, CLOSE };

struct region_state {
  phase current = WARMUP;
  mapping best_tm = CLOSE;
  double best_time = std::numeric_limits<double>::infinity();
  int best_nt = 0;
  int left = 0;
  int fib = 3;
  int pending = 0;
  std::vector<double> samples;
  hams_binding_cfg cfg{};
};

}  // namespace

struct j2025_b {
  int max_threads;
  std::vector<int> fibonacci{0, 1, 1, 2};
  std::vector<region_state> regions;
};

/* 同时生成 CPU mask 和严格升序的 tid -> CPU 映射。 */
static void make_cfg(region_state &state, int nt, int maximum, mapping tm) {
  state.cfg.thread_number = nt;
  state.cfg.mask.reset();
  for (int tid = 0; tid < nt; ++tid) {
    int cpu = tm == CLOSE ? tid : tid * maximum / nt;
    state.cfg.mask[cpu] = true;
    state.cfg.tid_to_cpu[tid] = cpu;
  }
}

static region_state &state_of(j2025_b *tuner, int id) {
  assert(tuner && id >= 0 && id < static_cast<int>(tuner->regions.size()));
  return tuner->regions[id];
}

/* Fibonacci 候选为 {2, 4, ..., N}；缓存样本，最后补测剩余区间端点。 */
static int next_candidate(j2025_b *tuner, region_state &state) {
  int count = tuner->max_threads / 2;
  while (state.fib > 3) {
    int a = state.left + tuner->fibonacci[state.fib - 2];
    int b = state.left + tuner->fibonacci[state.fib - 1];
    if (a < count && state.samples[a] < 0) return a;
    if (b < count && state.samples[b] < 0) return b;
    double fa = a < count ? state.samples[a] : std::numeric_limits<double>::infinity();
    double fb = b < count ? state.samples[b] : std::numeric_limits<double>::infinity();
    if (fa > fb) state.left = a;
    --state.fib;
  }
  int right = std::min(state.left + tuner->fibonacci[state.fib], count - 1);
  for (int i = state.left; i <= right; ++i)
    if (state.samples[i] < 0) return i;
  return -1;
}

/* 每次只选择一个配置，实际应用由公共 tuner 挂载层完成。 */
const hams_binding_cfg *j2025_b_select_cfg(j2025_b *tuner, int id) {
  auto &state = state_of(tuner, id);
  if (state.current == STABLE) return &state.cfg;

  int nt = tuner->max_threads;
  mapping tm = CLOSE;
  switch (state.current) {
    case WARMUP:
      break;
    case NT_SEARCH:
      state.pending = next_candidate(tuner, state);
      if (state.pending < 0) {
        // mapping 固定最佳线程数，重新比较两个新样本。
        state.current = TM_SCATTER;
        nt = state.best_nt;
        tm = SCATTER;
      } else {
        nt = 2 * (state.pending + 1);
      }
      break;
    case TM_SCATTER:
    case TM_CLOSE:
      nt = state.best_nt;
      tm = state.current == TM_CLOSE ? CLOSE : SCATTER;
      break;
    case APPLY_BEST:
      state.current = STABLE;
      nt = state.best_nt;
      tm = state.best_tm;
      break;
    case STABLE:
      break;
  }
  make_cfg(state, nt, tuner->max_threads, tm);
  return &state.cfg;
}

/* 只推进状态；下一次 select 前不改写刚执行的 cfg。 */
void j2025_b_observe(j2025_b *tuner, int id, double seconds) {
  auto &state = state_of(tuner, id);
  assert(seconds >= 0 && seconds < std::numeric_limits<double>::infinity());
  switch (state.current) {
    case WARMUP:
      state.current = NT_SEARCH;
      break;
    case NT_SEARCH:
      state.samples[state.pending] = seconds;
      if (seconds < state.best_time) {
        state.best_time = seconds;
        state.best_nt = state.cfg.thread_number;
      }
      break;
    case TM_SCATTER:
      state.best_time = seconds;
      state.best_tm = SCATTER;
      state.current = TM_CLOSE;
      break;
    case TM_CLOSE:
      if (seconds < state.best_time) {
        state.best_time = seconds;
        state.best_tm = CLOSE;
      }
      state.current = APPLY_BEST;
      break;
    case APPLY_BEST:
    case STABLE:
      break;
  }
}

j2025_b *j2025_b_create(int region_count, int max_threads) {
  assert(region_count > 0 && max_threads >= 2 && max_threads % 2 == 0);
  assert(max_threads <= HAMS_CPU_COUNT);
  auto *tuner = new (std::nothrow) j2025_b;
  assert(tuner);
  tuner->max_threads = max_threads;
  while (tuner->fibonacci.back() < max_threads / 2 - 1) {
    int n = static_cast<int>(tuner->fibonacci.size());
    tuner->fibonacci.push_back(tuner->fibonacci[n - 1] + tuner->fibonacci[n - 2]);
  }
  tuner->regions.resize(region_count);
  for (auto &state : tuner->regions) {
    state.best_nt = max_threads;
    state.fib = static_cast<int>(tuner->fibonacci.size()) - 1;
    state.samples.assign(max_threads / 2, -1);
  }
  return tuner;
}

void j2025_b_destroy(j2025_b *tuner, const region_info *regions) {
  for (std::size_t id = 0; regions && id < tuner->regions.size(); ++id) {
    const auto &cfg = tuner->regions[id].cfg;
    if (!cfg.thread_number) continue;
    std::printf("J2025_B final region=%s", regions[id].name);
    if (regions[id].line) std::printf(":%d", regions[id].line);
    std::printf(" threads=%d mask=%s\n", cfg.thread_number,
                cfg.mask.to_string().substr(HAMS_CPU_COUNT - tuner->max_threads).c_str());
  }
  delete tuner;
}
