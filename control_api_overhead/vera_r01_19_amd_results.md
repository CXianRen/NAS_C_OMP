# AMD vera-r01-19

环境：2026-09-24，AMD EPYC 9354，2×32 cores，SMT off，CPU `0-63`，8 NUMA nodes，Linux `5.14.0-687.39.1.el9_8.x86_64`，Clang/libomp 18.1.8，hwloc 2.10.0，governor `performance`，boost on，git `4241abde432c`。

路径：`region_control -> Offline tuner -> HAMS -> LLVM OpenMP -> sched_setaffinity`。`OMP_NUM_THREADS/THREAD_LIMIT=64`，`OMP_DYNAMIC/PROC_BIND=false`，`OMP_WAIT_POLICY/KMP_BLOCKTIME` unset。最大 team 和每方向 20 次 transition 已预热；normal/reverse 各 3 runs，每 run 3000 次（每种共 18,000 次）。

Native transition 直接执行 `omp_set_num_threads + omp parallel + sched_setaffinity`；cache-hit 的 Native 是 noinline 空边界。

| Transition | Native p50 (us) | Control p50 (us) | Paired extra (us) | Extra/native |
|---|---:|---:|---:|---:|
| cache hit | 0.001 | 0.063 | +0.062 | — |
| 64C -> 32C | 48.814 | 49.164 | +0.155 | 0.32% |
| 32C -> 64C | 121.359 | 121.984 | -0.041 | -0.03% |
| 32C -> 32S | 44.077 | 44.073 | +0.006 | 0.01% |
| 32S -> 32C | 43.586 | 43.606 | +0.060 | 0.14% |

`Paired extra` 是六个 run 各自 `(control p50 - native p50)` 的中位数；负值及接近零的值表示差异低于运行波动。逐 run 数据见 [vera_r01_19_amd_hot.csv](vera_r01_19_amd_hot.csv)。cache hit 共 600 万次 batched calls。

冷启动诊断（30 个新进程，HAMS primitive，不是完整 control 路径）：

| Case | Native p50 (us) | HAMS p50 (us) |
|---|---:|---:|
| first 64C | 9294.416 | 8683.059 |
| first 32C | 1679.310 | 1597.996 |
| first 32S | 1696.024 | 1728.820 |
| 64C -> 32C | 73.216 | 78.243 |
| 32C -> 64C | 3615.737 | 4551.464 |
| 1C -> 64C | 11303.498 | 11109.015 |
| 32C -> 32S | 52.720 | 54.242 |
| 32S -> 32C | 59.299 | 58.358 |

完整 cold 汇总见 [vera_r01_19_amd_cold.csv](vera_r01_19_amd_cold.csv)。

结论：AMD 上 control cache-hit p50 为 `63 ns`，相对空边界增加约 `62 ns`；四种 hot transition 的配对中位差绝对值不超过 `0.16 us`（不超过 `0.32%`），未观察到 control API 放大 transition overhead。

配置：`32C=CPU 0-31`（socket 0，NUMA 0-3）；`32S=CPU 0,2,...,62`（两路、全部 8 NUMA）。绝对 transition 时间不应与 Intel 节点直接作同 NUMA 语义比较。
