# Control API overhead benchmark

空 demo 对比完整 control 路径与直接 OpenMP affinity 操作。

- [Intel vera-r08-01 结果](vera_r08_01_intel_results.md)
- [AMD vera-r01-19 结果](vera_r01_19_amd_results.md)
- Benchmark：`control_api_overhead_benchmark.cpp`
- Runner：`run_control_api_overhead.sh`

```bash
./control_api_overhead/run_control_api_overhead.sh <allocated-node> all
```
