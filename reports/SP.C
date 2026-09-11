@zen4

    NAS Parallel Benchmarks (NPB3.3-OMP-C) - SP Benchmark

    No input file inputsp.data. Using compiled defaults
    Size:  162x 162x 162
    Iterations:   10    dt:    0.0006700
    Number of available threads:    64

    Time step    1
    Unknown class
    RMS-norms of residual
            1 1.1442864355109E+05
            2 1.7342134042633E+04
            3 3.3685429016511E+04
            4 2.9052210194476E+04
            5 1.0724991703742E+05
    RMS-norms of solution error
            1 2.7801902230695E+02
            2 2.1770020731352E+01
            3 7.0295207801956E+01
            4 6.2025243366806E+01
            5 5.5201260230070E+02
    No reference values provided
    No verification performed


    SP Benchmark Completed.
    Class           =                        U
    Size            =            162x 162x 162
    Iterations      =                       10
    Time in seconds =                     0.28
    Total threads   =                       64
    Avail threads   =                       64
    Mop/s total     =                130825.46
    Mop/s/thread    =                  2044.15
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
    iteration total: 0.277106047 s
    step %: accumulated region time / iteration total (average time-step basis)
    parallel region add:44  0.012788057 s  step: 4.615%
        for region add:44  0.012788057 s  step: 4.615%
    parallel region ninvr:45  0.001062393 s  step: 0.383%
        for region ninvr:45  0.001062393 s  step: 0.383%
    parallel region pinvr:45  0.000972986 s  step: 0.351%
        for region pinvr:45  0.000972986 s  step: 0.351%
    parallel region compute_rhs:43  0.094892979 s  step: 34.244%
        for region compute_rhs:50-78 (nowait)  0.027083874 s  step: 9.774%
        for region compute_rhs:78  0.023372889 s  step: 8.435%
        for region compute_rhs:92-182 (nowait)  0.011426210 s  step: 4.123%
        for region compute_rhs:182  0.009551764 s  step: 3.447%
        for region compute_rhs:276  0.010266304 s  step: 3.705%
        for region compute_rhs:322-371 (nowait)  0.007738590 s  step: 2.793%
        for region compute_rhs:371  0.001644373 s  step: 0.593%
        for region compute_rhs:381-391 (nowait)  0.003769875 s  step: 1.360%
    parallel region txinvr:45  0.004655838 s  step: 1.680%
        for region txinvr:45  0.004655838 s  step: 1.680%
    parallel region tzetar:46  0.019593000 s  step: 7.071%
        for region tzetar:46  0.019593000 s  step: 7.071%
    parallel region x_solve:48  0.057442904 s  step: 20.730%
        for region x_solve:48  0.057442904 s  step: 20.730%
    parallel region y_solve:48  0.039277792 s  step: 14.174%
        for region y_solve:48  0.039277792 s  step: 14.174%
    parallel region z_solve:52  0.046367168 s  step: 16.733%
        for region z_solve:52  0.046367168 s  step: 16.733%

@intel

    NAS Parallel Benchmarks (NPB3.3-OMP-C) - SP Benchmark

    No input file inputsp.data. Using compiled defaults
    Size:  162x 162x 162
    Iterations:   10    dt:    0.0006700
    Number of available threads:    64

    Time step    1
    Unknown class
    RMS-norms of residual
            1 1.1442864355109E+05
            2 1.7342134042633E+04
            3 3.3685429016512E+04
            4 2.9052210194476E+04
            5 1.0724991703742E+05
    RMS-norms of solution error
            1 2.7801902230695E+02
            2 2.1770020731352E+01
            3 7.0295207801956E+01
            4 6.2025243366806E+01
            5 5.5201260230070E+02
    No reference values provided
    No verification performed


    SP Benchmark Completed.
    Class           =                        U
    Size            =            162x 162x 162
    Iterations      =                       10
    Time in seconds =                     0.84
    Total threads   =                       64
    Avail threads   =                       64
    Mop/s total     =                 43001.84
    Mop/s/thread    =                   671.90
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
    iteration total: 0.843045950 s
    step %: accumulated region time / iteration total (average time-step basis)
    parallel region add:44  0.017707825 s  step: 2.100%
        for region add:44  0.017707825 s  step: 2.100%
    parallel region ninvr:45  0.010226250 s  step: 1.213%
        for region ninvr:45  0.010226250 s  step: 1.213%
    parallel region pinvr:45  0.010815859 s  step: 1.283%
        for region pinvr:45  0.010815859 s  step: 1.283%
    parallel region compute_rhs:43  0.217503548 s  step: 25.800%
        for region compute_rhs:50-78 (nowait)  0.025443792 s  step: 3.018%
        for region compute_rhs:78  0.014835358 s  step: 1.760%
        for region compute_rhs:92-182 (nowait)  0.044481277 s  step: 5.276%
        for region compute_rhs:182  0.040858269 s  step: 4.847%
        for region compute_rhs:276  0.049617290 s  step: 5.885%
        for region compute_rhs:322-371 (nowait)  0.033010006 s  step: 3.916%
        for region compute_rhs:371  0.000054836 s  step: 0.007%
        for region compute_rhs:381-391 (nowait)  0.009160519 s  step: 1.087%
    parallel region txinvr:45  0.018412828 s  step: 2.184%
        for region txinvr:45  0.018412828 s  step: 2.184%
    parallel region tzetar:46  0.028135777 s  step: 3.337%
        for region tzetar:46  0.028135777 s  step: 3.337%
    parallel region x_solve:48  0.179712772 s  step: 21.317%
        for region x_solve:48  0.179712772 s  step: 21.317%
    parallel region y_solve:48  0.172830105 s  step: 20.501%
        for region y_solve:48  0.172830105 s  step: 20.501%
    parallel region z_solve:52  0.187476158 s  step: 22.238%
        for region z_solve:52  0.187476158 s  step: 22.238%