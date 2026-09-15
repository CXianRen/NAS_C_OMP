#include "../dummy/dummy.h"
#include <cassert>
#include <cstdio>

/* 不依赖 OpenMP：任何 region 和耗时序列都返回初始满线程配置。 */
static void check_fixed_config(int full)
{
  dummy *policy = dummy_create(full);
  const int regions[] = {0, 3, 1, 255, 0, 3};
  const double seconds[] = {1.0, 1000.0, 0.0, 0.001, 8.0, 0.5};
  for (int i = 0; i < 6; ++i) {
    const hams_binding_cfg *cfg = dummy_select_cfg(policy, regions[i]);
    assert(cfg && cfg->thread_number == full);
    assert(cfg->mask.count() == static_cast<unsigned>(full));
    for (int tid = 0; tid < full; ++tid) {
      assert(cfg->tid_to_cpu[tid] == tid);
      assert(cfg->mask[tid]);
    }
    dummy_observe(policy, regions[i], seconds[i]);
  }
  const hams_binding_cfg *last = dummy_select_cfg(policy, 1);
  assert(last->thread_number == full);
  dummy_destroy(policy);
}

int main()
{
  for (int full : {1, 3, 8, HAMS_CPU_COUNT}) check_fixed_config(full);
  std::puts("dummy PASS (fixed full threads, contiguous binding, varied regions and samples)");
}
