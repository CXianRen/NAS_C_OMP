# SP offline 配置

## 全局 48 threads + spread + interleave

[`sp_global_48_spread_interleave.conf`](sp_global_48_spread_interleave.conf)
让 SP 的全部 13 个顶层 parallel region 使用同一配置：**48 threads + spread + interleave**。
文件列出全部顶层 region；内部 `omp for` 沿用所在 parallel region 的配置。
当前 offline 格式没有全局通配键，因此逐项写入相同配置。

**生效时机：当前 framework 仅在正式迭代的 step 内应用 offline 配置。**
初始化和 warmup 尚未应用这些配置，受启动线程数和启动时亲和性影响；
验证阶段沿用最后的线程数和绑定，不重新应用对应 region 的配置。
因此文件包含 `initialize` 等条目，并不表示初始化已经按其绑定。

目标假设：64 个物理核心各取一个硬件线程，对应系统 CPU ID `0–63`，
这些 CPU 全部可用，且编号顺序适合跨目标机拓扑分散。
这里的 spread 使用仓库 `framework/j2025/j2025.cpp` 的 SCATTER 规则：
`CPU(tid) = floor(tid * 64 / 48)`，`tid = 0,...,47`。
即每 4 个 CPU 选前 3 个，mask 为 `0x7777777777777777`，恰好选择 48 个 CPU。
若目标机编号、SMT 或可用 CPU 集不同，需要相应调整 mask。
这个显式映射不保证逐线程复现目标 OpenMP runtime 的 `OMP_PROC_BIND=spread`；
后者还取决于 runtime、place 顺序和拓扑。

在**目标机器**的 `benchmark` 根目录执行，以下用 Class D 举例；
可同时替换构建命令和运行路径中的 Class，配置文件不区分 Class。
需要 Clang/libomp、C++17 编译器、hwloc 开发库、Python 3、GNU Make 4.3+ 和 numactl。
编译器名称可按目标机安装情况调整。

```bash
make -C NPB3.3-OMP-C -j8 BENCHMARKS=SP CLASS=D INSTRUMENT=1 \
    CC=clang-18 CXX=clang++-18 CLANG=clang-18

env -u OMP_PLACES -u KMP_AFFINITY -u GOMP_CPU_AFFINITY -u NPB_NITER \
    OMP_NUM_THREADS=48 OMP_THREAD_LIMIT=48 OMP_DYNAMIC=false \
    OMP_PROC_BIND=false TUNER=offline REGION_TIME_REPORT=1 \
    OFFLINE_CONFIG="$PWD/cfg/sp_global_48_spread_interleave.conf" \
    numactl --interleave=all ./NPB3.3-OMP-C/bin/SP.D
```

`OMP_PROC_BIND=false` 是当前 offline 模式的要求：framework 按文件中的 mask
逐线程绑定。48 线程由环境变量与文件共同指定；64 核心的分散范围由 mask 指定。
内存 interleave 不属于 offline 文件格式，由 `numactl --interleave=all` 在进程启动时设置。

配置键对应当前 SP 源码的 `function:pragma行号`。目标机器应使用相同版本；
源码行号变化后，按 `.build/SP.<CLASS>/generated/instrumentation.json` 中
`parent == -1` 的条目更新配置。正常加载会输出 `Offline loaded ... entries=13`。

本次仅生成配置并静态核对 region、线程数和 mask；未编译或运行 benchmark，
未在本平台探测目标机绑定效果或性能。

## Intel Class C：逐 region 配置，default memory policy

[`sp_intel_C_offline.conf`](sp_intel_C_offline.conf) 根据提供的 Intel Class C JSON 生成，
默认问题规模为 `162 x 162 x 162`，迭代 400 次。
沿用上述 CPU `0–63` 的目标机假设及均匀分散规则：
`CPU(tid) = floor(tid * 64 / threads)`，不保证复现目标 runtime 的具体 spread 映射。

| Region | Threads | CPU mask |
| --- | ---: | --- |
| `compute_rhs:43` | 48 | `0x7777777777777777` |
| `x_solve:48` | 32 | `0x5555555555555555` |
| `y_solve:48` | 32 | `0x5555555555555555` |
| `z_solve:52` | 24 | `0x2525252525252525` |
| 其余全部顶层 parallel region | 32 | `0x5555555555555555` |

文件显式覆盖全部 13 个顶层 region，以实现 JSON 中的 `fallback=32 threads + spread`。
不能省略其余 region：offline 自身的缺项回退会使用启动线程上限及连续 CPU 映射。
运行时线程上限设置为 48，以容纳 `compute_rhs`；各 region 实际线程数由配置文件决定。

在目标机器的 `benchmark` 根目录、默认内存策略环境下执行：

```bash
make -C NPB3.3-OMP-C -j8 BENCHMARKS=SP CLASS=C INSTRUMENT=1 \
    CC=clang-18 CXX=clang++-18 CLANG=clang-18

env -u OMP_PLACES -u KMP_AFFINITY -u GOMP_CPU_AFFINITY -u NPB_NITER \
    OMP_NUM_THREADS=48 OMP_THREAD_LIMIT=48 OMP_DYNAMIC=false \
    OMP_PROC_BIND=false TUNER=offline REGION_TIME_REPORT=1 \
    OFFLINE_CONFIG="$PWD/cfg/sp_intel_C_offline.conf" \
    ./NPB3.3-OMP-C/bin/SP.C
```

本配置使用 `default` 内存策略，运行命令不加 `numactl --interleave=all`。
运行目录中不要放置覆盖默认规模和迭代次数的 `inputsp.data`。
JSON 中的 `27.726897001 s` 是原始全局 32-thread spread 配置的测量值；
各配置仅测量一次，当前逐 region 组合没有测量结果。
本次只生成文件并做静态核对，未编译或运行 SP。

## 全局 32 threads + spread + default

[`sp_global_32_spread_default.conf`](sp_global_32_spread_default.conf)
让全部 13 个顶层 parallel region 固定使用 **32 threads + spread + default memory policy**，
配置在正式迭代的 step 内生效。沿用 CPU `0–63` 对应 64 个物理核心的假设，
按 `CPU(tid) = 2 * tid` 绑定到 `0,2,4,...,62`，mask 为 `0x5555555555555555`。
该均匀分散映射不保证与目标 OpenMP runtime 的 spread 映射完全一致。

可复用上面的 Class C 构建命令；在目标机器的 `benchmark` 根目录、默认内存策略环境下运行：

```bash
env -u OMP_PLACES -u KMP_AFFINITY -u GOMP_CPU_AFFINITY -u NPB_NITER \
    OMP_NUM_THREADS=32 OMP_THREAD_LIMIT=32 OMP_DYNAMIC=false \
    OMP_PROC_BIND=false TUNER=offline REGION_TIME_REPORT=1 \
    OFFLINE_CONFIG="$PWD/cfg/sp_global_32_spread_default.conf" \
    ./NPB3.3-OMP-C/bin/SP.C
```

配置文件不区分 Class；若换 Class，使用对应构建和可执行文件。
本次仅静态核对配置，未编译或运行 SP。
