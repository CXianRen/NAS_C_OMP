# Intel vera-r08-01

环境：2026-09-24，`vera-r08-01`，Intel Xeon Platinum 8358，2×32 cores，SMT off，NUMA `0-31 / 32-63`，Clang/libomp 18.1.8，git `4241abde432c`。

路径：`region_control -> Offline tuner -> HAMS -> LLVM OpenMP -> sched_setaffinity`。`OMP_NUM_THREADS/THREAD_LIMIT=64`，`OMP_DYNAMIC/PROC_BIND=false`；最大 team 和每方向 20 次 transition 已预热。normal/reverse 各 3 runs，每 run 3000 次（每种共 18,000 次）。

Native transition 直接执行 `omp_set_num_threads + omp parallel + sched_setaffinity`；cache-hit 的 Native 是 noinline 空边界。

| Transition | Native p50 (us) | Control p50 (us) | Paired extra (us) | Extra/native |
|---|---:|---:|---:|---:|
| cache hit | 0.001 | 0.057 | +0.056 | — |
| 64C -> 32C | 44.235 | 44.409 | +0.241 | 0.54% |
| 32C -> 64C | 85.302 | 85.667 | +0.325 | 0.38% |
| 32C -> 32S | 25.112 | 25.222 | +0.143 | 0.57% |
| 32S -> 32C | 24.883 | 25.111 | +0.201 | 0.81% |

`Paired extra` = 六个 run 各自 `(control p50 - native p50)` 的中位数；因此不等于前两列汇总值直接相减。逐 run 数据见 [vera_r08_01_intel_hot.csv](vera_r08_01_intel_hot.csv)。cache hit 每 run 为 `100 x 10,000` 次 batched calls，共 600 万次。

冷启动诊断（30 个新进程，以下为 HAMS primitive，不是完整 control 路径）：

| Case | Native p50 (us) | HAMS p50 (us) |
|---|---:|---:|
| first 64C | 2076.115 | 2067.982 |
| first 32C | 899.421 | 892.017 |
| first 32S | 911.391 | 917.439 |
| 64C -> 32C | 45.444 | 47.007 |
| 32C -> 64C | 1404.381 | 1407.690 |
| 1C -> 64C | 5314.264 | 4917.638 |
| 32C -> 32S | 31.765 | 31.038 |
| 32S -> 32C | 34.168 | 33.227 |

结论：该节点的 hot control 额外开销约 `0.14-0.33 us`，四种变化均低于 `1%`；主要成本来自 libomp team/affinity 操作。

配置：`C` 使用 CPU `0..N-1`；`32S` 使用 CPU `0,2,...,62`。结果仅代表本节点、LLVM libomp 18.1.8 和空 demo；复现节点需分配 CPU `0-63`。

复现：

```bash
./control_api_overhead/run_control_api_overhead.sh <allocated-node> hot
# 加 cold diagnostic：
./control_api_overhead/run_control_api_overhead.sh <allocated-node> all
```
