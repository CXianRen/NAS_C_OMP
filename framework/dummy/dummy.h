#ifndef DUMMY_H
#define DUMMY_H

#include "../hams/hams_binding.h"

struct dummy;

/* 固定初始满线程配置：1 <= max_threads <= HAMS_CPU_COUNT，tid 绑定 CPU tid。 */
dummy *dummy_create(int max_threads);

/* 所有 region 始终返回同一配置；运行时仍执行选择、绑定和计时反馈。 */
const hams_binding_cfg *dummy_select_cfg(dummy *tuner, int id);
void dummy_observe(dummy *tuner, int id, double seconds);

void dummy_destroy(dummy *tuner);

#endif
