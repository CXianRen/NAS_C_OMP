#include "j2025.h"

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

/* 仅测试：预热排除、TM 比较、候选缓存、Fibonacci 收敛及 STABLE 固定配置。 */
static void check_search(int maximum, int optimum, bool close, bool tie = false) {
  j2025 *tuner = j2025_create(1, maximum);
  const auto *callbacks = j2025_callbacks();
  for (int call = 0; call < 3; ++call) {
    const auto *cfg = callbacks->select_cfg(tuner, 0);
    assert(cfg->thread_number == maximum);
    check_cfg(cfg, maximum, false);
    double seconds = cost(maximum, optimum);
    if (!tie && ((call == 1 && close) || (call == 2 && !close))) seconds += 10;
    callbacks->observe(tuner, 0, call == 0 ? 0 : seconds);
  }

  std::vector<bool> measured(maximum / 2, false);
  measured.back() = true;  // TM 已测过满线程，NT 搜索复用这个样本。
  bool stable = false;
  int probes = 0;
  for (int call = 0; call < 32; ++call) {
    callbacks->step_start(tuner, call);
    const auto *cfg = callbacks->select_cfg(tuner, 0);
    int nt = cfg->thread_number;
    check_cfg(cfg, maximum, close && !tie);
    if (measured[nt / 2 - 1]) {
      stable = true;
      assert(nt == optimum);
    } else {
      assert(!stable);  // 未收敛时，同一候选不得重复 probe。
      measured[nt / 2 - 1] = true;
      ++probes;
    }
    callbacks->observe(tuner, 0, stable ? 0 : cost(nt, optimum));
    callbacks->step_finish(tuner, call);
  }
  assert(stable);
  if (maximum >= 32) assert(probes < maximum / 2 - 1);
  j2025_destroy(tuner);
}

/* 仅测试：交错执行两个 region，step 通知不能重置各自的搜索状态。 */
static void check_regions() {
  j2025 *tuner = j2025_create(2, 8);
  const auto *callbacks = j2025_callbacks();
  for (int step = 0; step < 32; ++step) {
    callbacks->step_start(tuner, step);
    for (int id = 0; id < 2; ++id) {
      int optimum = id == 0 ? 2 : 6;
      const auto *cfg = callbacks->select_cfg(tuner, id);
      if (step < 3) assert(cfg->thread_number == 8);
      if (step >= 16) {
        assert(cfg->thread_number == optimum);
        check_cfg(cfg, 8, id == 0);
      }
      double seconds = cost(cfg->thread_number, optimum);
      if ((step == 1 && id == 0) || (step == 2 && id == 1)) seconds += 10;
      callbacks->observe(tuner, id, step == 0 ? 0 : seconds);
    }
    callbacks->step_finish(tuner, step);
  }
  j2025_destroy(tuner);
}

/* 仅测试：穷举多种候选规模及最优点，不创建线程、不绑定、不读时钟。 */
int main() {
  int cases = 0;
  for (int maximum = 2; maximum <= 64 && maximum <= HAMS_CPU_COUNT; maximum += 2) {
    for (int optimum = 2; optimum <= maximum; optimum += 2) {
      check_search(maximum, optimum, false);
      check_search(maximum, optimum, true);
      cases += 2;
    }
  }
  check_search(8, 4, false, true);
  check_regions();
  std::printf("j2025_search=PASS cases=%d\n", cases);
  std::puts("j2025_mapping_tie=PASS j2025_region_isolation=PASS");
}
