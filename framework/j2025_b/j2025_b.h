#ifndef J2025_B_H
#define J2025_B_H

#include "../hams/hams_binding.h"
#include "../region_control/region_control.h"

struct j2025_b;

/* 每个 region 独立：连续绑定 Fibonacci 搜索线程数，再比较绑定方式。
 * max_threads 为初始线程上限，须为 >= 2 的偶数。 */
j2025_b *j2025_b_create(int region_count, int max_threads);

const hams_binding_cfg *j2025_b_select_cfg(j2025_b *tuner, int id);
void j2025_b_observe(j2025_b *tuner, int id, double seconds);

/* 按 region 汇报最后应用的配置；无元数据时仅释放。 */
void j2025_b_destroy(j2025_b *tuner, const region_info *regions = nullptr);

#endif
