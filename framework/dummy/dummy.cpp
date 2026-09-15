#include "dummy.h"

#include <cassert>
#include <new>

struct dummy {
  hams_binding_cfg initial{};
};

dummy *dummy_create(int max_threads)
{
  assert(max_threads >= 1 && max_threads <= HAMS_CPU_COUNT);
  auto *tuner = new (std::nothrow) dummy{};
  assert(tuner);
  tuner->initial.thread_number = max_threads;
  for (int tid = 0; tid < max_threads; ++tid) {
    tuner->initial.mask[tid] = true;
    tuner->initial.tid_to_cpu[tid] = tid;
  }
  return tuner;
}

const hams_binding_cfg *dummy_select_cfg(dummy *tuner, int)
{
  assert(tuner);
  return &tuner->initial;
}

/* 保留反馈调用路径；样本不改变初始配置。 */
void dummy_observe(dummy *, int, double) {}

void dummy_destroy(dummy *tuner)
{
  delete tuner;
}
