#ifndef REGION_CONTROL_H
#define REGION_CONTROL_H

#define REGION_CONTROL_MAX_REGIONS 256

/* parent=-1 表示 parallel；combined 的 parallel/for 两行共用同一份耗时。 */
typedef struct {
  const char *name;
  int parent, combined;
  const char *file;
  int line, nowait;
} region_info;

/* 全部 callback 可为空；context 由调用方管理，for 不触发 callback。 */
typedef struct {
  void (*step_start)(void *context, int step);
  void (*parallel_start)(void *context, int id);
  void (*parallel_end)(void *context, int id, double seconds);
  void (*step_end)(void *context, int step);
} region_control_callbacks;

/* 调用方持有状态；一个协调线程使用，不支持 nested parallel。 */
typedef struct region_control {
  region_info *regions;
  int region_count;
  region_control_callbacks callbacks;
  void *context;
  int in_step, parallel_id, sample_active;
  double parallel_begin;

  /* 内置计时：running 是总窗口，active 控制 region 累计，enabled 控制报告。 */
  int enabled, active, running, pending_nowait;
  double total_start, total_elapsed;
  double start[REGION_CONTROL_MAX_REGIONS], elapsed[REGION_CONTROL_MAX_REGIONS];
} region_control;

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化内置计时和可写静态表；name=NULL 的 region 在首次 START 时捕获位置。 */
void region_control_init(region_control *control, region_info *regions,
                         int count, int report);
/* 复制 callback/context；callbacks=NULL 清空注册，不接管 context 所有权。 */
void region_control_register(region_control *control,
                             const region_control_callbacks *callbacks,
                             void *context);
/* 开始正式总计时窗口。 */
void iteration_start(region_control *control);
/* 结束并累计正式总计时窗口。 */
void iteration_end(region_control *control);
/* 仅在正式窗口内通知 step 开始，不读取时钟。 */
void step_start(region_control *control, int step);
/* 可选：仅在正式窗口内通知 step 结束，不读取时钟。 */
void step_end(region_control *control, int step);
/* 返回正式窗口累计时间。 */
double iteration_time(const region_control *control);
/* 输出已启用的 parallel/for 报告。 */
void region_report(const region_control *control);
/* START 宏入口：首次保存位置，调用 parallel callback 后开始采样。 */
void region_parallel_start(region_control *control, int id,
                           const char *file, const char *name, int line);
/* END 宏入口：结束采样后反馈 callback。 */
void region_parallel_end(region_control *control, int id);
/* FOR_START 宏入口：仅由 master 计时，并按 metadata 延后 nowait 结束。 */
void region_for_start(region_control *control, int id,
                      const char *file, const char *name, int line);
/* FOR_END 宏入口：普通 for 结束计时，nowait 保持待结束状态。 */
void region_for_end(region_control *control, int id);
/* 既有 barrier 后的 master 入口；结束 pending nowait，不增加 barrier。 */
void region_sync(region_control *control);

#ifdef __cplusplus
}
#endif

#ifndef REGION_INSTRUMENT
#define REGION_INSTRUMENT 1
#endif

#if REGION_INSTRUMENT
#define PARALLEL_START(control, id) \
  region_parallel_start((control), (id), __FILE__, __func__, __LINE__)
#define PARALLEL_END(control, id) region_parallel_end((control), (id))
#define FOR_START(control, id) do { \
  _Pragma("omp master") \
  { region_for_start((control), (id), __FILE__, __func__, __LINE__); } \
} while (0)
#define FOR_END(control, id) do { \
  _Pragma("omp master") { region_for_end((control), (id)); } \
} while (0)
#define REGION_SYNC(control) do { \
  _Pragma("omp master") { region_sync((control)); } \
} while (0)
#else
#define PARALLEL_START(control, id) ((void)0)
#define PARALLEL_END(control, id) ((void)0)
#define FOR_START(control, id) ((void)0)
#define FOR_END(control, id) ((void)0)
#define REGION_SYNC(control) ((void)0)
#endif

#endif
