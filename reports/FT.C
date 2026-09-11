@zen4
    NAS Parallel Benchmarks (NPB3.3-OMP-C) - FT Benchmark

    Size                :  512x 512x 512
    Iterations                  :     10
    Number of available threads :     64

    T =    1     Checksum =    5.195078707457E+02    5.149019699238E+02
    T =    2     Checksum =    5.155422171134E+02    5.127578201997E+02
    T =    3     Checksum =    5.144678022222E+02    5.122251847514E+02
    T =    4     Checksum =    5.140150594328E+02    5.121090289018E+02
    T =    5     Checksum =    5.137550426810E+02    5.121143685824E+02
    T =    6     Checksum =    5.135811056728E+02    5.121496764568E+02
    T =    7     Checksum =    5.134569343165E+02    5.121870921893E+02
    T =    8     Checksum =    5.133651975661E+02    5.122193250322E+02
    T =    9     Checksum =    5.132955192805E+02    5.122454735794E+02
    T =   10     Checksum =    5.132410471738E+02    5.122663649603E+02
    class = U


    FT Benchmark Completed.
    Class           =                        U
    Size            =            512x 512x 512
    Iterations      =                       10
    Time in seconds =                     0.62
    Total threads   =                       64
    Avail threads   =                       64
    Mop/s total     =                334828.66
    Mop/s/thread    =                  5231.70
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
    iteration total: 0.621890068 s
    step %: accumulated region time / iteration total (average time-step basis)
    parallel region evolve:204  0.148286819 s  step: 23.845%
        for region evolve:204  0.148286819 s  step: 23.845%
    parallel region cffts1:414  0.148894787 s  step: 23.942%
        for region cffts1:414  0.148894787 s  step: 23.942%
    parallel region cffts2:445  0.145704269 s  step: 23.429%
        for region cffts2:445  0.145704269 s  step: 23.429%
    parallel region cffts3:476  0.176423073 s  step: 28.369%
        for region cffts3:476  0.176423073 s  step: 28.369%
    parallel region checksum:638  0.000683308 s  step: 0.110%
        for region checksum:642-650 (nowait)  0.000605822 s  step: 0.097%

@intel
    NAS Parallel Benchmarks (NPB3.3-OMP-C) - FT Benchmark

    Size                :  512x 512x 512
    Iterations                  :     10
    Number of available threads :     64

    T =    1     Checksum =    5.195078707457E+02    5.149019699238E+02
    T =    2     Checksum =    5.155422171134E+02    5.127578201997E+02
    T =    3     Checksum =    5.144678022222E+02    5.122251847514E+02
    T =    4     Checksum =    5.140150594328E+02    5.121090289018E+02
    T =    5     Checksum =    5.137550426810E+02    5.121143685824E+02
    T =    6     Checksum =    5.135811056728E+02    5.121496764568E+02
    T =    7     Checksum =    5.134569343165E+02    5.121870921893E+02
    T =    8     Checksum =    5.133651975661E+02    5.122193250322E+02
    T =    9     Checksum =    5.132955192805E+02    5.122454735794E+02
    T =   10     Checksum =    5.132410471738E+02    5.122663649603E+02
    class = U


    FT Benchmark Completed.
    Class           =                        U
    Size            =            512x 512x 512
    Iterations      =                       10
    Time in seconds =                     1.26
    Total threads   =                       64
    Avail threads   =                       64
    Mop/s total     =                165326.64
    Mop/s/thread    =                  2583.23
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
    iteration total: 1.259486198 s
    step %: accumulated region time / iteration total (average time-step basis)
    parallel region evolve:204  0.331581116 s  step: 26.327%
        for region evolve:204  0.331581116 s  step: 26.327%
    parallel region cffts1:414  0.258093357 s  step: 20.492%
        for region cffts1:414  0.258093357 s  step: 20.492%
    parallel region cffts2:445  0.282878876 s  step: 22.460%
        for region cffts2:445  0.282878876 s  step: 22.460%
    parallel region cffts3:476  0.384526014 s  step: 30.530%
        for region cffts3:476  0.384526014 s  step: 30.530%
    parallel region checksum:638  0.000550985 s  step: 0.044%
        for region checksum:642-650 (nowait)  0.000505447 s  step: 0.040%