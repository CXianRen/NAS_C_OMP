#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "hams_binding.h"

#include <assert.h>
#include <new>
#include <omp.h>
#include <sched.h>

struct hams_binding {
  hams_binding_status status;
  int thread_number;
  std::bitset<HAMS_CPU_COUNT> mask;
};

/* 以编译期 CPU 数量和 OpenMP 限制计算线程上限。 */
hams_binding *hams_binding_create(void)
{
  hams_binding *binding = new (std::nothrow) hams_binding{};
  assert(binding != NULL);
  int limit = omp_get_thread_limit();
  binding->status.supported = omp_get_proc_bind() == omp_proc_bind_false;
  binding->status.max_threads = omp_get_max_threads();
  if (binding->status.max_threads > HAMS_CPU_COUNT) binding->status.max_threads = HAMS_CPU_COUNT;
  if (binding->status.max_threads > limit) binding->status.max_threads = limit;
  return binding;
}

/* 复制线程上限和绑定支持情况。 */
void hams_binding_get_status(const hams_binding *binding,
                             hams_binding_status *status)
{
  assert(binding != NULL && status != NULL);
  *status = binding->status;
}

/* 仅用于测试/示例诊断：直接读取 tid 对应的系统 CPU ID。 */
int hams_binding_get_target_cpu(const hams_binding_cfg *cfg, int tid)
{
  assert(cfg != NULL && tid >= 0 && tid < cfg->thread_number);
  return cfg->tid_to_cpu[tid];
}

/* 相同配置直接返回；配置变化时按映射生成单 CPU 掩码并绑定每个线程。 */
void hams_binding_apply(hams_binding *binding, const hams_binding_cfg *cfg)
{
  assert(binding != NULL && cfg != NULL && binding->status.supported);
  assert(cfg->thread_number > 0 && cfg->thread_number <= binding->status.max_threads);
  if (binding->thread_number == cfg->thread_number &&
      binding->mask == cfg->mask) return;
  omp_set_dynamic(0);
  omp_set_num_threads(cfg->thread_number);
  #pragma omp parallel num_threads(cfg->thread_number)
  {
    assert(omp_get_num_threads() == cfg->thread_number);
    int tid = omp_get_thread_num();
    int cpu = cfg->tid_to_cpu[tid];
    assert(cpu >= 0 && cpu < HAMS_CPU_COUNT && cpu < CPU_SETSIZE);
    assert(cfg->mask[cpu]);
    assert(tid == 0 || cfg->tid_to_cpu[tid - 1] < cpu);
    /* 直接将该 tid 的 CPU ID 转成系统亲和性掩码。 */
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(cpu, &mask);
    int result = sched_setaffinity(0, sizeof(mask), &mask);
    assert(result == 0);
    (void)result;   // make compiler happy
  }
  binding->thread_number = cfg->thread_number;
  binding->mask = cfg->mask;
}

/* 仅释放上下文内存，线程数、dynamic 设置和亲和性保持不变。 */
void hams_binding_destroy(hams_binding *binding)
{
  delete binding;
}
