# Framework UT

所有框架测试统一在本目录，常规产物放在 `build/`。

```sh
make -C framework/ut test                              # 全部测试，默认 8 线程
make -C framework/ut -B test CC=gcc                     # GCC / libgomp
make -C framework/ut hams-test THREADS=8
make -C framework/ut j2025-test
make -C framework/ut region-control-test
```

| 测试源码 | 检查内容 |
| --- | --- |
| [hams_binding_ut.cpp](hams_binding_ut.cpp) | 满线程、半线程 close/spread 的实际 affinity 与 bitmask |
| [j2025_ut.cpp](j2025_ut.cpp) | mock 搜索、配置映射和 region 状态隔离，无 OpenMP 运行依赖 |
| [j2025_runtime_ut.cpp](j2025_runtime_ut.cpp) | 实际绑定、跨 region 恢复、样本反馈和注销 |
| [region_control_ut.c](region_control_ut.c) | callback、计时和报告，同时测试关闭插桩的构建 |
| [region_nowait_test.c](region_nowait_test.c) | 真实并发下的 nowait、已有 barrier/join 与跳过、重复调用 |

运行环境由 Makefile 自动设置，`CC=gcc` 自动选择 `g++`。切换编译器时使用 `-B` 重编译。
`region-control-test` 包含 [nowait 测试脚本](test_nowait.py)，其临时构建在测试后自动清理。
