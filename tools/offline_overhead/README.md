# SP 既有日志分析

分析已有日志，不运行程序：

```bash
python3 tools/offline_overhead/logs.py \
    scan_001.log intel_c_global_32_spread_first_touch.log \
    --out /tmp/sp-offline-log-comparison
```

输出 `comparison.md`、`regions.csv`、`comparison.json`。
只汇总顶层 parallel region，不重复累加子区间；报告区间外残差和工作负载/版本混杂。
缺少高精度计时时会明确标注 NPB 四舍五入时间；工作负载已知且不同则拒绝直接比较。

已生成的 [逐 region 对比](supplied_logs/comparison.md) 与 [原因分析](diagnosis.md)
说明为何当前两份日志的 20.35% 差异不能直接归为框架控制开销。

要测量**当前框架线程绑定，相对 OpenMP 环境变量绑定的 overhead**，使用
[binding_overhead 工具](../binding_overhead/README.md)。

纯文本解析检查（不运行 benchmark）：

```bash
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover \
    -s tools/offline_overhead -p 'test_logs.py'
```
