# Framework

| 模块 | 职责 |
| --- | --- |
| [hams](hams/README.md) | 通用线程绑定，接收 mask 和 tid → CPU 映射 |
| [region_control](region_control/README.md) | 通用 step/region callback，连接算法与 HAMS |
| [timer](timer/README.md) | 独立计时、单次采样和累计报告 |
| [j2025](../J-2025.md) | 算法部件，保存各 region 的配置和搜索状态 |

[独立示例](../example/README.md)：`make -C example run`，自动设置运行环境。
应用 benchmark 仅保留 [NPB SP](../NPB3.3-OMP-C/SP/)。
