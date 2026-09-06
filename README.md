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

The process above is implemented once in `otter/otter_tuner.c`. Each supported
benchmark keeps its original numerical kernel and only places `begin/end`
hooks around one real, timed outer iteration:

| Benchmark | Tuned outer iteration |
| --- | --- |
| SP, BT | one `adi()` time step |
| CG | one inverse-power iteration, including `conj_grad` and normalization |
| LU | one SSOR pseudo-time step, including the residual and norm work |
| MG | one V-cycle plus the following residual calculation |
| FT | one evolve, inverse FFT, and checksum iteration |
| LULESH | one physical simulation time step |

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
make -C NPB3.3-OMP-C -j BENCHMARKS="SP CG LU BT MG FT" CLASS=S
```

Run with `OMP_PROC_BIND=false` so an OpenMP runtime does not replace Otter's
placement between parallel regions:

```sh
OMP_PROC_BIND=false OTTER_MAX_THREADS=8 ./NPB3.3-OMP-C/bin/SP.S
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

## Otter correctness tests and overhead profiling

Otter has two independent correctness tests and a separate overhead profiler.
The correctness tests include the production implementation to exercise its
private configuration and search functions without adding public test APIs.

Build all three programs (or use `make -C otter correctness` for just the tests):

```sh
make -C otter -B
```

Run the binding correctness test with LLVM OpenMP on the 64-core machine:

```sh
env -u OMP_PLACES -u KMP_AFFINITY -u GOMP_CPU_AFFINITY \
  OMP_PROC_BIND=false OMP_DYNAMIC=false OMP_NUM_THREADS=64 \
  OTTER_MAX_THREADS=64 OTTER_PHYSICAL_CORES=1 \
  ./otter/build/otter_tuner_binding_correctness --require-libomp
```

The binding test directly applies a fixed sequence of configurations, without
running search or measuring workload time. It checks the next ordinary OpenMP
parallel region (without a `num_threads` clause): default team size, singleton
CPU affinity, target CPU, and distinct CPUs across workers. The sequence covers
thread growth/shrinkage in both placements, a single-thread team, both directions
of `CONTIGUOUS <-> SCATTER`, and repeated configuration application. Destruction
must restore the OpenMP settings and worker affinity. At least four usable cores
and working Linux affinity control are required.

Run the search correctness test independently:

```sh
./otter/build/otter_tuner_search_correctness
```

The search test uses a virtual thread limit and feeds exact mock metrics directly
into the production state machine. It never discovers topology, creates OpenMP
teams, binds threads, sleeps, or measures wall time. It still links the normal
OpenMP/hwloc libraries because it includes the production implementation.
Its fixed cases check Newton selection with and without tolerance, flat curves,
golden-search interior and boundary optima, odd/small thread limits, both
placement winners and ties, unavailable placement, ignored warmup metrics, and
terminal-state stability. Golden-search cases use distance 1 for exact integer
answers; they do not assert exact optima for the coarser production default.
The expected answers are constants derived from the mock curves, independent
of the machine's core count and affinity environment.

Run overhead profiling separately:

```sh
env -u OMP_PLACES -u KMP_AFFINITY -u GOMP_CPU_AFFINITY \
  OMP_PROC_BIND=false OMP_DYNAMIC=false OMP_NUM_THREADS=64 \
  OTTER_MAX_THREADS=64 OTTER_PHYSICAL_CORES=1 \
  ./otter/build/otter_tuner_overhead --rounds 30 --require-libomp
```

The profiler does not run correctness affinity probes. It reports
min/p50/p95/mean/max in microseconds (`*_us`) for initialization, unchanged
`begin`, team growth, team shrink, placement switching, the final apply,
post-tuning no-op calls, and affinity restoration. These timings measure tuner
configuration overhead only; the synthetic delays that make state selection
deterministic are outside the reported `begin` timings.
