# 独立运行示例

```bash
make -C example run J2025_ENABLE=1          # 默认开启 J2025，8 线程
make -C example run J2025_ENABLE=0          # 关闭调优，保留计时和报告
make -C example run THREADS=4               # 冷启动线程上限，使用 >= 2 的偶数
make -C example -B run CC=gcc CXX=g++       # GNU OpenMP runtime
```

两个有数据依赖的 parallel for 执行 32 个 step，最后断言结果正确并输出计时报告。
示例使用手动桩：`iteration_start/end`、`step_start`、`PARALLEL_START/END`，计时和报告由 region_control 内置处理。
`J2025_ENABLE` 默认 `1`，在同一构建目录切换会自动重建；设为 `0` 时不链接 J2025/HAMS，`attach/detach` 由头文件内联为空操作。

```text
j2025_attach(control)
  J2025 创建搜索状态和共享 binding，直接注册自己的 callback
iteration_start(control)
  step_start(control, step)
    PARALLEL_START -> J2025 选择 cfg -> HAMS 绑定 -> 开始计时
    原 parallel region 执行一次
    PARALLEL_END -> 结束计时 -> J2025 更新统计
iteration_end(control)
j2025_detach(runtime)
```

静态 metadata 只填写 parent、combined 和 nowait；首次 START 自动记录函数名、文件和行号。
`step_start` 只通知算法；`step_end` 可选。J2025 负责配置和统计，HAMS 只执行线程绑定。
所有实现来自 `framework/`，无应用专用 adapter 或生成脚本。
