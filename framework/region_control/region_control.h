#ifndef REGION_CONTROL_H
#define REGION_CONTROL_H

#include "../hams/hams_binding.h"

/* 一个协调线程共享一个 control；算法 context 的生命周期由调用方管理。 */
struct region_control;

struct region_control_callbacks {
  void (*step_start)(void *context, int step);                  /* 可为空：迭代开始通知。 */
  const hams_binding_cfg *(*select_cfg)(void *context, int id);  /* 返回本次 region 配置。 */
  void (*observe)(void *context, int id, double seconds);       /* 接收单次 region 耗时。 */
  void (*step_finish)(void *context, int step);                 /* 可为空：迭代结束通知。 */
};

/* 冷启动时创建共享 binding，捕获线程上限；不创建工作线程。 */
region_control *region_control_create();

/* 返回冷启动捕获的线程上限，不受后续配置切换影响。 */
int region_control_max_threads(const region_control *control);

/* 复制 callback 和 context；select_cfg、observe 必须非空。 */
void region_control_register(region_control *control,
                             const region_control_callbacks *callbacks,
                             void *context);

/* 仅转发迭代开始通知，不计时、不绑定、不重置算法状态。 */
void control_step_start(region_control *control, int step);

/* 仅转发迭代结束通知，不计时、不绑定。 */
void control_step_finish(region_control *control, int step);

/* 获取本次配置并立即应用；放在原 region 计时开始之前。 */
void control_region_start(region_control *control, int id);

/* 原样反馈 timer 提供的单次耗时；放在原 region 计时结束之后。 */
void control_region_finish(region_control *control, int id, double seconds);

/* 仅释放 binding 和 control，保留调用方 context 及线程状态。 */
void region_control_destroy(region_control *control);

#endif
