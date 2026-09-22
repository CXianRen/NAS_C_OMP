# Rodinia example logs

These seven logs were captured from actual executions of the current sources with
Clang/Clang++ 18.1.3. Each contains the build/run commands, UTC timestamp, working
directory, environment, complete merged stdout/stderr, and the exit code.
All runs used **4 threads**, `TUNER=none`, `REGION_TIME_REPORT=1`,
`OMP_PROC_BIND=false`, and `OMP_DYNAMIC=false`. Execution used a clean environment
containing only `PATH`, `HOME`, `LC_ALL=C`, and those five settings, removing other
tuner and affinity environment variables.

These are small functional examples, not performance measurements. CFD retains
its original 2,000 iterations. ParticleFilter retains the original `time()`-based
seed: its estimates and timings can change between runs. No test-only clock/RNG
wrapper or iteration-count source modification was used.

| Log | Lines, including metadata | Measured parallel regions |
| --- | ---: | ---: |
| [hotspot.log](hotspot.log) | 20 | 1 |
| [streamcluster.log](streamcluster.log) | 33 | 2 |
| [particle_filter.log](particle_filter.log) | 63 | 8 |
| [euler3d_cpu.log](euler3d_cpu.log) | 29 | 4 |
| [euler3d_cpu_double.log](euler3d_cpu_double.log) | 29 | 4 |
| [pre_euler3d_cpu.log](pre_euler3d_cpu.log) | 31 | 5 |
| [pre_euler3d_cpu_double.log](pre_euler3d_cpu_double.log) | 31 | 5 |

Every run exited with code 0 and produced a finite, positive measurement total.
Each measured parallel row has an identical child `for` row for the combined
`parallel for` construct; these overlapping times must not be added together.
The output files were checked before being left in the temporary run directory:

- Hotspot: 256 finite temperatures, ranging from 80.5 to 81.9062.
- Streamcluster: 4 centers, total weight 128, finite coordinates within `[0, 1]`.
- ParticleFilter: two processed frames with finite position/error estimates.
- All four CFD variants: finite fields, steady-state density 1.4, zero transverse
  momenta, and positive energy on the balanced single-cell mesh.

## Build and rerun

Run the following from the repository root. The Make variable
`OMP_NUM_THREADS=4` also selects CFD's compile-time padding width of 4; the runtime
thread setting is independently supplied to every process.

```bash
make -C rodina -j4 CC=clang-18 CXX=clang++-18 CLANG=clang-18 INSTRUMENT=1 OMP_NUM_THREADS=4

rodinia_repo=$PWD
rodinia_inputs="$rodinia_repo/doc/example/rodinia/inputs"
rodinia_work=$(mktemp -d /tmp/rodinia-example.XXXXXX)
run_rodinia_example() {
    local name=$1
    shift
    mkdir -p "$rodinia_work/$name"
    (
        cd "$rodinia_work/$name" || exit
        env -i PATH="$PATH" HOME="$HOME" LC_ALL=C \
            OMP_NUM_THREADS=4 OMP_PROC_BIND=false OMP_DYNAMIC=false \
            TUNER=none REGION_TIME_REPORT=1 "$@"
    )
}

run_rodinia_example hotspot "$rodinia_repo/rodina/hotspot/hotspot" \
    16 16 3 4 "$rodinia_inputs/hotspot_temp_16.txt" \
    "$rodinia_inputs/hotspot_power_16.txt" temperature.out \
    > "$rodinia_work/hotspot.stdout.log" 2>&1

run_rodinia_example streamcluster "$rodinia_repo/rodina/streamcluster/sc_omp" \
    2 4 3 128 64 128 unused centers.out 4 \
    > "$rodinia_work/streamcluster.stdout.log" 2>&1

run_rodinia_example particle_filter "$rodinia_repo/rodina/particlefilter/particle_filter" \
    -x 16 -y 16 -z 3 -np 100 \
    > "$rodinia_work/particle_filter.stdout.log" 2>&1

for program in euler3d_cpu euler3d_cpu_double pre_euler3d_cpu pre_euler3d_cpu_double; do
    run_rodinia_example "$program" "$rodinia_repo/rodina/cfd/$program" \
        "$rodinia_inputs/cfd_1cell.mesh" \
        > "$rodinia_work/$program.stdout.log" 2>&1
done
```

Each program has a separate directory under `/tmp`, so CFD's `density`,
`momentum`, and `density_energy` files and the other numerical output files do
not overwrite source-tree files. The rerun commands save fresh raw output in
that temporary directory and leave the recorded example logs unchanged.

## Inputs

The saved fixtures match the small cases in
`rodina/tests/test_instrumented_build.py`:

- [cfd_1cell.mesh](inputs/cfd_1cell.mesh): one unit-area cell with four far-field
  faces and balanced normals. It checks preservation of the uniform steady state.
- [hotspot_temp_16.txt](inputs/hotspot_temp_16.txt): 16 × 16 nonuniform initial
  temperatures, `80 + column/16 + row/32`, stored in row-major order.
- [hotspot_power_16.txt](inputs/hotspot_power_16.txt): constant power 500000 at each
  cell, giving an observable temperature update over three steps.

Streamcluster generates 128 three-dimensional points internally and processes
64-point chunks before the final center clustering. ParticleFilter generates its
own 16 × 16 video with 3 frames and 100 particles. Neither needs an input file.

The three saved input files can be regenerated from the repository root:

```python
from pathlib import Path

inputs = Path("doc/example/rodinia/inputs")
inputs.mkdir(parents=True, exist_ok=True)
(inputs / "cfd_1cell.mesh").write_text(
    "1\n1.0\n-1 1 0 0\n-1 -1 0 0\n-1 0 1 0\n-1 0 -1 0\n"
)
(inputs / "hotspot_temp_16.txt").write_text(
    "".join(f"{80 + (i % 16)/16 + (i // 16)/32}\n" for i in range(256))
)
(inputs / "hotspot_power_16.txt").write_text("500000\n" * 256)
```
