# OpenMP 线程绑定 demo

Linux 下展示不同 binding 策略的 `tid → place → CPU` 对应关系。

```bash
cd FastCheck
bash run.sh             # 4 个线程，OMP_PLACES=cores，比较四种策略
bash run.sh 8 threads   # 8 个线程，每个 place 是一个硬件线程
```

脚本分别启动四个进程，设置 `OMP_PROC_BIND=false / close / spread / primary`。
每个线程占一列，按 tid 从左到右排序，各项信息占一行。
脚本清除 `KMP_AFFINITY`、`GOMP_CPU_AFFINITY`，避免覆盖策略。

| 输出行 | 含义 |
| --- | --- |
| `tid` | 当前 OpenMP team 内的线程编号，从 0 开始 |
| `cpu` | 该线程采样时实际运行的 Linux 逻辑 CPU 编号 |
| `place` | OpenMP place 编号；`-1` 表示没有关联的 place |
| `place_cpus` | 该 place 包含的 CPU 编号 |
| `allowed_cpus` | Linux affinity mask，线程允许运行的 CPU 集合；连续编号压缩为范围 |

通常可以看到：`false` 不启用 OpenMP 绑定；`close` 在线程数少于 place 数时使用相邻
place；`spread` 将线程分散到 place 列表中；`primary` 将所有线程放在主线程所在的
place。具体 CPU 映射取决于机器拓扑、进程可用 CPU 和 OpenMP runtime。

`OMP_PLACES=cores` 时，一个 place 可能包含同一物理核的多个 SMT 硬件线程，
因此同一 place 内的线程不一定运行在同一个逻辑 CPU 上。`cpu` 是一次采样，
`allowed_cpus` 才表示允许运行的范围；`false` 下采样 CPU 后续可能变化。
place 编号也不等于 CPU 编号。CPU mask 查询使用 Linux 的固定 `CPU_SETSIZE`；
超出其容量时会报错并返回非零退出码。

单独编译、指定策略运行：

```bash
make
OMP_NUM_THREADS=4 OMP_PLACES=cores OMP_PROC_BIND=spread ./binding_demo

# 切换编译器时先清理；Clang 需要已安装 libomp。
make clean
make CC=clang-18
make clean
```

可以显式指定 place 列表，让差异更直观（CPU 编号必须在当前进程可用范围内）：

```bash
bash run.sh 4 '{0},{1},{2},{3},{4},{5},{6},{7}'
```

在该列表下，`close` 通常选择相邻的四个 place，`spread` 则间隔分布。
