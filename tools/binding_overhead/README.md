# Control layer 与线程绑定 overhead

## TUNER=none：沿用用户环境，只比较 control layer

指定 `--tuner none`，或设置环境变量 `TUNER=none`，会进入 control-layer 对比路径。
这条路径**不查询亲和性、不读取 CPU 拓扑、不检查绑定策略、不手动绑核**，
完整继承用户的 `OMP_*`、`KMP_*`、`GOMP_*` 设置，也不要求 CPU mask 或 singleton places。
`--cpus`、`--threads`、`--other-threads`、`--scenario`、`--proc-bind` 不用于这条路径；
实际线程数和绑定必须由 OpenMP 环境变量指定。

在目标机器的 `benchmark` 根目录运行：

```bash
TUNER=none OMP_NUM_THREADS=32 OMP_PLACES=cores OMP_PROC_BIND=spread \
    OMP_DYNAMIC=false REGION_TIME_REPORT=0 \
    python3 tools/binding_overhead/run.py \
    --cxx clang++ --repeats 5 --out /tmp/control-overhead-none
```

先检查命令而不执行，可追加 `--dry-run`。
也可以保留同样的 OpenMP 环境变量，使用 `--tuner none` 显式选择。
需要比较开启 region 计时的成本时，改为 `REGION_TIME_REPORT=1`，或指定 `--report 1`；
默认保持调用者的报告开关，未设置则按框架默认关闭。

| 结果中的路径 | 使用的绑定 | 每次 parallel region 的 control hooks |
| --- | --- | --- |
| `native` | 用户提供的 OpenMP 环境变量 | 无 |
| `framework` | 完全相同的环境变量，真实 `TUNER=none` | step、parallel、for hooks |

两组调用真实 `region_control_init` 和 `tuner_attach`；none 必须返回空 tuner 和空 callbacks。
生产 none 分支本来就在创建 HAMS 之前返回；旧的绑定微基准直接调用 HAMS，未读取 `TUNER`，
现在已增加这条单独路径。当前 none 对照中不存在 HAMS 配置应用或绑定校验。

`summary.md` / `comparisons.csv` 中的差值为每次 parallel region 的额外 control-layer 耗时（μs/次与百分比）。
报告关闭时测 hook 调用和状态维护；报告开启时再包含 region/for 计时与累计。
两组都使用相同的批次总窗口计时，因此差值不包含一次性初始化或总窗口边界的公共成本。
这里用一个 parallel 和一个 child for-hook 对代表调用路径；它不是特定应用的端到端 overhead。
`first` 与 `steady` 分开，稳态 CV/P95 仍按独立进程均值计算。

这条路径构建真实完整框架，需要 hwloc 开发库与 OpenMP C/C++17 编译器；
`--cc` 可指定 C 编译器，否则从 `--cxx` 推导。它不会编译或运行 SP。

## 线程绑定机制对比（未选择 none 时）

以下原有路径专门比较手动绑核机制，会主动检查实际 CPU 映射；
执行这些命令时不要设置 `TUNER=none`。

直接链接当前 [`hams_binding.cpp`](../../framework/hams/hams_binding.cpp)，比较：

| 路径 | 每次应用 parallel region 前的操作 |
| --- | --- |
| `native` | `OMP_PROC_BIND=spread` + 显式 `OMP_PLACES`；固定线程数时无需 setter，切换线程数时调用 `omp_set_num_threads` |
| `framework` | `OMP_PROC_BIND=false`；调用生产 `hams_binding_apply`，然后执行相同的应用 parallel region |

两条路径使用同一个编译器/runtime、相同线程数和**相同 tid → CPU 映射**。
原生 OpenMP 映射先通过独立进程实测，框架再按该映射绑定，并在另一个进程验证。
没有采用 `floor(tid * 64 / threads)` 近似原生 spread。

## 在目标机器运行

从 `benchmark` 根目录执行。先看计划（不编译、不探测亲和性、不运行计时）：

```bash
python3 tools/binding_overhead/run.py \
    --cpus 0-63 --threads 32 --other-threads 48 --dry-run
```

测全局固定 32 线程时框架的重复配置开销：

```bash
python3 tools/binding_overhead/run.py \
    --cpus 0-63 --threads 32 --scenario fixed \
    --repeats 5 --out /tmp/binding-overhead-fixed32
```

同时测固定 32 线程和 `32 ↔ 48` 切换：

```bash
python3 tools/binding_overhead/run.py \
    --cpus 0-63 --threads 32 --other-threads 48 --scenario all \
    --iterations 1000 --batches 10 --warmup 100 --repeats 5 \
    --out /tmp/binding-overhead-32-48
```

默认 `--cxx clang++-18`；可改为目标机器可用的编译器（如 `--cxx g++`）。
只需要 Python 3、Linux、支持 OpenMP 的 C++17 编译器；不依赖 NAS、hwloc 或 SP 输入。
输出目录需不存在，避免混入旧数据。`--proc-bind close` 可改测原生 close 对照。

`--cpus` 是**可用 CPU 池**，如 `0-63` 或 `0,2,4,6`。工具为每个 CPU 建立一个 singleton place，
然后由 runtime 在整个池中 spread。若 SMT 开启，请为每个物理核心选择一个硬件线程的 CPU ID。
只有“64 核心”不能保证 CPU 编号就是 `0–63`，可用 `lscpu -e=CPU,CORE,SOCKET,NODE` 核对。
这里的 singleton places 用于严格对齐两条路径，不将 `OMP_PLACES=cores` 可能包含多个 SMT 线程的
宽 affinity 与框架的单 CPU affinity 混为同一绑定。

## 计时含义

- **first**：首次配置应用及第一个应用 parallel region，包含首次创建 team 的影响。
  基础 OpenMP 状态查询和框架上下文创建已在计时前完成；native 的主线程可能已在这些查询时绑定。
  因而这不是完整冷启动/进程启动耗时，也不是单独的 `sched_setaffinity` 时间。
- **fixed / steady**：配置保持 32 线程不变。框架每次仍调用 `hams_binding_apply`，由其真实缓存判断返回；
  原生路径每次直接进入应用 parallel region。这是检查全局固定配置日常开销的主要结果。
- **switch / steady**：32 与 48 线程连续交替。框架包含改变配置时额外的绑定 parallel region，
  原生路径包含 runtime 在应用 parallel region 上调整 team/绑定的代价。两者随后执行相同小 kernel。

应用 parallel region 仅让各线程更新各自缓存行上的计数，防止空 parallel 被编译器消除。
每批计时一次；计时内不打印、不读 affinity、不检查正确性。
warmup、mapping probe 与结果校验不计入 steady 时间。首次样本与稳态样本分别统计。
每个计时进程测完后还会复核映射；不一致则该进程失败，不进入有效统计。
这是计时后的状态检查，不宣称逐次监测了所有已计时 parallel region 的 affinity。

工具记录原始批次、各独立进程的均值以及汇总：

```text
framework_extra_us = mean(framework_us_per_region) - mean(native_us_per_region)
framework_extra_percent = (mean(framework) / mean(native) - 1) * 100
```

`unit: us`，保留正负差值。统计 mean、median、P95、CV，按独立进程重复数计算，
不会把同一进程的多个 batch 当作独立重复。steady 的 P95 是进程平均每 region 耗时的分位数，
不是单次 region 调用的尾延迟。原生和框架进程的运行顺序随机化。
5 次只是初筛；差异接近波动幅度时应增加重复数，不把很小的均值差当作稳定胜负。

## 输出与边界

输出包含 `summary.md`、`summary.csv`、`comparisons.csv`（额外 μs 与百分比）、
原始日志、CPU 映射和逐批/逐进程样本，
并保存编译命令、受控环境变量、编译器/runtime、CPU 拓扑与源码身份信息。
工具清除继承的 OpenMP/tuner 控制变量，明确设置两条路径的控制参数。
绑定 probe 若发现实际 mapping 与预期不同，或原生 tid 顺序无法被当前框架的升序映射表达，会终止比较。

这是**绑线程机制微基准**。不含 offline 配置文件读取、region callback、region 计时、SP 内存访问等开销，
因此不能直接把结果当作 SP 总 slowdown。它能回答“固定配置的缓存路径多花多少时间”和
“切换配置时，框架的额外 parallel 绑核过程相比原生 runtime 多花多少时间”。

两份 SP 日志的独立分析见 [日志诊断](../offline_overhead/diagnosis.md)。
工具代码只做编译/静态检查与 Python mock 检查；本次没有运行真实性能测量。
