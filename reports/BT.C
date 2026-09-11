@zen4
    NAS Parallel Benchmarks (NPB3.3-OMP-C) - BT Benchmark

    No input file inputbt.data. Using compiled defaults
    Size:  162x 162x 162
    Iterations:   10       dt:   0.0001000
    Number of available threads:    64

    Time step    1
    Unknown class
    RMS-norms of residual
            1 1.0525534802190E+05
            2 1.0559529047583E+04
            3 2.6562941482314E+04
            4 2.2512080006834E+04
            5 1.4842728151608E+05
    RMS-norms of solution error
            1 4.3709111900060E+02
            2 3.7400915861747E+01
            3 1.1009074345520E+02
            4 9.5577949030397E+01
            5 7.8369662718822E+02
    No reference values provided
    No verification performed


    BT Benchmark Completed.
    Class           =                        U
    Size            =            162x 162x 162
    Iterations      =                       10
    Time in seconds =                     0.86
    Total threads   =                       64
    Avail threads   =                       64
    Mop/s total     =                166526.06
    Mop/s/thread    =                  2601.97
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
        RAND         = (none)

    --------------------------------------
    Please send all errors/feedbacks to:
    Center for Manycore Programming
    cmp@aces.snu.ac.kr
    http://aces.snu.ac.kr
    --------------------------------------


    time report
    unit: seconds
    iteration total: 0.860610008 s
    step %: accumulated region time / iteration total (average time-step basis)
    parallel region add:44  0.025118113 s  step: 2.919%
        for region add:44  0.025118113 s  step: 2.919%
    parallel region compute_rhs:42  0.091376066 s  step: 10.618%
        for region compute_rhs:49-72 (nowait)  0.034567118 s  step: 4.017%
        for region compute_rhs:72  0.012804508 s  step: 1.488%
        for region compute_rhs:86-191 (nowait)  0.011103392 s  step: 1.290%
        for region compute_rhs:191  0.009633541 s  step: 1.119%
        for region compute_rhs:295  0.010248661 s  step: 1.191%
        for region compute_rhs:352-402 (nowait)  0.006991863 s  step: 0.812%
        for region compute_rhs:402  0.002017021 s  step: 0.234%
        for region compute_rhs:413-423 (nowait)  0.003974199 s  step: 0.462%
    parallel region x_solve:69  0.236807585 s  step: 27.516%
        for region x_solve:69  0.236807585 s  step: 27.516%
    parallel region y_solve:68  0.236783266 s  step: 27.513%
        for region y_solve:68  0.236783266 s  step: 27.513%
    parallel region z_solve:68  0.270478010 s  step: 31.429%
        for region z_solve:68  0.270478010 s  step: 31.429%

@intel
    NAS Parallel Benchmarks (NPB3.3-OMP-C) - BT Benchmark

    No input file inputbt.data. Using compiled defaults
    Size:  162x 162x 162
    Iterations:   10       dt:   0.0001000
    Number of available threads:    64

    Time step    1
    Unknown class
    RMS-norms of residual
            1 1.0525534802190E+05
            2 1.0559529047583E+04
            3 2.6562941482314E+04
            4 2.2512080006834E+04
            5 1.4842728151608E+05
    RMS-norms of solution error
            1 4.3709111900060E+02
            2 3.7400915861747E+01
            3 1.1009074345520E+02
            4 9.5577949030397E+01
            5 7.8369662718822E+02
    No reference values provided
    No verification performed


    BT Benchmark Completed.
    Class           =                        U
    Size            =            162x 162x 162
    Iterations      =                       10
    Time in seconds =                     1.21
    Total threads   =                       64
    Avail threads   =                       64
    Mop/s total     =                118419.89
    Mop/s/thread    =                  1850.31
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
        RAND         = (none)

    --------------------------------------
    Please send all errors/feedbacks to:
    Center for Manycore Programming
    cmp@aces.snu.ac.kr
    http://aces.snu.ac.kr
    --------------------------------------


    time report
    unit: seconds
    iteration total: 1.210218906 s
    step %: accumulated region time / iteration total (average time-step basis)
    parallel region add:44  0.020847797 s  step: 1.723%
        for region add:44  0.020847797 s  step: 1.723%
    parallel region compute_rhs:42  0.216420889 s  step: 17.883%
        for region compute_rhs:49-72 (nowait)  0.023271799 s  step: 1.923%
        for region compute_rhs:72  0.012941599 s  step: 1.069%
        for region compute_rhs:86-191 (nowait)  0.044097424 s  step: 3.644%
        for region compute_rhs:191  0.043056250 s  step: 3.558%
        for region compute_rhs:295  0.049360514 s  step: 4.079%
        for region compute_rhs:352-402 (nowait)  0.034104109 s  step: 2.818%
        for region compute_rhs:402  0.000057936 s  step: 0.005%
        for region compute_rhs:413-423 (nowait)  0.009492159 s  step: 0.784%
    parallel region x_solve:69  0.308715343 s  step: 25.509%
        for region x_solve:69  0.308715343 s  step: 25.509%
    parallel region y_solve:68  0.329408884 s  step: 27.219%
        for region y_solve:68  0.329408884 s  step: 27.219%
    parallel region z_solve:68  0.334561825 s  step: 27.645%
        for region z_solve:68  0.334561825 s  step: 27.645%