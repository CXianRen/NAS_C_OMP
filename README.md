# Otter tuner

```cpp
T_max = number_of_available_cores;
T_current = T_max;
placement = CONTIGUOUS;
state = WARMUP_FIRST;

// --------------------------------------------------
// Phase 0: identify iterative part + initial warm-up
// --------------------------------------------------

for (it = 0; it < iterations; it++) {

    set_num_threads(T_current);
    set_thread_placement(placement);

    start_time();

    for (region : iterative_regions) {
        execute(region);
    }

    stop_time();

    metric = execution_time;

    // ==================================================
    // Phase 0: detect repeated regions / warm up
    // ==================================================

    if (state == WARMUP_FIRST) {
        T_current = T_max;

        // first probing iteration
        // identify parallel regions belonging to
        // the repeated iterative section
        state = WARMUP_SECOND;
        continue;
    }

    if (state == WARMUP_SECOND) {
        T_current = T_max;

        // second probing / warm-up iteration
        // full-thread configuration is available
        // for subsequent tuning
        state = SAMPLE_FULL;
        continue;
    }


    // ==================================================
    // Phase 1: Thread Scalability Optimization
    //
    // Sample:
    //       T_max
    //       T_max / 2
    //       3*T_max / 4
    //
    // placement = CONTIGUOUS during this phase
    // ==================================================

    if (state == SAMPLE_FULL) {

        sample[T_max] = metric;

        T_current = T_max / 2;
        state = SAMPLE_HALF;
        continue;
    }

    if (state == SAMPLE_HALF) {

        sample[T_max / 2] = metric;

        T_current = 3 * T_max / 4;
        state = SAMPLE_3QUARTER;
        continue;
    }

    if (state == SAMPLE_3QUARTER) {

        sample[3 * T_max / 4] = metric;

        // classify performance curve
        if (performance_saturates(sample)) {

            // situation Fig.7(a)
            model = NewtonInterpolation(sample);

            T_best =
                minimum_thread_number_within_threshold(
                    model,
                    threshold = 10%);

            state = THREAD_TUNING_DONE;
        }
        else {

            // situation Fig.7(b)/(c)
            initialize_golden_section_interval(sample);

            T_current =
                next_golden_section_point();

            state = GOLDEN_SEARCH;
        }

        continue;
    }


    // ==================================================
    // Golden-section search
    // ==================================================

    if (state == GOLDEN_SEARCH) {

        golden_samples[T_current] = metric;

        update_search_interval();

        if (search_interval <= distance_threshold) {

            T_best =
                thread_number_with_minimum_metric();

            state = THREAD_TUNING_DONE;
        }
        else {
            T_current =
                next_golden_section_point();
        }

        continue;
    }


    // ==================================================
    // Phase 2: Thread Placement Optimization
    //
    // T is now FIXED at T_best
    // ==================================================

    if (state == THREAD_TUNING_DONE) {

        T_current = T_best;
        placement = CONTIGUOUS;

        state = CONTIGUOUS_WARMUP;

        continue;
    }

    if (state == CONTIGUOUS_WARMUP) {

        // discard this measurement
        state = CONTIGUOUS_MEASURE;
        continue;
    }

    if (state == CONTIGUOUS_MEASURE) {

        metric_contiguous = metric;

        migrate_threads(CONTIGUOUS, SCATTER);

        placement = SCATTER;
        state = SCATTER_WARMUP;

        continue;
    }

    if (state == SCATTER_WARMUP) {

        // discard this measurement
        state = SCATTER_MEASURE;
        continue;
    }

    if (state == SCATTER_MEASURE) {

        metric_scatter = metric;

        if (metric_contiguous < metric_scatter)
            P_best = CONTIGUOUS;
        else
            P_best = SCATTER;

        state = TUNING_DONE;
        continue;
    }


    // ==================================================
    // Phase 3: Remaining iterations
    // ==================================================

    if (state == TUNING_DONE) {

        T_current = T_best;
        placement = P_best;

        // use this configuration for all remaining
        // iterations; no further tuning
    }
}
```

## Implementation

The process above is implemented once in
`NPB3.3-OMP-C/common/otter_tuner.c`. Each supported benchmark keeps its
original numerical kernel and only places `begin/end` hooks around one real,
timed outer iteration:

| Benchmark | Tuned outer iteration |
| --- | --- |
| SP, BT | one `adi()` time step |
| CG | one inverse-power iteration, including `conj_grad` and normalization |
| LU | one SSOR pseudo-time step, including the residual and norm work |
| MG | one V-cycle plus the following residual calculation |
| FT | one evolve, inverse FFT, and checksum iteration |

`THREAD_TUNING_DONE` is an explicit but instantaneous transition in the C
state machine. It prepares the first contiguous warm-up immediately instead
of spending an iteration on the previous configuration. `TUNING_DONE`
similarly applies `T_best/P_best` before the next iteration.

The pseudocode leaves several policy details open. This implementation uses
the following concrete definitions:

- `T_max` is the number of physical cores in the process's original Linux CPU
  affinity mask, capped by the initial OpenMP maximum.
- The metric is elapsed wall-clock execution time measured with
  `CLOCK_MONOTONIC`; lower is better. PMU counters are not used.
- Saturation means that the full and three-quarter thread metrics differ by at
  most 10%. Newton interpolation is evaluated between the half-thread and
  full-thread samples, and selects the smallest thread count within 10% of the
  predicted minimum.
- Otherwise, an integer golden-section search covers `[1, T_max]`. Its default
  stopping distance is `ceil(T_max / 8)`; the best measured point wins.
- `CONTIGUOUS` packs workers onto the first allowed cores. `SCATTER` spreads
  them across the hwloc core list within the process's original CPU mask. The
  change is implemented with Linux per-worker affinity because OpenMP has no
  runtime placement setter. If placement cannot be controlled, Phase 2 is
  skipped and reported as `P_best=N/A` rather than comparing two uncontrolled
  runs.

The first two states consume two existing benchmark iterations; they do not
add numerical iterations. Consequently, short standard runs such as some MG
and FT classes can end before all states are visited. Even the shortest path
needs nine iterations to finish placement and a tenth to run with the final
configuration; golden search can need more. Otter reports the exact unfinished
state rather than silently adding work (which would invalidate NPB
verification).

## Build and run

Build the six integrated benchmarks, for example:

```sh
make -j BENCHMARKS="SP CG LU BT MG FT" CLASS=S
```

Run with `OMP_PROC_BIND=false` so an OpenMP runtime does not replace Otter's
placement between parallel regions:

```sh
OMP_PROC_BIND=false OTTER_MAX_THREADS=8 ./bin/SP.S
```

Otter is enabled by default. Useful controls are:

| Variable | Default | Meaning |
| --- | --- | --- |
| `OTTER_ENABLED` | `1` | Set to `0` to keep the original numerical/configuration behavior; the two no-op hook calls remain |
| `OTTER_MAX_THREADS` | available physical cores | Cap `T_max` |
| `OTTER_THRESHOLD_PERCENT` | `10` | Saturation and Newton near-best threshold |
| `OTTER_GOLDEN_DISTANCE` | `ceil(T_max/8)` | Integer golden-search stop distance |
| `OTTER_VERBOSE` | `1` | `0`: quiet, `1`: samples, `2`: samples plus transitions/topology |
| `OTTER_PIN_THREADS` | `1` | Set to `0` to skip placement tuning (`P_best=N/A`) |
| `OTTER_PHYSICAL_CORES` | `1` | Set to `0` to treat allowed logical CPUs as candidates |

The tuner restores the initial OpenMP thread setting and affinity before NPB
verification, postprocessing, and result printing. Where that work belongs to
the original NPB total (MG and FT), the total timer is paused only for the
restore and then resumed, preserving the benchmark's timed components.
Therefore, the standard NPB `Total threads`/`Avail threads` fields describe
the restored launch setting, while the `[otter]` summary records the selected
mixed-configuration result. NPB's derived `Mop/s/thread` also uses the restored
thread count; it is not an Otter `T_best` metric.

Keep the class's standard iteration count when checking official NPB reference
values; `NPB_NITER` remains useful for diagnostics but a changed numerical
iteration count may not have a matching reference value.

## Switching correctness and overhead UT

`tests/otter_tuner_ut.c` drives the real tuner through a deterministic short
state-machine run. For every active iteration it starts a separate OpenMP
region and checks the actual team size and every worker's Linux affinity against
Otter's requested CPU. It also requires the thread-count transitions, the
`CONTIGUOUS -> SCATTER` transition, final `T_best=floor(T_max/2)`, and final
`P_best=SCATTER` to occur.

Build and run it with the LLVM OpenMP runtime on the 64-core machine:

```sh
make -B otter-ut CC=clang-18

env -u OMP_PLACES -u KMP_AFFINITY -u GOMP_CPU_AFFINITY \
  OMP_PROC_BIND=false OMP_DYNAMIC=false OMP_NUM_THREADS=64 \
  OTTER_MAX_THREADS=64 OTTER_PHYSICAL_CORES=1 \
  ./bin/otter_tuner_ut --rounds 30 --require-libomp
```

The test prints the compiler, detected loaded OpenMP runtime, correctness
result, and the state/configuration trace for the first round. Later rounds stay
quiet and contribute only to the statistics. The overhead table reports
min/p50/p95/mean/max in microseconds (`*_us`) for initialization, unchanged
`begin`, team growth, team shrink, placement switching, the final apply,
post-tuning no-op calls, and affinity restoration. These timings measure tuner
configuration overhead only; the synthetic delays that make state selection
deterministic are outside the reported `begin` timings.
