#ifndef OFFLINE_H
#define OFFLINE_H

#include "../hams/hams_binding.h"
#include "../region_control/region_control.h"

struct offline;

/* 加载并校验整个文件；直接使用 region_control 的公共 region 元数据。
 * regions 由调用方持有，名称和行号在编译期已完整初始化。
 * 空路径或无法读取时使用初始满线程；配置内容错误直接报错退出。 */
offline *offline_create(const region_info *regions, int region_count,
                        int max_threads, const char *path);

/* 每个 region 首次进入时缓存配置；缺项提示一次并使用默认配置。 */
const hams_binding_cfg *offline_select_cfg(offline *tuner, int id);
void offline_observe(offline *tuner, int id, double seconds);
void offline_destroy(offline *tuner);

#endif
