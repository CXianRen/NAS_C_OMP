# NAS Parallel Benchmarks

This repository contains the OpenMP C implementation of the [NAS Parallel
Benchmarks][1]. It is based on the [compor/SNU_NPB repository][2], with the
serial implementation removed.

The benchmark sources remain organized under `NPB3.3-OMP-C/<BENCHMARK>/src`.
A standalone GNU Makefile is provided inside `NPB3.3-OMP-C`, so an external
build harness is no longer required. Builds require Python 3 and Clang 18
(`CLANG=...` selects another parser), plus the selected C/OpenMP compiler.

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

Python generates fixed IDs and the `npb_regions` table in
`.build/<BENCHMARK>.<CLASS>/instrumented/npb_generated_regions.{h,c}`.
The original `src` files contain no region BEGIN/END macros, explicit region
start/stop calls, or hand-maintained region tables. Only application-specific
iteration-window markers and total/report calls remain in the source.

The generated table records source labels, physical parallel parent IDs and
combined parallel-for sites. Shared helpers accumulate their calls under one
fixed ID. Nowait labels use `function:start-end (nowait)` in the original source:
start is the first nowait pragma; end marks the lexical group boundary in the
original source. The timing may continue beyond that boundary to an existing
synchronization point, including through a helper return. IDs and line labels
are regenerated when source or build configuration changes. The generated
manifest records this as `timing_end: next_for_or_barrier`.

The API in `common/region_timers.{h,c}` uses fixed arrays and accumulates total
seconds. Only master reads the region timestamps; it adds no barriers,
allocations, event logs, runtime parent tracking, or call counters. The native
NAS timer library, numbered timers, switches, and timer-only OpenMP regions
have been removed. Standard benchmark time and throughput use
`npb_time_total()`, the same formal-iteration total used by this report.
With region reporting disabled, only the two total timestamps per iteration
window remain. These timings exclude setup and verification and therefore
do not retain every original NAS timing boundary (notably FT and DC).

```c
npb_time_begin();                  /* after warmup, before iterations */
/* ... formal iterations ... */
npb_time_end();                    /* before verification */
npb_time_report();                 /* after the original NAS output */
```

Python inserts pairs around parallel and ordinary for constructs. A combined
parallel-for receives one pair and two equal report rows. Consecutive sibling
nowait loops share a region. Its timer stays pending until the next measured
for begins, an existing explicit barrier completes, or the owning parallel
region exits. A trailing nowait group therefore includes the parallel region
barrier/join; its end timestamp is shared with the parallel total. An ordinary
for still includes its own implicit barrier. A following group starts a new
interval, so deferred groups do not overlap each other. No barrier is added,
and only the primary thread reads the clock. DC measures the entire OpenMP
parallel region. EP measures its main work batch.

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
```

Saved logs describe the source revision at collection time; their line labels
are historical. Check new logs against their generated manifests with:

```sh
python3 NPB3.3-OMP-C/tests/check_region_reports.py LOG_DIRECTORY \
  --build-dir NPB3.3-OMP-C/.build --class S
```

## Automatic Python instrumentation

Normal `make` runs the full pipeline automatically:

```text
src/*.c → Python + Clang AST → .build/CG.S/instrumented/*.c → C compiler → bin/CG.S
```

```sh
make -C NPB3.3-OMP-C BENCHMARKS=CG CLASS=S
NPB_TIME_REPORT=1 OMP_NUM_THREADS=4 ./NPB3.3-OMP-C/bin/CG.S
make -C NPB3.3-OMP-C instrument-test
python3 NPB3.3-OMP-C/tests/test_automatic_build.py
```

`tools/instrument_regions.py` parses the active preprocessor branches using
the class's generated parameters and build definitions. `CPPFLAGS` is passed
to both parsing and compilation; `-D`, `-U`, `-I` and `-std=` options in
`NPB_CFLAGS` also reach the parser. Additional parser options can be supplied
through `INSTRUMENT_FLAGS`. Existing OpenMP directives and original files are
preserved. Runtime timing hooks use the shared `common/region_timers.c`.

For each benchmark/class, the generated directory contains instrumented source
copies, `npb_generated_regions.{h,c}` and `instrumentation.json`. The manifest
records source hashes, compiler arguments, IDs, parents and original source
ranges. `#line` preserves source diagnostics and `__LINE__`. Source, header,
script and build-configuration changes trigger regeneration before compilation.
An unchanged build reuses its output. `make clean` removes generated files.

The standalone script remains available for other C programs:

```sh
python3 NPB3.3-OMP-C/tools/instrument_regions.py app.c helper.c \
  --output /tmp/instrumented-app -- -I/path/to/headers -DMY_BUILD_OPTION=1
```

Supply all source files containing parallel sites and their OpenMP helpers in
one invocation. Application-specific `npb_time_begin/end/report` markers define
which iterations are measured; the parser does not guess the application's
notion of a time step.

Literal C `parallel`, `parallel for`, and `for` constructs are supported.
Nested parallel regions, ambiguous orphaned-loop parents, macro-generated
OpenMP directives and unsupported combined constructs are rejected. Syntax
validation completes before output is published. All ten NAS benchmarks use
this same generator, including IS and DC, without special hand-inserted region
hooks. This source preprocessor is independent of OMPT.

[Automatic instrumentation validation](reports/automatic_instrumentation/README.md)
contains the Class S correctness and report checks.

[1]: www.nas.nasa.gov/publications/npb.html

[2]: https://github.com/compor/SNU_NPB/tree/master
