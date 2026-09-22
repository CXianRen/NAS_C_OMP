# Benchmark 示例日志

这里保存当前分支 18 个可执行 benchmark 的真实小规模运行输出。每份 `.log`
包含构建命令、运行命令、环境、UTC 时间和工作目录，以及未经编辑的合并
stdout/stderr；末尾的 `# exit_code: 0` 表示正常退出。构建输出不混入运行日志。

统一使用 Clang 18、`INSTRUMENT=1` 和以下运行设置：

```sh
OMP_NUM_THREADS=4 OMP_DYNAMIC=false OMP_PROC_BIND=false \
TUNER=none REGION_TIME_REPORT=1
```

这些日志展示程序输出和 framework 的 region 计时格式，来自单次小规模运行，
不作为性能比较结果。`parallel` 与其内部 `for` 行可能重叠；combined
`parallel for` 的两行使用同一份耗时，不能相加。

| Benchmark | 示例规模 | 日志 |
| --- | --- | --- |
| NAS BT | Class S，默认参数及完整迭代 | [BT.S.log](nas/BT.S.log) |
| NAS CG | Class S，默认参数及完整迭代 | [CG.S.log](nas/CG.S.log) |
| NAS DC | Class S，默认参数 | [DC.S.log](nas/DC.S.log) |
| NAS EP | Class S，默认参数 | [EP.S.log](nas/EP.S.log) |
| NAS FT | Class S，默认参数及完整迭代 | [FT.S.log](nas/FT.S.log) |
| NAS IS | Class S，默认参数及完整迭代 | [IS.S.log](nas/IS.S.log) |
| NAS LU | Class S，默认参数及完整迭代 | [LU.S.log](nas/LU.S.log) |
| NAS MG | Class S，默认参数及完整迭代 | [MG.S.log](nas/MG.S.log) |
| NAS SP | Class S，默认参数及完整迭代 | [SP.S.log](nas/SP.S.log) |
| NAS UA | Class S，默认参数及完整迭代 | [UA.S.log](nas/UA.S.log) |
| LULESH | `-s 8 -i 16` | [lulesh.log](lulesh/lulesh.log) |
| Rodinia Hotspot | 16×16，3 次更新，随附温度与功率输入 | [hotspot.log](rodinia/hotspot.log) |
| Rodinia Streamcluster | 128 个三维点，每批 64 点 | [streamcluster.log](rodinia/streamcluster.log) |
| Rodinia ParticleFilter | 16×16，3 帧，100 粒子 | [particle_filter.log](rodinia/particle_filter.log) |
| Rodinia CFD float | 合成单单元网格，完整 2,000 次迭代 | [euler3d_cpu.log](rodinia/euler3d_cpu.log) |
| Rodinia CFD double | 同上 | [euler3d_cpu_double.log](rodinia/euler3d_cpu_double.log) |
| Rodinia CFD pre float | 同上 | [pre_euler3d_cpu.log](rodinia/pre_euler3d_cpu.log) |
| Rodinia CFD pre double | 同上 | [pre_euler3d_cpu_double.log](rodinia/pre_euler3d_cpu_double.log) |

NAS 十个程序均通过原有的 `Verification = SUCCESSFUL` 校验。其他程序的输入、
复现命令和结果检查见 [NAS](nas/README.md)、[LULESH](lulesh/README.md)
和 [Rodinia](rodinia/README.md)。程序生成的计算结果写入临时目录；需要外部输入的
Rodinia 示例已随日志保存输入文件。
