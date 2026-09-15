# J2025

`j2025.h/.cpp` 负责搜索和生成 `hams_binding_cfg`；`j2025_select_cfg()` 选择本次配置，
`j2025_observe()` 接收 region_control 提供的单次耗时并更新搜索状态。

`j2025_runtime.h` 中 `J2025_ENABLE` 默认是 `1`；直接编译可用 `-DJ2025_ENABLE=0` 将 attach/detach 变为空操作，调用处无需 `#ifdef`。
Make 使用 `J2025_ENABLE=0/1` 传入同名宏，SP 默认关闭、example 默认开启。关闭时不注册 callback、不绑定、不输出 `J2025 final`，region_control 计时不受影响。

启用时，应用使用 C ABI 的 `j2025_runtime.h`：先初始化 `region_control`，再调用
`j2025_attach(&control)` 创建搜索状态和共享 binding，直接注册 J2025 callback：

- parallel 开始：`j2025_select_cfg()` → `hams_binding_apply()`。
- parallel 结束：`j2025_observe()` 更新统计与搜索状态。
- 当前搜索由 region 样本推进，step callback 留空。

这些 callback 实现在 `j2025_runtime.cpp` 中。结束后调用 `j2025_detach(runtime)` 注销并释放；
应用持有的 `control` 须存活至 detach 完成。HAMS 仅接收 cfg 执行绑定。

`j2025_detach()` 向 `j2025_destroy()` 传入 region 元数据，逐 region 输出最后一次选中的配置；STABLE 时即为收敛配置，从未选择过配置的 region 跳过。
名称复用 region_control 自动记录的函数名和行号，与计时报告一致。直接调用 `j2025_destroy()` 且不提供元数据时只释放内存。
仅析构时输出，不受 `NPB_TIME_REPORT` 影响。mask 按冷启动 `max_threads` 位宽从高位到低位显示，例如上限为 8：

```text
J2025 final region=z_solve:52 threads=4 mask=01010101
```

```sh
make -C framework/ut j2025-test
make -C example run
```

[纯搜索 UT](../ut/j2025_ut.cpp) 只编译 `j2025.cpp`，不链接 OpenMP 或 `j2025_runtime.cpp`。
[runtime UT](../ut/j2025_runtime_ut.cpp) 验证实际绑定、跨 region 恢复和耗时反馈；两者集中在 `framework/ut/`。
运行时初始线程上限须为不小于 2 的偶数，
且 CPU `0..max_threads-1` 均可绑定。[搜索方法](../../J-2025.md) 保持满线程预热、mapping 比较、Fibonacci 线程数搜索和稳定配置。
