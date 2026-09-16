# OpenMP tuning framework

`region_control` 提供 hook 和计时，`tuner` 选择配置，`HAMS` 应用线程数及 CPU 绑定。

| `TUNER` | 搜索状态 | 配置生效位置 | 反馈耗时 |
| --- | --- | --- | --- |
| `none`（默认） | 无 | 不修改配置 | 仅保留可选计时报告 |
| `dummy` | 固定初始满线程配置 | `PARALLEL_START` | 单个 region，接收后忽略 |
| `j2025` | 每个 region 独立 | `PARALLEL_START` | 单个 region |
| `j2025_b` | 每个 region 独立 | `PARALLEL_START` | 单个 region |
| `otter` | 全局一份 | `step_start` | 整个 step，包含串行部分 |

## 1. Framework

```text
control = region_control_init(region 表, 是否输出报告)
tuner = tuner_attach(control)                 // 按环境变量 TUNER 挂载 callback
iteration_start()                             // 正式总计时窗口

for 每个 step:
    step_start():
        if tuner == Otter:
            反馈尚未结束的上一 step 样本
            apply_config(Otter.select_cfg())
            开始整步计时

    for 本 step 实际执行的每个 parallel region R:
        PARALLEL_START(R):
            if tuner in {J2025, J2025_B, Dummy}:
                apply_config(tuner.select_cfg(R))
            按需开始 region 计时

        执行 R 一次                           // 内部 for 仅计时

        PARALLEL_END(R):
            按需结束 region 计时
            if tuner in {J2025, J2025_B, Dummy}: tuner.observe(R, region 耗时)

    step_end()  // 可省略；Otter 结束整步计时并反馈，不切换配置

iteration_end()  // 结算 Otter 最后一个未结束的 step，再结束总窗口
region_report()
tuner_detach()   // 注销并释放；J2025/J2025_B 逐 region 输出，Otter 仅输出一条配置

apply_config(cfg):
    允许绑定 → HAMS.apply(cfg)；否则 → 仅设置线程数

HAMS.apply(cfg):
    if cfg 与当前配置相同: return
    设置默认线程数；按 cfg 的 tid → CPU 映射绑定线程
```

tuner 样本排除配置选择、绑定和反馈开销，总窗口包含这些开销；关闭报告仍向 tuner 提供样本。

Dummy 保留上述挂载、选择、绑定、计时和反馈流程：

```text
attach: 初始配置 = (启动线程上限 N, tid → CPU tid)
select_cfg(region): return 初始配置
observe(region, 耗时): return
```

`N` 取启动时 OpenMP 线程上限（受线程限制和 HAMS 容量限制），不受 Otter 参数影响；每个 region 始终使用这一个配置。

## 2. J2025：先选绑定，再搜线程数

`N` 为初始线程上限（至少为 2 的偶数）。每个 region 独立推进下列状态；每次调用只执行一个配置。

```text
WARMUP:
    用 (N, SCATTER) 执行一次，忽略耗时
    → MAPPING

MAPPING:
    后续两次调用分别测量 (N, SCATTER)、(N, CLOSE)
    best_mapping = 耗时较小者
    → SEARCH

SEARCH:
    固定 best_mapping，在 {2, 4, ..., N} 上做 Fibonacci 搜索
    选择下一个未测线程数，执行一次，缓存耗时并缩小区间
    没有待测候选 → STABLE

STABLE:
    每次进入该 region，恢复其已测最快配置
```

## 3. J2025_B：先搜线程数，再选绑定

独立实现在 `framework/j2025_b`，有自己的状态和 API；按 region 调优。`CLOSE` 即连续 CPU 绑定（CONTIGUOUS）。

```text
WARMUP:
    用 (N, CLOSE) 执行一次，忽略耗时
    → SEARCH

SEARCH:
    固定 CLOSE，在 {2, 4, ..., N} 上做 Fibonacci 搜索
    每次执行一个未测线程数，缓存耗时并缩小区间
    没有待测候选 → 固定已测最快线程数 best_NT → MAPPING

MAPPING:
    后续两次调用分别测量 (best_NT, SCATTER)、(best_NT, CLOSE)
    使用这两个新样本选择较快绑定；相同耗时选 SCATTER
    → STABLE

STABLE:
    每次进入该 region，恢复 (best_NT, best_mapping)
```

## 4. Otter：先搜线程数，再选绑定

`N` 为线程上限，`ε` 为阈值（默认 10%）。全局状态跨 step 推进，**仅在 `step_start` 应用配置，同一步所有 region 共用它**。

```text
用 (N, CONTIGUOUS) 预热两个 step，忽略耗时
固定 CONTIGUOUS，在后续三个 step 分别测量 N、N/2、3N/4
    // 线程数向下取整，至少为 1；每个样本是完整 step 的耗时

if |time[N] - time[3N/4]| / min(time[N], time[3N/4]) ≤ ε:
    用三个样本做 Newton 二次插值
    在 [N/2, N] 中，选预测时间 ≤ 最佳预测时间 × (1+ε) 的最小线程数
    插值无效时回退到已测最快线程数
else:
    在 [1, N] 中做整数黄金分割搜索，复用已有样本
    区间足够小时补测两端，选已测最快线程数

固定所选线程数：
    若支持绑定，比较 CONTIGUOUS 和 SCATTER
    每种绑定先预热一个 step，再测量一个 step，保留较快者

搜索结束：后续 step 复用统一配置
```

源码：[挂载](framework/tuner/tuner.cpp) · [计时](framework/region_control/region_control.c) · [绑定](framework/hams/hams_binding.cpp) · [Dummy](framework/dummy/dummy.cpp) · [J2025](framework/j2025/j2025.cpp) · [J2025_B](framework/j2025_b/j2025_b.cpp) · [Otter](framework/otter/otter.cpp)。

分阶段伪代码：[J2025](framework/j2025/J-2025.md) · [J2025_B](framework/j2025_b/J-2025_B.md) · [Otter](framework/otter/Otter.md)。

## 运行与检查

需要 OpenMP C/C++17 编译器和 hwloc。绑定所选的 CPU 须可用。

```sh
make -C NPB3.3-OMP-C CLASS=S CC=clang-18
env -u OMP_PLACES -u KMP_AFFINITY -u GOMP_CPU_AFFINITY \
    OMP_PROC_BIND=false OMP_DYNAMIC=false OMP_NUM_THREADS=8 \
    TUNER=otter NPB_TIME_REPORT=1 ./NPB3.3-OMP-C/bin/SP.S

make -C example run TUNER=otter
make -C framework/ut test
python3 NPB3.3-OMP-C/tests/test_build.py --cc clang-18
```

切换 `TUNER=none|dummy|j2025|j2025_b|otter` 无需重编译。Dummy、J2025 与 J2025_B 需要 `OMP_PROC_BIND=false`。`INSTRUMENT=0` 关闭 region hook，此构建仅允许 `TUNER=none`。
Otter 默认打印搜索过程（step、配置、耗时 `time_us`、预热标记、搜索分支和状态转换），收敛后停止逐步打印；`OTTER_VERBOSE=0` 仅保留最终配置。过程日志不受 `NPB_TIME_REPORT` 控制，打印不计入 tuner 样本。
独立绑定工具见 [FastCheck](FastCheck/README.md)。
