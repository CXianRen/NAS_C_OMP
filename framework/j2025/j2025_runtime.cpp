#include "j2025_runtime.h"
#if J2025_ENABLE
#include "j2025.h"
#include "../region_control/region_control.h"

#include <cassert>
#include <new>

struct j2025_runtime {
  region_control *control;
  hams_binding *binding;
  j2025 *tuner;
};

/* J2025 在开始 callback 中选择 cfg，直接调用 HAMS 完成绑定。 */
static void on_parallel_start(void *context, int id)
{
  auto *runtime = static_cast<j2025_runtime *>(context);
  hams_binding_apply(runtime->binding, j2025_select_cfg(runtime->tuner, id));
}

/* 结束 callback 直接把桩提供的单次耗时交给 J2025。 */
static void on_parallel_end(void *context, int id, double seconds)
{
  auto *runtime = static_cast<j2025_runtime *>(context);
  j2025_observe(runtime->tuner, id, seconds);
}

/* J2025 拥有搜索状态和 binding，并直接向 region_control 注册回调。 */
j2025_runtime *j2025_attach(region_control *control)
{
  assert(control);
  auto *runtime = new (std::nothrow) j2025_runtime;
  assert(runtime);
  runtime->control = control;
  runtime->binding = hams_binding_create();
  hams_binding_status status;
  hams_binding_get_status(runtime->binding, &status);
  runtime->tuner = j2025_create(control->region_count, status.max_threads);
  // 当前搜索由 region 样本推进，不需要额外的 step 回调。
  static const region_control_callbacks callbacks{
      nullptr, on_parallel_start, on_parallel_end, nullptr};
  region_control_register(control, &callbacks, runtime);
  return runtime;
}

/* 先注销 J2025 回调，再释放搜索状态和 binding；不修改线程。 */
void j2025_detach(j2025_runtime *runtime)
{
  if (!runtime) return;
  region_control_register(runtime->control, nullptr, nullptr);
  hams_binding_destroy(runtime->binding);
  j2025_destroy(runtime->tuner, runtime->control->regions);
  delete runtime;
}
#endif
