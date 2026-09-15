#ifndef TUNER_H
#define TUNER_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct region_control region_control;
typedef struct tuner tuner;

/* 在已初始化的 control 上挂载 TUNER=none|j2025|otter，未设置/空值默认 none。
 * none 返回 NULL，不分配状态、不注册 callback、不修改 OpenMP 设置。
 * J2025 按 region 调优；Otter 仅在 step_start 调整全步共享配置。
 * 未知名称或启用时没有插桩会报错退出；每个 control 只挂载一个 tuner。 */
tuner *tuner_attach(region_control *control);

/* 显式选择同一挂载路径，供旧 J2025 API 等固定算法调用方使用。 */
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
