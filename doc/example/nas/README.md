# NAS Class S 示例

十份日志使用完整 Class S 默认参数，没有设置 `NPB_NITER`，也没有测试包装器。
所有程序正常退出，原始数值校验均为 `Verification = SUCCESSFUL`，并输出
framework 的 `time report`。

在仓库根目录构建：

```sh
make -C NPB3.3-OMP-C -j4 CLASS=S INSTRUMENT=1 \
  CC=clang-18 CXX=clang++-18 CLANG=clang-18
```

以下 Bash 命令从仓库根目录逐个复现。它在子 shell 中清理继承的 OpenMP、
调优与 NAS 覆盖配置，再为每个程序创建独立工作目录，避免已有 input 文件改变
参数。DC 自行生成的输入和中间文件也留在该临时目录中。

```bash
(
  example_repo="$PWD"
  example_work="$(mktemp -d)"
  for example_variable in $(compgen -e); do
    case "$example_variable" in
      OMP_*|KMP_*|GOMP_*|OTTER_*|NPB_*|TUNER|OFFLINE_CONFIG|REGION_TIME_REPORT)
        unset "$example_variable" ;;
    esac
  done
  export OMP_NUM_THREADS=4 OMP_DYNAMIC=false OMP_PROC_BIND=false
  export TUNER=none REGION_TIME_REPORT=1
  for benchmark in BT CG DC EP FT IS LU MG SP UA; do
    mkdir "$example_work/$benchmark"
    (cd "$example_work/$benchmark" && \
      "$example_repo/NPB3.3-OMP-C/bin/$benchmark.S")
  done
)
```

每份日志头部保留了本次采集的实际命令、工作目录、UTC 时间和工具链版本。
临时工作目录可能已清理；复现时创建新目录即可。时间会随机器和运行状态变化。
