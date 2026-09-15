#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "hams_binding.h"
#include <assert.h>
#include <omp.h>
#include <sched.h>
#include <stdio.h>

/* 生成 cfg 并检查实际绑定，按初始满线程数的宽度输出每个 tid 的 bitmask。 */
static void check(hams_binding *binding, const char *name, int threads,
                  int stride, int width)
{
  hams_binding_cfg cfg = {};
  cfg.thread_number = threads;
  for (int tid = 0; tid < threads; tid++) {
    int cpu = tid * stride;
    cfg.mask[cpu] = true;
    cfg.tid_to_cpu[tid] = cpu;
  }
  assert((int)cfg.mask.count() == threads);
  hams_binding_apply(binding, &cfg);
  cpu_set_t masks[HAMS_CPU_COUNT];

  #pragma omp parallel
  {
    int tid = omp_get_thread_num();
    int cpu = tid * stride;
    cpu_set_t &mask = masks[tid];
    int result = sched_getaffinity(0, sizeof(mask), &mask);
    assert(result == 0);
    assert(omp_get_num_threads() == threads);
    assert(CPU_COUNT(&mask) == 1 && CPU_ISSET(cpu, &mask));
    assert(sched_getcpu() == cpu);
  }
  printf("%s: T=%d, CPU=tid*%d PASS\n", name, threads, stride);
  for (int tid = 0; tid < threads; tid++) {
    printf("  tid=%d bitmask=", tid);
    for (int cpu = width - 1; cpu >= 0; cpu--) {
      printf("%d", CPU_ISSET(cpu, &masks[tid]) ? 1 : 0);
    }
    putchar('\n');
  }
}

/* 依次测试满线程、半线程 close、半线程 spread，满线程数由 OMP_NUM_THREADS 指定。 */
int main()
{
  int full = omp_get_max_threads();
  hams_binding *binding = hams_binding_create();
  check(binding, "full", full, 1, full);
  check(binding, "half close", full / 2, 1, full);
  check(binding, "half spread", full / 2, 2, full);
  hams_binding_destroy(binding);
}
