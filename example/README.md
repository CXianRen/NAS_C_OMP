# 独立运行示例

```bash
make -C example run                         # 默认 8 线程，自动设置绑定环境
make -C example run THREADS=4               # 冷启动线程上限，使用 >= 2 的偶数
make -C example -B run CC=gcc CXX=g++       # GNU OpenMP runtime
```

两个有数据依赖的 parallel for 执行 32 个 step，最后断言结果正确并输出 timer 报告。
只依赖 `framework/` 下的 `j2025/`、`region_control/`、`hams/` 和独立的 `timer/`。

```text
step_start callback
  control_region_start -> timer_sample_begin
  parallel region 执行一次
  timer_sample_end -> control_region_finish
  ... 下一个 region
step_finish callback
```

step 入口只通知算法。timer 独立计时，control 将单次耗时反馈给 J2025。
