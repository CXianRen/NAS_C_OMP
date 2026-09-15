# Region control

通用 C 手动插桩层，内置计时状态、采样和报告，持有可选 callback/context。
接入 [J2025](../j2025/README.md) 时，由 J2025 直接注册 callback，在开始 callback 中选择配置并调用 HAMS，在结束 callback 中更新统计。

```c
#include "region_control.h"

region_info regions[] = {{.parent = -1, .combined = 1}};
region_control control;
region_control_init(&control, regions, 1, 1); /* 最后一个参数启用报告。 */
/* 可选：region_control_register(&control, &callbacks, context); */
iteration_start(&control);
for (int t = 0; t < steps; ++t) {
    step_start(&control, t);          /* 仅通知，不读时钟。 */
    PARALLEL_START(&control, 0);
    #pragma omp parallel for
    for (int i = 0; i < size; ++i) work(i);
    PARALLEL_END(&control, 0);
}
iteration_end(&control);
region_report(&control);
```

- `parallel_start` callback → 开始计时 → 原 parallel → 结束计时 → `parallel_end` callback。callback 仅在正式窗口的 step 内触发；关闭报告仍提供单次耗时。
- 四个 callback 是 `step_start`、`parallel_start`、`parallel_end`、`step_end`，全部可为空。`region_control_register(&control, NULL, NULL)` 注销，普通计时继续可用。
- `step_end` 可选；循环中只调用 `step_start` 即可通知每个新 step，`iteration_end` 不补发结束 callback。
- region 表由调用方持有；`parent=-1` 表示 parallel，内部 for 填父 ID；`combined=1` 显示相同耗时的 parallel/for 两行。
- `name` 初始为空，首次 START 自动保存 `__FILE__`、`__func__`、`__LINE__`；报告显示 `函数:START行号`，后续执行不处理字符串。
- 内部 for 使用 `FOR_START/END(&control, id)`，宏自带 `omp master`，不触发 callback。表中 `nowait=1` 的 END 延后到下一 for、已有 barrier 后的 `REGION_SYNC(&control)` 或 parallel join，不增加 barrier。
- `REGION_INSTRUMENT=0` 将 region 宏变为 no-op，保留 iteration 总计时。一个上下文由一个协调线程使用，不支持 nested parallel。

测试：`make -C framework/ut region-control-test`；GNU：`make -C framework/ut -B region-control-test CC=gcc`。
