#include "region_control.h"

#include <cassert>
#include <new>

struct region_control {
  hams_binding *binding;
  int max_threads;
  region_control_callbacks callbacks;
  void *context;
};

/* 创建唯一的 binding，并保存初始线程上限。 */
region_control *region_control_create()
{
  region_control *control = new (std::nothrow) region_control{};
  assert(control != nullptr);
  control->binding = hams_binding_create();
  hams_binding_status status;
  hams_binding_get_status(control->binding, &status);
  control->max_threads = status.max_threads;
  return control;
}

/* 查询冷启动时的线程上限。 */
int region_control_max_threads(const region_control *control)
{
  assert(control != nullptr);
  return control->max_threads;
}

/* 注册算法入口；control 不持有算法 context 的所有权。 */
void region_control_register(region_control *control,
                             const region_control_callbacks *callbacks,
                             void *context)
{
  assert(control != nullptr && callbacks != nullptr);
  assert(callbacks->select_cfg != nullptr && callbacks->observe != nullptr);
  control->callbacks = *callbacks;
  control->context = context;
}

/* 通知算法新 iteration 开始。 */
void control_step_start(region_control *control, int step)
{
  assert(control != nullptr);
  if (control->callbacks.step_start)
    control->callbacks.step_start(control->context, step);
}

/* 通知算法当前 iteration 结束。 */
void control_step_finish(region_control *control, int step)
{
  assert(control != nullptr);
  if (control->callbacks.step_finish)
    control->callbacks.step_finish(control->context, step);
}

/* 每次进入 region 都提交配置，由共享 binding 判断是否需要重新绑定。 */
void control_region_start(region_control *control, int id)
{
  assert(control != nullptr && control->callbacks.select_cfg != nullptr);
  hams_binding_apply(control->binding,
                     control->callbacks.select_cfg(control->context, id));
}

/* 将外部 timer 的单次样本原样交给算法。 */
void control_region_finish(region_control *control, int id, double seconds)
{
  assert(control != nullptr && control->callbacks.observe != nullptr);
  control->callbacks.observe(control->context, id, seconds);
}

/* 释放内存，不销毁算法 context，不恢复或销毁工作线程。 */
void region_control_destroy(region_control *control)
{
  if (!control) return;
  hams_binding_destroy(control->binding);
  delete control;
}
