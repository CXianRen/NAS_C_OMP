#ifndef J2025_RUNTIME_H
#define J2025_RUNTIME_H

/* 旧显式 J2025 API 的兼容开关；新应用使用 tuner.h 和运行时 TUNER。 */
#ifndef J2025_ENABLE
#define J2025_ENABLE 1
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct region_control region_control;
typedef struct j2025_runtime j2025_runtime;

#if J2025_ENABLE
/* 通过通用 tuner 层显式挂载 J2025；此兼容 API 不读取 TUNER。 */
j2025_runtime *j2025_attach(region_control *control);
/* 注销回调并释放 J2025/binding；桩由调用方持有，线程配置继续保留。 */
void j2025_detach(j2025_runtime *runtime);
#else
/* 关闭时不分配状态、不注册 callback、不修改线程。 */
static inline j2025_runtime *j2025_attach(region_control *control)
{
  (void)control;
  return 0;
}
/* 与关闭时的 attach 配对，无需应用代码额外添加条件编译。 */
static inline void j2025_detach(j2025_runtime *runtime)
{
  (void)runtime;
}
#endif

#ifdef __cplusplus
}
#endif

#endif
