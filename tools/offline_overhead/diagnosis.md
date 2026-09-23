# 两份 Intel SP.C 日志的诊断

日志显示 offline 这次慢了 **20.35%**，但不能据此声称 offline 控制逻辑有 20.35% overhead。
当前证据首先要求排查初始化/first-touch 条件与 CPU 映射差异。
完整逐 region 表见 [生成报告](supplied_logs/comparison.md)，原始输入为
[`scan_001.log`](../../scan_001.log) 和
[`intel_c_global_32_spread_first_touch.log`](../../intel_c_global_32_spread_first_touch.log)。

## 已测量的差异

| 项目 | 原始扫描 | Offline | 差值 |
| --- | ---: | ---: | ---: |
| iteration total | 27.726897001 s | 33.370447159 s | +5.643550158 s |
| 9 个顶层 parallel region 合计 | 27.726076366 s | 33.367914200 s | +5.641837834 s |
| total − 顶层合计 | 0.820635 ms | 2.532959 ms | +1.712324 ms |
| compute_rhs | 8.677172661 s | 12.586781502 s | +3.909608841 s |

顶层 parallel region 内的耗时增量占总增量 **99.97%**；`compute_rhs` 单独占 **69.28%**，
自身变慢 **45.06%**。`add`、`txinvr`、`tzetar` 分别变慢约 52.83%、41.58%、34.51%；
`x_solve` / `y_solve` 只变慢约 4.2%，`z_solve` 反而快约 2.0%。
这优先指向 region 执行条件变化，而不是按调用次数累加的固定控制成本。

这里仅加顶层 parallel 行。combined 的 for 行是同一份计时，不能再次累加。
旧日志的 rhs 子区间分组与新日志不同，不能把这些 child 行逐项匹配。
残差包含串行语句、控制逻辑和计时边界误差；跨版本日志中的残差差值不是纯 tuner 成本或严格上界。

## 当前代码确认的机制

1. **Offline 配置在初始化之后才应用。**
   [SP 主流程](../../NPB3.3-OMP-C/SP/src/sp.c) 先调用 `exact_rhs()`、`initialize()`、warmup `adi()`，
   然后 `iteration_start()` 和 `step_start()`。
   [region_parallel_start](../../framework/region_control/region_control.c) 仅在
   `control->running && control->in_step` 时调用绑定 callback。
   配置中列出 `initialize` 并不能改变这个时机。原 cfg README 的初始化生效说明已修正。

2. **两次运行的启动条件没有对齐。**
   原始扫描明确 `OMP_NUM_THREADS=32 OMP_PROC_BIND=spread OMP_PLACES=cores`；
   offline 日志启动最大线程数是 64，随后报告正式配置为 32。
   `max_threads=64` 不是实际 warmup team size 的测量，但它已经显示两次启动条件不同。
   在当前实现中，offline 尚未给初始化应用 mask，页面首次访问可能因此不同。
   日志没有 NUMA 页面分布，不能把“远端访问导致 5.64 秒”写成已证明的结论。
   64 到 32 的切换还可能影响 worker pool；这是与页面放置一起需要控制的因素。

3. **两个 spread 没有证据是相同映射。**
   原扫描由 OpenMP runtime 根据 `cores` 的 place 顺序分散；offline 固定 CPU `0,2,...,62`，
   [offline parser](../../framework/offline/offline.cpp) 按置位 CPU 升序分配 tid。
   目标机器的 NUMA、SMT、CPU 编号、runtime place 顺序都未在原日志里记录。
   名称相同不代表逐线程绑定相同。

4. **全局固定配置不会在每个 region 真正重绑。**
   [hams_binding_apply](../../framework/hams/hams_binding.cpp) 在线程数和 mask 未变化时立即返回；
   [offline_select_cfg](../../framework/offline/offline.cpp) 缓存每个 region 的结果，`offline_observe` 为空。
   当前全局 32-thread 配置仅在第一次正式应用时执行绑核，之后走缓存。
   绑定 callback 位于 region 计时起点之前，observe 位于终点之后；它们不会直接解释 rhs 内增加的 3.91 秒。
   控制逻辑造成的间接缓存/调度影响仍可能落在 region 时间内，不能由残差排除所有间接影响。

5. **还存在源码/插桩版本及重复次数的混杂。**
   编译日期分别为 9 月 11 日、9 月 23 日，rhs 子区间标签也不同；日志不足以证明编译器、runtime、
   CPU 频率和节点负载相同。旧结果又是扫描选出的最小值，且每配置仅一个样本，不能直接作为稳定 overhead 基线。
   当前源码分析解释的是这个 checkout 的机制；目标二进制若来自其他版本，需由工具在同 checkout 重建验证。

## 如何检查线程绑定开销

使用 [binding_overhead 工具](../binding_overhead/README.md)，直接比较生产
`hams_binding_apply` 与 OpenMP 环境变量绑定。优先看 fixed / steady 的 32-thread 结果；
再看 switch / steady 的 32 与 48 线程切换结果。
两条路径在独立 probe 中验证相同 tid → CPU 映射，计时期间不进行亲和性探测。

该工具隔离线程绑定机制，不包含 SP 数据访问、offline 文件读取或计时回调。
它不能单独验证 first-touch 是否导致本例 5.64 秒差异。
若继续定位 SP slowdown，下一步应在同版本程序上控制初始化前的线程数和绑定，
并记录页面分布，再比较正式迭代时间。
`REGION_TIME_REPORT=0` 仍会给 offline 的 end callback 采样，不能当作完全无计时。

本次只解析已有日志、审查源码和验证工具代码；未运行 SP 或测量本平台性能。
