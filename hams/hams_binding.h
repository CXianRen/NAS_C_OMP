#ifndef HAMS_BINDING_H
#define HAMS_BINDING_H

#include <bitset>

/* 编译期 CPU 数量，假定 [0, HAMS_CPU_COUNT) 内全部可绑定。 */
#ifndef HAMS_CPU_COUNT
#define HAMS_CPU_COUNT 128
#endif

/* bit 下标是系统 CPU ID（hwloc PU os_index）；tid x 对应第 x 个置位 bit。
 * 调用方生成 cfg 时同时填写 mask 和严格升序的 tid_to_cpu，不由绑定层展开。 */
typedef struct {
  int thread_number;
  std::bitset<HAMS_CPU_COUNT> mask;
  int tid_to_cpu[HAMS_CPU_COUNT];
} hams_binding_cfg;

/* 同一协调线程在并行区外使用，一个线程池只保留一个上下文。
 * 期间不要另外修改默认线程数、dynamic 设置或工作线程亲和性。
 * 错误仅通过 assert 终止程序；-DNDEBUG 关闭检查，调用方须保证输入和运行环境有效。 */
typedef struct hams_binding hams_binding;

typedef struct {
  int max_threads;        /* 初始 OpenMP 最大线程数、线程上限和 HAMS_CPU_COUNT 的最小值。 */
  int supported;          /* OMP_PROC_BIND=false 时支持手动绑定。 */
} hams_binding_status;

/* 读取线程上限，不探测 CPU 可用性、不创建或绑定线程。 */
hams_binding *hams_binding_create(void);

/* 将线程上限和绑定支持情况写入 status；参数必须非空。 */
void hams_binding_get_status(const hams_binding *binding,
                             hams_binding_status *status);

/* 唯一配置输入是 cfg：1 <= thread_number <= max_threads，置位数等于线程数，
 * tid_to_cpu[tid] 是第 tid 个置位 bit，CPU ID 在 [0, HAMS_CPU_COUNT) 内且小于 CPU_SETSIZE。
 * 直接按映射绑定，设置后续普通并行区的默认线程数；相同配置直接返回。
 * 仅缓存线程数和 mask，不保留 cfg 指针。 */
void hams_binding_apply(hams_binding *binding, const hams_binding_cfg *cfg);

/* 仅用于测试/示例诊断：读取预生成的 tid_to_cpu[tid]。
 * cfg 有效且 tid 在 [0, T) 内；实际绑定流程不调用此函数。 */
int hams_binding_get_target_cpu(const hams_binding_cfg *cfg, int tid);

/* 仅释放上下文内存，允许 NULL；不改变线程数、dynamic 设置或亲和性。 */
void hams_binding_destroy(hams_binding *binding);

#endif
