# NAS Parallel Benchmarks

This repository contains the OpenMP C implementation of the [NAS Parallel
Benchmarks][1]. It is based on the [compor/SNU_NPB repository][2], with the
serial implementation removed.

The benchmark sources remain organized under `NPB3.3-OMP-C/<BENCHMARK>/src`.
A standalone GNU Makefile is provided inside `NPB3.3-OMP-C`, so an external
build harness is no longer required.

## Build

Build every benchmark for Classes S, W, A, and B:

```sh
make -C NPB3.3-OMP-C -j
```

Select benchmarks and classes explicitly:

```sh
make -C NPB3.3-OMP-C -j BENCHMARKS="CG MG" CLASSES="S A C D"
make -C NPB3.3-OMP-C BENCHMARKS=CG CLASS=D
```

Binaries are written to `NPB3.3-OMP-C/bin/` as `<BENCHMARK>.<CLASS>`, for
example `NPB3.3-OMP-C/bin/CG.S`. Unsupported benchmark/class combinations are
rejected. Remove all generated files with `./clean.sh`.

## Iteration region timing

```sh
NPB_TIME_REPORT=1 OMP_NUM_THREADS=4 ./NPB3.3-OMP-C/bin/CG.S
```

Unset `NPB_TIME_REPORT` or set it to `0` to disable the report; `1`, `true`,
`yes`, and `on` enable it. `timer.flag` is ignored. Only formal iterations
are measured, excluding initialization, warmup and verification.

```text
time report
unit: seconds
iteration total: 0.200000000 s
step %: accumulated region time / iteration total (average time-step basis)
parallel region main:343  0.010000000 s  step: 5.000%
    for region main:343  0.010000000 s  step: 5.000%
parallel region conj_grad:443  0.123456789 s  step: 61.728%
    for region conj_grad:450  0.001234567 s  step: 0.617%
    for region conj_grad:617-622 (nowait)  0.000123456 s  step: 0.062%
```

Only `parallel region` and `for region` rows are reported. The header's
`iteration total` remains the denominator for every `step` percentage.
An unchanged `omp parallel for` is one measured region, displayed as a parallel
row and a child for row with exactly the same time and percentage.

Every percentage is accumulated region time divided by the accumulated formal
iteration window, multiplied by 100. This equals average region time per step
divided by average step duration; seconds remain accumulated totals. Parent
and child times overlap and must not be added. EP measures a work batch rather
than a physical time-step loop.

Each benchmark has fixed IDs in `src/region_info.h` and a hardcoded
`npb_regions` table in `src/region_info.c`. It stores the source label, physical
parallel parent ID, and whether the site is a combined parallel-for. Shared
helpers accumulate all their calls under one fixed ID. Nowait labels use
`function:start-end (nowait)`: start is the first nowait pragma line, and end
is the existing `NPB_FOR_END()` or explicit stop-call line. Update the labels
when moving code; the tests check these ranges against the sources.

The API in `common/region_timers.{h,c}` uses fixed arrays and accumulates total
seconds. Only master reads the region timestamps; it adds no barriers,
allocations, event logs, runtime parent tracking, or call counters. Original
NAS timers remain independent and do not create region-report rows.

```c
npb_time_begin();                  /* after warmup, before iterations */
/* ... formal iterations ... */
npb_time_end();                    /* before verification */
npb_time_report();                 /* after the original NAS output */
```

Wrap a parallel with `NPB_PARALLEL_BEGIN(id)` / `NPB_PARALLEL_END()`.
Wrap an unchanged `omp parallel for` with `NPB_PARALLEL_FOR_BEGIN(id)` /
`NPB_PARALLEL_FOR_END()`; its parallel and for rows share the same total.
Wrap an ordinary for with `NPB_FOR_BEGIN(id)` / `NPB_FOR_END()`.
Consecutive nowait loops use one pair, ending before the next ordinary for,
explicit barrier or parallel-body end:

```c
NPB_FOR_BEGIN(R_RHS_Z)
#pragma omp for schedule(static) nowait
for (...) { /* first loop */ }
#pragma omp for schedule(static) nowait
for (...) { /* second loop */ }
NPB_FOR_END()
```

The nowait pair ends when the master reaches END, without waiting for other
workers. An ordinary for includes its existing implicit barrier. Put BEGIN
directly before the pragma; paired macros open/close a C block, without trailing
semicolons. Parallel pairs are entered serially; for pairs also work in called
functions. DC times its master's compute phase; EP times its main work batch.

Check the API with either OpenMP runtime:

```sh
make -C NPB3.3-OMP-C time-test
make -C NPB3.3-OMP-C time-test CC=clang-18
```

The timers require OpenMP. The [Class B quick report](reports/class_b_checks/region_times.md)
contains measured totals, percentages of iteration time, and percentages of each
parent region. Regenerate its Markdown and CSV from the saved logs with:

```sh
python3 NPB3.3-OMP-C/tests/report_region_times.py reports/class_b_checks
python3 NPB3.3-OMP-C/tests/check_region_reports.py reports/class_b_checks
```

[1]: www.nas.nasa.gov/publications/npb.html

[2]: https://github.com/compor/SNU_NPB/tree/master
