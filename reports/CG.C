@zen4
    NAS Parallel Benchmarks (NPB3.3-OMP-C) - CG Benchmark

    Size:      150000
    Iterations:                     10
    Number of available threads:    64


    iteration           ||r||                 zeta
            1       3.53112135897357E-13   109.9994423237398
            2       8.04020705219182E-16    27.3920437146521
            3       8.53101942354427E-16    28.0339761840269
            4       8.77709679634344E-16    28.4191507551292
            5       8.92792340477675E-16    28.6471670038896
            6       8.95180687810255E-16    28.7812969418413
            7       9.00462181694430E-16    28.8600458537346
            8       9.08734063012836E-16    28.9063145572687
            9       9.08975945980195E-16    28.9335649214617
        10       9.11659705345857E-16    28.9496695448999
    Benchmark completed
    VERIFICATION FAILED
    Zeta                 2.8949669544900E+01
    The correct zeta is  2.8973605592845E+01


    CG Benchmark Completed.
    Class           =                        C
    Size            =                   150000
    Iterations      =                       10
    Time in seconds =                     0.29
    Total threads   =                       64
    Avail threads   =                       64
    Mop/s total     =                 66369.39
    Mop/s/thread    =                  1037.02
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
    iteration total: 0.287979126 s
    step %: accumulated region time / iteration total (average time-step basis)
    parallel region main:325  0.000161171 s  step: 0.056%
        for region main:325  0.000161171 s  step: 0.056%
    parallel region main:342  0.001091719 s  step: 0.379%
        for region main:342  0.001091719 s  step: 0.379%
    parallel region conj_grad:420  0.286572218 s  step: 99.511%
        for region conj_grad:426  0.001506090 s  step: 0.523%
        for region conj_grad:438  0.000108957 s  step: 0.038%
        for region conj_grad:472  0.232489824 s  step: 80.731%
        for region conj_grad:522  0.003407478 s  step: 1.183%
        for region conj_grad:536  0.011973619 s  step: 4.158%
        for region conj_grad:556  0.026509285 s  step: 9.205%
        for region conj_grad:567  0.009178162 s  step: 3.187%
        for region conj_grad:579-584 (nowait)  0.000152111 s  step: 0.053%


@intel

    NAS Parallel Benchmarks (NPB3.3-OMP-C) - CG Benchmark

    Size:      150000
    Iterations:                     10
    Number of available threads:    64


    iteration           ||r||                 zeta
            1       3.53112135897357E-13   109.9994423237398
            2       8.04020705219182E-16    27.3920437146521
            3       8.53101942354427E-16    28.0339761840269
            4       8.77709679634344E-16    28.4191507551292
            5       8.92792340477675E-16    28.6471670038896
            6       8.95180687810255E-16    28.7812969418413
            7       9.00462181694430E-16    28.8600458537346
            8       9.08734063012836E-16    28.9063145572687
            9       9.08975945980195E-16    28.9335649214617
        10       9.11659705345857E-16    28.9496695448999
    Benchmark completed
    VERIFICATION FAILED
    Zeta                 2.8949669544900E+01
    The correct zeta is  2.8973605592845E+01


    CG Benchmark Completed.
    Class           =                        C
    Size            =                   150000
    Iterations      =                       10
    Time in seconds =                     0.53
    Total threads   =                       64
    Avail threads   =                       64
    Mop/s total     =                 35989.33
    Mop/s/thread    =                   562.33
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
    iteration total: 0.531074047 s
    step %: accumulated region time / iteration total (average time-step basis)
    parallel region main:325  0.000141144 s  step: 0.027%
        for region main:325  0.000141144 s  step: 0.027%
    parallel region main:342  0.000054359 s  step: 0.010%
        for region main:342  0.000054359 s  step: 0.010%
    parallel region conj_grad:420  0.528838873 s  step: 99.579%
        for region conj_grad:426  0.000657320 s  step: 0.124%
        for region conj_grad:438  0.000083208 s  step: 0.016%
        for region conj_grad:472  0.481093645 s  step: 90.589%
        for region conj_grad:522  0.002394915 s  step: 0.451%
        for region conj_grad:536  0.007581234 s  step: 1.428%
        for region conj_grad:556  0.016815901 s  step: 3.166%
        for region conj_grad:567  0.019325972 s  step: 3.639%
        for region conj_grad:579-584 (nowait)  0.000252008 s  step: 0.047%