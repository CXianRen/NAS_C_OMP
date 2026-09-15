#include "j2025.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <limits>
#include <new>
#include <vector>

enum phase { WARMUP, TM_SCATTER, TM_CLOSE, NT_SEARCH, STABLE };
enum mapping { SCATTER, CLOSE };

struct region_state {
  phase current = WARMUP;
  mapping best_tm = SCATTER;
  double best_time = std::numeric_limits<double>::infinity();
  int best_nt = 0;
  int left = 0;
  int fib = 3;
  int pending = 0;
  std::vector<double> samples;
  hams_binding_cfg cfg{};
};

struct j2025 {
  int max_threads;
  std::vector<int> fibonacci{0, 1, 1, 2};
  std::vector<region_state> regions;
};

/* 一次遍历同时生成系统 CPU mask 和严格升序的 tid -> CPU 映射。 */
static void make_cfg(region_state &state, int nt, int maximum, mapping tm) {
  state.cfg.thread_number = nt;
  state.cfg.mask.reset();
  for (int tid = 0; tid < nt; ++tid) {
    int cpu = tm == CLOSE ? tid : tid * maximum / nt;
    state.cfg.mask[cpu] = true;
    state.cfg.tid_to_cpu[tid] = cpu;
  }
}

/* 读取一个 region 的私有状态；调用方使用静态、连续的 region ID。 */
static region_state &state_of(j2025 *tuner, int id) {
  assert(tuner && id >= 0 && id < static_cast<int>(tuner->regions.size()));
  return tuner->regions[id];
}

/* 只保留严格更小的耗时，平局保留先测到的配置。 */
static void keep_best(region_state &state, int nt, mapping tm, double seconds) {
  if (seconds < state.best_time) {
    state.best_time = seconds;
    state.best_nt = nt;
    state.best_tm = tm;
  }
}

/* 在整数 Fibonacci 区间上搜索；缓存已测点，最后补测区间内的端点。 */
static int next_candidate(j2025 *tuner, region_state &state) {
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

/* TODO：后续加入 stddev(XObject) / mean(XObject) > 10% 的判断。 */
static void check_variance() {}

/* TODO：后续加入 NUMA balancing；FIRST_TOUCH 当前保留应用已有页面布局。 */
static void enable_numa_balancing() {}

/* 选择本次唯一一次 region 执行的配置，搜索跨后续调用推进。 */
const hams_binding_cfg *j2025_select_cfg(j2025 *tuner, int id) {
  auto &state = state_of(tuner, id);
  if (state.current == STABLE) return &state.cfg;

  int nt = tuner->max_threads;
  mapping tm = state.current == TM_CLOSE ? CLOSE : SCATTER;
  if (state.current == NT_SEARCH) {
    state.pending = next_candidate(tuner, state);
    if (state.pending < 0) {
      state.current = STABLE;
      nt = state.best_nt;
    } else {
      nt = 2 * (state.pending + 1);
    }
    tm = state.best_tm;
  }
  make_cfg(state, nt, tuner->max_threads, tm);
  return &state.cfg;
}

/* 接收单次耗时；预热和 STABLE 样本不参与搜索。 */
void j2025_observe(j2025 *tuner, int id, double seconds) {
  auto &state = state_of(tuner, id);
  assert(seconds >= 0 && seconds < std::numeric_limits<double>::infinity());
  switch (state.current) {
    case WARMUP:
      state.current = TM_SCATTER;
      break;
    case TM_SCATTER:
      keep_best(state, tuner->max_threads, SCATTER, seconds);
      state.current = TM_CLOSE;
      break;
    case TM_CLOSE:
      keep_best(state, tuner->max_threads, CLOSE, seconds);
      check_variance();
      enable_numa_balancing();
      state.samples.back() = state.best_time;
      state.current = NT_SEARCH;
      break;
    case NT_SEARCH:
      state.samples[state.pending] = seconds;
      keep_best(state, state.cfg.thread_number, state.best_tm, seconds);
      break;
    case STABLE:
      break;
  }
}

/* 冷启动一次确定候选集合和 Fibonacci 长度，不读取或修改 OpenMP 设置。 */
j2025 *j2025_create(int region_count, int max_threads) {
  assert(region_count > 0 && max_threads >= 2 && max_threads % 2 == 0);
  assert(max_threads <= HAMS_CPU_COUNT);
  auto *tuner = new (std::nothrow) j2025;
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

/* 析构时汇报最后一次选择的配置；跳过未使用的 region，不再推进搜索。 */
void j2025_destroy(j2025 *tuner, const region_info *regions) {
  for (std::size_t id = 0; regions && id < tuner->regions.size(); ++id) {
    const auto &cfg = tuner->regions[id].cfg;
    if (!cfg.thread_number) continue;
    // 与计时报告共用首次 START 捕获的函数名和行号。
    std::printf("J2025 final region=%s", regions[id].name);
    if (regions[id].line) std::printf(":%d", regions[id].line);
    // mask 只显示冷启动线程上限的宽度，最右侧为 CPU 0。
    std::printf(" threads=%d mask=%s\n", cfg.thread_number,
                cfg.mask.to_string().substr(HAMS_CPU_COUNT - tuner->max_threads).c_str());
  }
  delete tuner;
}
