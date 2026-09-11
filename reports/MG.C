@zen4

    NAS Parallel Benchmarks (NPB3.3-OMP-C) - MG Benchmark

    No input file. Using compiled defaults 
    Size:  512x 512x 512  (class C)
    Iterations:                     10
    Number of available threads:    64

    iter   1
    iter   5
    iter  10

    Benchmark completed
    VERIFICATION FAILED
    L2 Norm is              8.5408152190125E-06
    The correct L2 Norm is  5.7067322857400E-07


    MG Benchmark Completed.
    Class           =                        C
    Size            =            512x 512x 512
    Iterations      =                       10
    Time in seconds =                     0.35
    Total threads   =                       64
    Avail threads   =                       64
    Mop/s total     =                225129.83
    Mop/s/thread    =                  3517.65
    Operation type  =           floating point
    Verification    =             UNSUCCESSFUL
    Version         =                    3.3.1
    Compile date    =              11 Sep 2026

    Compile options:
        CC           = clang
        CLINK        = clang
        C_LIB        = -lm
        C_INC        = (none)
        CFLAGS       = -O3 -fopenmp -mcmodel=medium -mlarge-data-t...
        CLINKFLAGS   = -fopenmp -mcmodel=medium -mlarge-data-thres...
        RAND         = randdp

    --------------------------------------
    Please send all errors/feedbacks to:
    Center for Manycore Programming
    cmp@aces.snu.ac.kr
    http://aces.snu.ac.kr
    --------------------------------------


    time report
    unit: seconds
    iteration total: 0.345783949 s
    step %: accumulated region time / iteration total (average time-step basis)
    parallel region psinv:454  0.075172424 s  step: 21.740%
        for region psinv:454  0.075172424 s  step: 21.740%
    parallel region resid:515  0.180704594 s  step: 52.259%
        for region resid:515  0.180704594 s  step: 52.259%
    parallel region rprj3:591  0.034791708 s  step: 10.062%
        for region rprj3:591  0.034791708 s  step: 10.062%
    parallel region interp:659  0.034859180 s  step: 10.081%
        for region interp:659  0.034859180 s  step: 10.081%
    parallel region comm3:866  0.016006470 s  step: 4.629%
        for region comm3:868  0.003965616 s  step: 1.147%
        for region comm3:883-890 (nowait)  0.011204004 s  step: 3.240%
    parallel region zero3:1236  0.004020214 s  step: 1.163%
        for region zero3:1236  0.004020214 s  step: 1.163%

@intel
    NAS Parallel Benchmarks (NPB3.3-OMP-C) - MG Benchmark

    No input file. Using compiled defaults 
    Size:  512x 512x 512  (class C)
    Iterations:                     10
    Number of available threads:    64

    iter   1
    iter   5
    iter  10

    Benchmark completed
    VERIFICATION FAILED
    L2 Norm is              8.5408152190125E-06
    The correct L2 Norm is  5.7067322857400E-07


    MG Benchmark Completed.
    Class           =                        C
    Size            =            512x 512x 512
    Iterations      =                       10
    Time in seconds =                     0.77
    Total threads   =                       64
    Avail threads   =                       64
    Mop/s total     =                100961.12
    Mop/s/thread    =                  1577.52
    Operation type  =           floating point
    Verification    =             UNSUCCESSFUL
    Version         =                    3.3.1
    Compile date    =              11 Sep 2026

    Compile options:
        CC           = clang
        CLINK        = clang
        C_LIB        = -lm
        C_INC        = (none)
        CFLAGS       = -O3 -fopenmp -mcmodel=medium -mlarge-data-t...
        CLINKFLAGS   = -fopenmp -mcmodel=medium -mlarge-data-thres...
        RAND         = randdp

    --------------------------------------
    Please send all errors/feedbacks to:
    Center for Manycore Programming
    cmp@aces.snu.ac.kr
    http://aces.snu.ac.kr
    --------------------------------------


    time report
    unit: seconds
    iteration total: 0.771052122 s
    step %: accumulated region time / iteration total (average time-step basis)
    parallel region psinv:454  0.192604303 s  step: 24.979%
        for region psinv:454  0.192604303 s  step: 24.979%
    parallel region resid:515  0.396574259 s  step: 51.433%
        for region resid:515  0.396574259 s  step: 51.433%
    parallel region rprj3:591  0.071062803 s  step: 9.216%
        for region rprj3:591  0.071062803 s  step: 9.216%
    parallel region interp:659  0.091853380 s  step: 11.913%
        for region interp:659  0.091853380 s  step: 11.913%
    parallel region comm3:866  0.009231091 s  step: 1.197%
        for region comm3:868  0.005213976 s  step: 0.676%
        for region comm3:883-890 (nowait)  0.003524780 s  step: 0.457%
    parallel region zero3:1236  0.008943081 s  step: 1.160%
        for region zero3:1236  0.008943081 s  step: 1.160%