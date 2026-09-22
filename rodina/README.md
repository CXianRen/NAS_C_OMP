# Rodinia OpenMP benchmarks

Source: [socal-ucr/Rodinia](https://github.com/socal-ucr/Rodinia/tree/master/openmp).
Imported from commit `761c16c62c8dc20aac6814ae63f6f7095de754b4`.

- `hotspot/`: copied from upstream [`openmp/hotspot`](https://github.com/socal-ucr/Rodinia/tree/761c16c62c8dc20aac6814ae63f6f7095de754b4/openmp/hotspot).
- `streamcluster/`: copied from upstream [`openmp/streamcluster`](https://github.com/socal-ucr/Rodinia/tree/761c16c62c8dc20aac6814ae63f6f7095de754b4/openmp/streamcluster).
- `cfd/`: copied from upstream [`openmp/cfd`](https://github.com/socal-ucr/Rodinia/tree/761c16c62c8dc20aac6814ae63f6f7095de754b4/openmp/cfd), including all four CPU variants.
- `particlefilter/`: copied from upstream [`openmp/particlefilter`](https://github.com/socal-ucr/Rodinia/tree/761c16c62c8dc20aac6814ae63f6f7095de754b4/openmp/particlefilter).

The CPU sources, upstream README files, run scripts, and licenses are imported
from the `ompt_v` branch. All seven programs now use this branch's shared
`framework/` runtime and tuner policies. Upstream offload scripts are retained as
reference; the supported Make targets build the seven host CPU programs only.

## Build

```sh
make -C rodina -j4
# Individual targets:
make -C rodina/hotspot hotspot
make -C rodina/streamcluster omp
make -C rodina/cfd euler3d_cpu euler3d_cpu_double pre_euler3d_cpu pre_euler3d_cpu_double
make -C rodina/particlefilter openmp
```

Builds require Python 3, Clang with OpenMP headers for source generation, a C/C++
OpenMP toolchain, hwloc development files, and GNU Make 4.3 or newer. Defaults use
`cc`/`g++` and `clang-18`; override `CC`, `CXX`, and `CLANG` to select compatible
compiler/runtime pairs. C sources and generated tables compile as C. Shared
framework C++ objects compile as C++17 even when application `CXXFLAGS` selects
an older language standard. CFD's four programs share one set of framework
objects within their build directory.

`mk/benchmark.mk` includes `framework/benchmark/benchmark.mk`. `FRAMEWORK` can
point to a relocated copy. `CPPFLAGS`, `CFLAGS`, `CXXFLAGS`, `LDFLAGS`, `LDLIBS`,
and `INSTRUMENT_FLAGS` remain configurable. Parser flags follow the application's
language and preprocessing definitions. Generated source copies, region tables,
manifest, and dependencies are under `build/<program>/generated/`; original
sources are not overwritten. Compiler flags, source/header changes, and
instrumentation mode changes trigger rebuilding. Missing generated outputs are
restored by the next Make invocation.

## Measurement and tuning

```sh
REGION_TIME_REPORT=1 OMP_NUM_THREADS=4 TUNER=none \
  ./rodina/particlefilter/particle_filter -x 128 -y 128 -z 10 -np 10000
OMP_PROC_BIND=false OMP_NUM_THREADS=4 TUNER=otter \
  ./rodina/particlefilter/particle_filter -x 128 -y 128 -z 10 -np 10000
make -C rodina INSTRUMENT=0
make -C rodina INSTRUMENT=1
```

`REGION_TIME_REPORT=1` enables timing reports. `TUNER=none` is the default;
`dummy`, `offline`, `j2025`, `j2025_b`, and `otter` use the same policies and
settings as the existing benchmarks. Reports may remain disabled during tuning.
`INSTRUMENT=0` still generates metadata and compiles the shared runtime, while
region macros are disabled; that mode requires `TUNER=none`.

Applications manually initialize the shared control and mark measurement windows
and real steps. The generator inserts OpenMP hooks. Initialization, input reading,
and final output remain outside the measured windows.

| Program | One measured step | Static / measured regions |
| --- | --- | --- |
| Hotspot | One temperature update and buffer swap | 1 / 1 |
| Streamcluster | One `localSearch`, including final center clustering | 2 / up to 2 |
| ParticleFilter | One processed frame (`Nfr - 1` frames) | 10 / 8 |
| CFD ordinary float/double | One outer iteration, including its three RK stages | 5 / 4 |
| CFD pre float/double | One outer iteration, including its three RK stages | 6 / 5 |

The windows accumulate across steps. ParticleFilter retains upstream diagnostic
printing inside each frame, so the application total includes that printing.
Streamcluster can finish a small input without entering every conditional region.
All thread-level regions here are combined `parallel for`: parent/child report
rows overlap and must not be added together. Standalone SIMD pragmas remain
unchanged and have no separate timer.

Hotspot and Streamcluster take their initial OpenMP thread limit from their
command-line arguments. Hotspot sets that limit once in `main`, before tuner
initialization, allowing a step-level tuner to choose subsequent teams. CFD and
ParticleFilter use `OMP_NUM_THREADS`. CFD's similarly named Make variable sets
its compile-time `block_length` padding width, independently of the runtime
thread limit. The numerical code and OpenMP pragmas are otherwise preserved.

## Validation

```sh
python3 rodina/tests/test_instrumented_build.py --cc gcc --cxx g++ --clang clang-18
python3 rodina/tests/test_instrumented_build.py --cc clang-18 --cxx clang++-18 --clang clang-18
```

The test copies only `framework/` and the needed Rodinia sources to a temporary
checkout. It builds all seven programs, runs synthetic inputs at 1/4 threads,
checks region coverage, unchanged pragmas, finite numerical output, the CFD
steady state, and agreement across `INSTRUMENT=1 -> 0 -> 1`. A test-only time
wrapper makes ParticleFilter's original RNG reproducible. It also checks manifest recovery, all programs with Dummy, and representative per-region
and per-step tuners with reports disabled. It does not download datasets or
claim large-input performance validation.

## Input data and runs

```sh
bash rodina/download_data.sh
(cd rodina/cfd && OMP_NUM_THREADS=4 ./euler3d_cpu ../../data/cfd/fvcorr.domn.097K)
OMP_NUM_THREADS=4 ./rodina/particlefilter/particle_filter -x 128 -y 128 -z 10 -np 10000
```

The download script uses `curl` and a pinned revision of the
[Rodinia data mirror](https://github.com/msasongko17/rodinia_data/tree/862ada1b99b106f9cb4af4a86c13dc0a615f9a5f).
It writes CFD's 97,046-cell and 193,474-cell meshes to `data/cfd/`, and Hotspot's
1024-grid temperature/power files to `data/hotspot/`, relative to the repository.
Inputs are not tracked in Git. CFD writes `density`, `momentum`, and
`density_energy` in its working directory; all four variants retain 2,000 outer
iterations. ParticleFilter generates its own video input. Its `-x`/`-y` arguments
set image dimensions, `-z` sets frame count, and `-np` sets particle count.
