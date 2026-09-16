#include "../j2025_b/j2025_b.h"

#include <cassert>
#include <cstdio>
#include <vector>

/* 仅测试：使用确定性的单峰耗时，最优点可以位于任意候选位置。 */
static double cost(int threads, int optimum) {
  double distance = threads - optimum;
  return 1 + distance * distance;
}

/* 仅测试：检查线程数、系统 CPU 位和预生成映射完全一致。 */
static void check_cfg(const hams_binding_cfg *cfg, int maximum, bool close) {
  int nt = cfg->thread_number;
  assert(nt >= 2 && nt <= maximum && nt % 2 == 0);
  assert(cfg->mask.count() == static_cast<unsigned>(nt));
  for (int tid = 0; tid < nt; ++tid) {
    int cpu = close ? tid : tid * maximum / nt;
    assert(cfg->tid_to_cpu[tid] == cpu);
    assert(cfg->mask[cpu]);
  }
}

/* 仅测试：observe 只推进策略，下一次 select 前不得改写刚执行的配置。 */
static void check_same_cfg(const hams_binding_cfg *cfg, const hams_binding_cfg &before) {
  assert(cfg->thread_number == before.thread_number);
  assert(cfg->mask == before.mask);
  for (int tid = 0; tid < before.thread_number; ++tid)
    assert(cfg->tid_to_cpu[tid] == before.tid_to_cpu[tid]);
}

struct b_progress {
  explicit b_progress(int maximum) : measured(maximum / 2, false) {}
  std::vector<bool> measured;
  bool warmed = false;
  int probes = 0;
  int mapping_samples = 0;
  bool stable = false;
};

/* 仅测试：B 先连续映射搜索线程数，再对同一个最优线程数重新测量两种映射。 */
static const hams_binding_cfg *check_b_call(j2025_b *tuner, int id, int maximum,
                                           int optimum, bool close, bool tie,
                                           b_progress &progress) {
  const auto *cfg = j2025_b_select_cfg(tuner, id);
  const auto before = *cfg;
  int nt = cfg->thread_number;
  assert(nt >= 2 && nt <= maximum && nt % 2 == 0);
  double seconds = 0;
  if (!progress.warmed) {
    assert(nt == maximum);
    check_cfg(cfg, maximum, true);
    progress.warmed = true;
    // 极小的预热时间不能占用满线程缓存，也不能影响最终最优线程数。
  } else if (progress.mapping_samples == 0 && !progress.measured[nt / 2 - 1]) {
    assert(!progress.stable);
    check_cfg(cfg, maximum, true);
    progress.measured[nt / 2 - 1] = true;
    ++progress.probes;
    seconds = cost(nt, optimum);
  } else if (progress.mapping_samples == 0) {
    // 第一次重复候选只能是搜索结束后的 SCATTER 映射样本。
    assert(nt == optimum);
    check_cfg(cfg, maximum, false);
    progress.mapping_samples = 1;
    // 映射样本均比搜索样本慢；映射选择只能比较这两个新样本。
    seconds = 100000 + (!tie && close ? 10 : 0);
  } else if (progress.mapping_samples == 1) {
    assert(nt == optimum);
    check_cfg(cfg, maximum, true);
    progress.mapping_samples = 2;
    seconds = 100000 + (!tie && !close ? 10 : 0);
  } else {
    progress.stable = true;
    assert(nt == optimum);
    check_cfg(cfg, maximum, close && !tie);
    // STABLE 的新样本不能触发重新搜索或映射切换。
  }
  j2025_b_observe(tuner, id, seconds);
  check_same_cfg(cfg, before);
  return cfg;
}

static void check_b_search(int maximum, int optimum, bool close, bool tie = false) {
  j2025_b *tuner = j2025_b_create(1, maximum);
  b_progress progress(maximum);
  for (int call = 0; call < 32; ++call)
    check_b_call(tuner, 0, maximum, optimum, close, tie, progress);
  assert(progress.stable && progress.mapping_samples == 2);
  assert(progress.probes > 0);
  if (maximum >= 32) assert(progress.probes < maximum / 2);
  j2025_b_destroy(tuner);
}

/* 仅测试：两个 region 以不同节奏推进 B；线程数和映射结果各自独立。 */
static void check_b_regions() {
  constexpr int maximum = 16;
  j2025_b *tuner = j2025_b_create(2, maximum);
  b_progress progress[] = {b_progress(maximum), b_progress(maximum)};
  const hams_binding_cfg *last[] = {nullptr, nullptr};
  hams_binding_cfg saved[2]{};
  for (int step = 0; step < 64; ++step) {
    for (int id = 0; id < 2; ++id) {
      if (id == 1 && step % 2) continue;
      last[id] = check_b_call(tuner, id, maximum, id == 0 ? 2 : 12,
                              id == 0, false, progress[id]);
      int other = 1 - id;
      if (last[other]) check_same_cfg(last[other], saved[other]);
      saved[id] = *last[id];
    }
  }
  assert(progress[0].stable && progress[1].stable);
  j2025_b_destroy(tuner);
}

/* 仅测试：独立链接 B，穷举候选规模和最优点，不创建线程、不绑定、不读时钟。 */
int main() {
  int cases = 0;
  for (int maximum = 2; maximum <= 64 && maximum <= HAMS_CPU_COUNT; maximum += 2) {
    for (int optimum = 2; optimum <= maximum; optimum += 2) {
      check_b_search(maximum, optimum, false);
      check_b_search(maximum, optimum, true);
      cases += 2;
    }
  }
  check_b_search(8, 4, false, true);
  check_b_regions();
  std::printf("j2025_b_search_then_mapping=PASS cases=%d\n", cases);
  std::puts("j2025_b_mapping_tie=PASS j2025_b_region_isolation=PASS");
}
