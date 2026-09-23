# SP 全局固定配置

[`sp_global_48_spread_interleave.conf`](sp_global_48_spread_interleave.conf)
让 SP 的全部 13 个顶层 parallel region 使用同一配置：**48 threads + spread + interleave**。
覆盖初始化、正式迭代和验证；内部 `omp for` 沿用所在 parallel region 的配置。
当前 offline 格式没有全局通配键，因此逐项写入相同配置。

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
