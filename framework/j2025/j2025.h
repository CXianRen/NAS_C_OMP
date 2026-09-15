#ifndef J2025_H
#define J2025_H

#include "../hams/hams_binding.h"
#include "../region_control/region_control.h"

struct j2025;

/* 创建独立的 region 状态；max_threads 是冷启动上限，须为 >= 2 的偶数。 */
j2025 *j2025_create(int region_count, int max_threads);

/* 选择当前 region 的配置；由 J2025 的桩 callback 调用 HAMS 执行。 */
const hams_binding_cfg *j2025_select_cfg(j2025 *tuner, int id);

/* 使用本次 region 耗时更新搜索状态。 */
void j2025_observe(j2025 *tuner, int id, double seconds);

/* 按桩的名称汇报最终配置并释放；未提供元数据时只释放搜索状态。 */
void j2025_destroy(j2025 *tuner, const region_info *regions = nullptr);

#endif
