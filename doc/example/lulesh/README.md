# LULESH example

`lulesh.log` contains provenance comments, the complete, unedited stdout and
stderr, and the exit status from one successful run of the current
instrumented code: non-MPI LULESH, an `8 × 8 × 8` mesh,
16 timesteps, four OpenMP threads, no tuner, and region timing enabled.

Run these commands from the benchmark repository root:

```sh
benchmark_root=$PWD
make -C lulesh -j4 CC=clang-18 CXX=clang++-18 CLANG=clang-18 INSTRUMENT=1
(
  cd /tmp
  env -u OMP_PLACES -u KMP_AFFINITY -u GOMP_CPU_AFFINITY \
    -u OMP_THREAD_LIMIT -u OFFLINE_CONFIG \
    -u OTTER_PHYSICAL_CORES -u OTTER_PIN_THREADS -u OTTER_MAX_THREADS \
    -u OTTER_THRESHOLD_PERCENT -u OTTER_GOLDEN_DISTANCE -u OTTER_VERBOSE \
    OMP_NUM_THREADS=4 OMP_PROC_BIND=false OMP_DYNAMIC=false \
    TUNER=none REGION_TIME_REPORT=1 \
    "$benchmark_root/lulesh/build/lulesh2.0" -s 8 -i 16 2>&1
)
```

The recorded run exited with status 0, completed 16 iterations, and reported
final origin energy `9.693707e+04`, followed by the generated region report.
It uses the normal executable without test wrappers.

This small input demonstrates the output format and instrumentation. Its
elapsed times are from one local run and are not a performance conclusion.
