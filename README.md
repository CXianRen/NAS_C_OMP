# SP / OpenMP region tuning

Benchmark 只保留 NPB SP；通用框架、独立示例和 FastCheck 测试工具继续保留。

| 目录 | 用途 |
| --- | --- |
| [framework/](framework/README.md) | HAMS 绑定、region callback、独立 timer 和 J2025 算法 |
| [NPB3.3-OMP-C/SP/](NPB3.3-OMP-C/SP/) | SP 源码，构建产物为 `NPB3.3-OMP-C/bin/SP.<CLASS>` |
| [example/](example/README.md) | 两个 region 的完整框架调用示例 |
| [FastCheck/](FastCheck/README.md) | 独立绑定检查工具 |

## SP 构建与运行

需要 Python 3、Clang AST 解析器和支持 OpenMP 的 C/C++ 编译器。

```sh
make -C NPB3.3-OMP-C CLASS=S CC=clang-18 CLANG=clang-18
OMP_NUM_THREADS=8 NPB_TIME_REPORT=1 ./NPB3.3-OMP-C/bin/SP.S
```

`CLASS` 选择数据规模。默认 `TUNER=none`、`INSTRUMENT=1`；`INSTRUMENT=0` 可关闭 region 插桩。

启用 J2025：

```sh
make -C NPB3.3-OMP-C CLASS=S TUNER=j2025 CC=clang-18 CLANG=clang-18
env -u OMP_PLACES -u KMP_AFFINITY -u GOMP_CPU_AFFINITY \
    OMP_PROC_BIND=false OMP_DYNAMIC=false OMP_NUM_THREADS=8 \
    NPB_TIME_REPORT=1 ./NPB3.3-OMP-C/bin/SP.S
```

J2025 要求 `INSTRUMENT=1`，冷启动线程上限为至少 2 的偶数，所选系统 CPU ID 均可绑定。
GNU runtime 使用 `CC=gcc CXX=g++`，AST 解析仍由 `CLANG` 指定。

## 框架与计时

`J2025 -> region_control -> HAMS` 负责配置与绑定；[timer](framework/timer/README.md) 独立计时。
`step_start/finish` 只通知迭代边界，不读取时间。每个 region 执行一次，结束后反馈本次耗时。

`NPB_TIME_REPORT=1` 输出正式迭代窗口与 region 累计秒数，`step %` 为 region 耗时除以总窗口耗时。
父子 region 耗时会重叠，不能相加。`NPB_TIME_REPORT=0` 关闭报告，J2025 仍正常采样和调优。
绑定与 callback 开销计入总窗口，不计入 region 样本。

[共享插桩器](framework/region_control/instrument_regions.py) 在构建时生成 region ID、元数据和插桩副本，
保留原 pragma、行号和 nowait 同步语义；SP 源码不被覆盖。

## 示例与测试

```sh
make -C example run
make -C framework/hams test
make -C framework/region_control test
make -C framework/j2025 test
make -C framework/timer test
make -C NPB3.3-OMP-C time-test instrument-test control-test CC=clang-18 CLANG=clang-18
```

独立示例和绑定测试自动设置 OpenMP 环境。J2025 UT 使用固定 mock 耗时，不创建线程。
清理生成产物：`./clean.sh`。
