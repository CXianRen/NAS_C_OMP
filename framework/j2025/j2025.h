#ifndef J2025_H
#define J2025_H

#include "../region_control/region_control.h"

struct j2025;

/* 创建独立的 region 状态；max_threads 是冷启动上限，须为 >= 2 的偶数。 */
j2025 *j2025_create(int region_count, int max_threads);

/* 返回供 region_control_register 注册的通用 callback。 */
const region_control_callbacks *j2025_callbacks();

/* 仅释放搜索状态，不修改线程或页面布局。 */
void j2025_destroy(j2025 *tuner);

#endif
