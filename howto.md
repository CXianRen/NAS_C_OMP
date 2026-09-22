# Build and run

```sh
make -C NPB3.3-OMP-C CLASS=S CC=clang-18
REGION_TIME_REPORT=1 TUNER=none OMP_NUM_THREADS=4 ./NPB3.3-OMP-C/bin/SP.S
```

OpenMP region hooks and metadata are generated in the build directory. Keep
application iteration/step boundaries explicit in the original source.

The framework reads `REGION_TIME_REPORT` at initialization; benchmarks do not
manage the report switch. Set it to `1`, `true`, `yes`, or `on` to enable
reporting for SP or the example. Unset it or use `0` to disable the timing
report while retaining tuning.
`OTTER_VERBOSE=0` separately disables Otter's search log.

Build with `INSTRUMENT=0` to disable region hooks; this mode requires
`TUNER=none`. Offline configuration keys use the original pragma's
`function:line` from the generated `instrumentation.json`.
