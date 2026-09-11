@zen4


    NAS Parallel Benchmarks (NPB3.3-OMP-C) - LU Benchmark

    Size:  162x 162x 162
    Iterations:                     10
    Number of available threads:    64

    Time step    1
    Time step   10
    Unknown class
    RMS-norms of residual
            1   1.7132043970688E+05
            2   1.7353199971588E+04
            3   4.3406841113590E+04
            4   3.6803290918013E+04
            5   2.3970825035350E+05
    RMS-norms of solution error
            1   4.7156826056897E+02
            2   4.1196382569283E+01
            3   1.1890988006133E+02
            4   1.0298829295720E+02
            5   8.2757235826389E+02
    Surface integral
                4.3874238145232E+02
    No reference values provided
    No verification performed


    LU Benchmark Completed.
    Class           =                        U
    Size            =            162x 162x 162
    Iterations      =                       10
    Time in seconds =                     0.51
    Total threads   =                       64
    Avail threads   =                       64
    Mop/s total     =                160226.36
    Mop/s/thread    =                  2503.54
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
    iteration total: 0.509029150 s
    step %: accumulated region time / iteration total (average time-step basis)
    parallel region l2norm:59  0.001096010 s  step: 0.215%
        for region l2norm:64-74 (nowait)  0.001093149 s  step: 0.215%
    parallel region rhs:56  0.163037062 s  step: 32.029%
        for region rhs:61  0.048362732 s  step: 9.501%
        for region rhs:81-200 (nowait)  0.011899710 s  step: 2.338%
        for region rhs:200  0.029883862 s  step: 5.871%
        for region rhs:327-449 (nowait)  0.072845459 s  step: 14.311%
    parallel region ssor:128  0.344826221 s  step: 67.742%
        for region blts:69-208 (nowait)  0.090304852 s  step: 17.741%
        for region buts:67-208 (nowait)  0.139720440 s  step: 27.448%
        for region jacld:55-338 (nowait)  0.033929825 s  step: 6.666%
        for region jacu:55-358 (nowait)  0.033353806 s  step: 6.552%
        for region ssor:132-145 (nowait)  0.009519339 s  step: 1.870%
        for region ssor:195-205 (nowait)  0.037951469 s  step: 7.456%

@intel
    NAS Parallel Benchmarks (NPB3.3-OMP-C) - LU Benchmark

    Size:  162x 162x 162
    Iterations:                     10
    Number of available threads:    64

    Time step    1
    Time step   10
    Unknown class
    RMS-norms of residual
            1   1.7132043970688E+05
            2   1.7353199971588E+04
            3   4.3406841113590E+04
            4   3.6803290918013E+04
            5   2.3970825035350E+05
    RMS-norms of solution error
            1   4.7156826056897E+02
            2   4.1196382569283E+01
            3   1.1890988006133E+02
            4   1.0298829295720E+02
            5   8.2757235826389E+02
    Surface integral
                4.3874238145232E+02
    No reference values provided
    No verification performed


    LU Benchmark Completed.
    Class           =                        U
    Size            =            162x 162x 162
    Iterations      =                       10
    Time in seconds =                     0.46
    Total threads   =                       64
    Avail threads   =                       64
    Mop/s total     =                176186.91
    Mop/s/thread    =                  2752.92
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
    iteration total: 0.462916851 s
    step %: accumulated region time / iteration total (average time-step basis)
    parallel region l2norm:59  0.000898838 s  step: 0.194%
        for region l2norm:64-74 (nowait)  0.000895977 s  step: 0.194%
    parallel region rhs:56  0.139930964 s  step: 30.228%
        for region rhs:61  0.032205105 s  step: 6.957%
        for region rhs:81-200 (nowait)  0.024406433 s  step: 5.272%
        for region rhs:200  0.037760496 s  step: 8.157%
        for region rhs:327-449 (nowait)  0.045527935 s  step: 9.835%
    parallel region ssor:128  0.321812391 s  step: 69.518%
        for region blts:69-208 (nowait)  0.098487377 s  step: 21.275%
        for region buts:67-208 (nowait)  0.100081205 s  step: 21.620%
        for region jacld:55-338 (nowait)  0.043982506 s  step: 9.501%
        for region jacu:55-358 (nowait)  0.044478655 s  step: 9.608%
        for region ssor:132-145 (nowait)  0.010088921 s  step: 2.179%
        for region ssor:195-205 (nowait)  0.024650097 s  step: 5.325%