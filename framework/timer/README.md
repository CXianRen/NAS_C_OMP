# Timer

独立 C 计时模块，只依赖 OpenMP。调用方持有 `region_timer` 和 region 元数据，决定是否累计、输出报告；模块不读取环境变量。

```c
#include "region_timer.h"

const timer_region_info regions[] = {{"kernel", -1, 1}};
region_timer timer;
region_timer_init(&timer, regions, 1, 1); /* 最后一个参数启用报告累计。 */
region_timer_begin(&timer);             /* 正式总计时窗口。 */
double begin = region_timer_sample_begin(&timer, 0);
/* 执行原 parallel region 一次。 */
double seconds = region_timer_sample_end(&timer, 0, begin);
/* 将 seconds 交给 control；callback、绑定放在这对采样之外。 */
region_timer_end(&timer);
region_timer_report(&timer);
```

- `sample_begin/end` 始终各读一次时钟，关闭报告仍可给调优方法提供样本。
- `start/stop/nowait_start/sync` 仅在报告窗口内读时钟；nowait 与后续边界共用时间戳。
- 每个 context 由同一个协调线程操作；parallel 内的计时入口只由 master 执行，不支持同 context 的嵌套 parallel。
- `parent=-1` 表示外层 parallel；`combined=1` 使用同一耗时显示 parallel/for 两行。
- 只计总时间时使用 `region_timer_init(&timer, NULL, 0, 0)`。

测试：`make -C framework/timer test`；GNU 编译器：`make -C framework/timer -B test CC=gcc`。
