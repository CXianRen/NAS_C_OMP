# SP / OpenMP region tuning

Benchmark 保留 NPB SP；通用框架、独立示例和 FastCheck 测试工具分别维护。

| 目录 | 用途 |
| --- | --- |
| [framework/](framework/README.md) | 内置计时的 region hook、HAMS 绑定和 J2025 算法 |
| [NPB3.3-OMP-C/SP/](NPB3.3-OMP-C/SP/) | SP 源码，直接编译为 `NPB3.3-OMP-C/bin/SP.<CLASS>` |
| [example/](example/README.md) | 两个 region 的完整调用示例 |
| [FastCheck/](FastCheck/README.md) | 独立绑定检查工具 |

## SP 构建与运行

构建只需要 Make 和支持 OpenMP 的 C/C++ 编译器。

```sh
make -C NPB3.3-OMP-C CLASS=S J2025_ENABLE=0 CC=clang-18
OMP_NUM_THREADS=8 NPB_TIME_REPORT=1 ./NPB3.3-OMP-C/bin/SP.S
```

`CLASS` 选择数据规模。SP 默认 `J2025_ENABLE=0`、`INSTRUMENT=1`；`INSTRUMENT=0` 将 region 宏编译为空操作。

启用 J2025：

```sh
make -C NPB3.3-OMP-C CLASS=S J2025_ENABLE=1 CC=clang-18
env -u OMP_PLACES -u KMP_AFFINITY -u GOMP_CPU_AFFINITY \
    OMP_PROC_BIND=false OMP_DYNAMIC=false OMP_NUM_THREADS=8 \
    NPB_TIME_REPORT=1 ./NPB3.3-OMP-C/bin/SP.S
```

`J2025_ENABLE=0/1` 映射为同名 `-D` 编译宏；example 默认开启。关闭时不注册 callback、不绑定线程、不输出 `J2025 final`，region_control 计时不受影响，`INSTRUMENT` 仍独立。
兼容 `TUNER=none/j2025`：未指定 `J2025_ENABLE` 时推导为 `0/1`，显式指定时以 `J2025_ENABLE` 为准。
启用 J2025 要求 `INSTRUMENT=1`，冷启动线程上限为至少 2 的偶数，所选系统 CPU ID 均可绑定。
GNU runtime 使用 `CC=gcc CXX=g++`。

## 手动 hook 与计时

应用在 parallel/for 前后放置 `PARALLEL_START/END`、`FOR_START/END`。
[SP 静态表](NPB3.3-OMP-C/SP/src/sp_regions.c) 保存 ID、parent 和 combined/nowait 标记；函数名、文件和行号由 START 首次捕获。
parallel 触发调优 callback；for 仅由 master 计时，保留原有 nowait 同步语义。
J2025 直接注册 callback：开始时选择 cfg 并调用 HAMS 绑定，结束时接收耗时、更新统计。HAMS 只执行绑定。

`iteration_start/end` 定义正式计时窗口。每步的 `step_start` 只通知算法，不读取时间；`step_end` 为可选通知。
每个 region 执行一次，结束后反馈单次耗时；[region_control](framework/region_control/README.md) 内置采样、累计计时和报告。

`NPB_TIME_REPORT=1` 输出累计秒数，`step %` 为 region 耗时除以总窗口耗时；父子耗时不能相加。
关闭报告仍可调优。绑定与 callback 开销计入总窗口，不计入 region 样本。

## 示例与测试

测试脚本使用 Python 3。

```sh
make -C example run
make -C framework/ut test
python3 NPB3.3-OMP-C/tests/test_build.py --cc clang-18
python3 NPB3.3-OMP-C/tests/check_region_reports.py /path/to/SP.log
```

框架 `make test` 包含 region_control、HAMS/J2025 和手动 nowait 测试；SP 构建检查直接运行脚本。
[框架 UT](framework/ut/README.md) 统一放在 `framework/ut/`。示例和绑定测试自动设置 OpenMP 环境。
清理生成产物：`./clean.sh`。
