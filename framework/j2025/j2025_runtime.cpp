#include "j2025_runtime.h"
#if J2025_ENABLE
#include "../tuner/tuner.h"

#include <cassert>
#include <new>

struct j2025_runtime {
  tuner *runtime;
};

/* 兼容原有显式 J2025 挂载 API；配置/反馈/绑定统一由 tuner 层处理。 */
j2025_runtime *j2025_attach(region_control *control)
{
  assert(control);
  auto *runtime = new (std::nothrow) j2025_runtime;
  assert(runtime);
  runtime->runtime = tuner_attach_named(control, "j2025");
  return runtime;
}

/* 先注销 J2025 回调，再释放搜索状态和 binding；不修改线程。 */
void j2025_detach(j2025_runtime *runtime)
{
  if (!runtime) return;
  tuner_detach(runtime->runtime);
  delete runtime;
}
#endif
