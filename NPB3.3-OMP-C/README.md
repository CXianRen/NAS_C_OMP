# NAS OpenMP benchmarks

This directory contains BT, CG, DC, EP, FT, IS, LU, MG, SP and UA. The additional
source trees and common helpers come from `ompt_v:nas`; the current SP integration
is retained. All applications share `../framework/benchmark/benchmark.mk`, the
region controller and the same runtime tuner implementations.

```sh
make -j4 CLASS=S
make -j4 BENCHMARKS='BT CG SP' CLASS=A
REGION_TIME_REPORT=1 TUNER=none OMP_NUM_THREADS=4 ./bin/BT.S
TUNER=otter OMP_NUM_THREADS=4 ./bin/CG.S
python3 tests/test_suite.py
python3 tests/test_build.py --cc clang-18
```

Run DC in a scratch working directory: it generates its own local input and
intermediate files. No downloaded dataset is needed for the Class S tests.

| Benchmark | Supported classes |
|---|---|
| BT, EP, FT, LU, SP | S, W, A, B, C, D, E |
| CG, IS, MG, UA | S, W, A, B, C, D |
| DC | S, W, A, B |

`BENCHMARKS` defaults to all ten and `CLASSES` to `S W A B`. `CLASS=X` selects one
class. Unsupported pairs fail explicitly. Each pair owns
`.build/BENCH.CLASS/npbparams.h` and its `generated/` sources, metadata and manifest.
`INSTRUMENT=0` still generates metadata, compiles the region hooks out and disables
tuners that require instrumentation. `REGION_TIME_REPORT` is read only by the
framework. Application sources retain explicit initialization, timing windows,
logical steps and final reports; OpenMP region hooks are generated during builds.

BT/SP timestep loops, CG inverse-power iterations, FT transform iterations,
LU SSOR iterations, MG multigrid iterations and UA timesteps each notify one
logical step. Warmup work stays outside the formal window. IS retains its original
per-rank timing windows, with one logical step for each formal rank iteration;
partial verification remains outside each window. EP and DC each perform one
measured parallel batch and therefore expose one logical step, not a repeated
iteration sequence. DC partitions work using the actual team selected at region
entry, so selecting fewer threads does not omit partitions.

UA builds `transfer.c`; `transfer_au.c` is the upstream alternative implementation
and is intentionally excluded. The application algorithms and source OpenMP
constructs otherwise remain those imported from the branch, except documented
framework integration and DC's actual-team partition parameters.

`tests/test_suite.py` verifies all ten Class S results with reports enabled and
disabled, exercises dummy/Otter on iterative applications, checks a two-thread
Offline configuration for DC, and builds all ten with `INSTRUMENT=0`. The existing
`tests/test_build.py` continues to cover SP source/header/flag dependency changes,
configuration validation and Offline region keys. Use `--cc gcc --clang clang-18`
for GCC binaries with Clang AST generation.
