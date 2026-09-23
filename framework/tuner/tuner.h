#ifndef TUNER_H
#define TUNER_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct region_control region_control;
typedef struct tuner tuner;

/* 在已初始化的 control 上挂载 TUNER=none|dummy|offline|j2025|j2025_b|otter，未设置/空值默认 none。
 * none 返回 NULL，不查询绑定支持/拓扑、不分配状态、不注册 callback；
 * 线程数和绑定完全沿用 OpenMP 环境变量，不修改 OpenMP 设置。
 * Dummy 按 region 执行完整流程，始终使用初始满线程、连续绑定配置。
 * Offline 从 OFFLINE_CONFIG 加载 function:line 配置，缺项使用初始满线程配置。
 * J2025/J2025_B 按 region 调优；Otter 仅在 step_start 调整全步共享配置。
 * 未知名称或启用时没有插桩会报错退出；每个 control 只挂载一个 tuner。 */
tuner *tuner_attach(region_control *control);

/* 显式指定 tuner 名称，使用同一挂载路径。 */
tuner *tuner_attach_named(region_control *control, const char *name);

/* 查询所挂载算法的名称；NULL 返回 "none"。 */
const char *tuner_name(const tuner *runtime);

/* iteration_end 后注销、报告并释放；control 须仍有效。
 * 保留最后应用的线程配置，允许 NULL。 */
void tuner_detach(tuner *runtime);

#ifdef __cplusplus
}
#endif

#endif
