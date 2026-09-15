# Framework

| 模块 | 职责 |
| --- | --- |
| [hams](hams/README.md) | 通用线程绑定，接收 mask 和 tid → CPU 映射 |
| [region_control](region_control/README.md) | 通用 C hook，内置采样、累计报告，触发 parallel callback |
| [j2025](j2025/README.md) | 保存配置和搜索状态，直接注册并处理 region callback |

[独立示例](../example/README.md)：`make -C example run`，自动设置运行环境。
[框架 UT](ut/README.md)：`make -C framework/ut test`，所有测试源码和构建入口集中在 `ut/`。
应用 benchmark 仅保留 [NPB SP](../NPB3.3-OMP-C/SP/)。

静态表保存 region 结构，START 宏首次捕获名称和位置；`step_start` 只通知新迭代，`step_end` 可选。

开始 callback：`region_control -> J2025 选择 cfg -> HAMS 绑定`；结束 callback：`region_control -> J2025 更新统计`。
