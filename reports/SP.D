@intel J2025 
    SP Benchmark Completed.
    Class           =                        D
    Size            =            408x 408x 408
    Iterations      =                      500
    Time in seconds =                  1130.72
    Total threads   =                       64
    Avail threads   =                       64
    Mop/s total     =                 26121.39
    Mop/s/thread    =                   408.15
    Operation type  =           floating point
    Verification    =               SUCCESSFUL
    Version         =                    3.3.1
    Compile date    =              15 Sep 2026

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
    iteration total: 1130.719588041 s
    step %: accumulated region time / iteration total (average time-step basis)
    parallel region add:44  24.774834871 s  step: 2.191%
        for region add:44  24.774834871 s  step: 2.191%
    parallel region ninvr:45  16.587472200 s  step: 1.467%
        for region ninvr:45  16.587472200 s  step: 1.467%
    parallel region pinvr:45  14.008659840 s  step: 1.239%
        for region pinvr:45  14.008659840 s  step: 1.239%
    parallel region compute_rhs:43  314.263809443 s  step: 27.793%
        for region compute_rhs:51 (nowait)  37.059940100 s  step: 3.278%
        for region compute_rhs:81  17.539382458 s  step: 1.551%
        for region compute_rhs:97 (nowait)  60.997971296 s  step: 5.395%
        for region compute_rhs:189  56.356971979 s  step: 4.984%
        for region compute_rhs:285  68.683926582 s  step: 6.074%
        for region compute_rhs:333 (nowait)  48.702750444 s  step: 4.307%
        for region compute_rhs:384  8.587071657 s  step: 0.759%
        for region compute_rhs:396 (nowait)  16.333904505 s  step: 1.445%
    parallel region txinvr:45  24.866129160 s  step: 2.199%
        for region txinvr:45  24.866129160 s  step: 2.199%
    parallel region tzetar:46  32.091697931 s  step: 2.838%
        for region tzetar:46  32.091697931 s  step: 2.838%
    parallel region x_solve:48  235.904056072 s  step: 20.863%
        for region x_solve:48  235.904056072 s  step: 20.863%
    parallel region y_solve:48  226.151768923 s  step: 20.001%
        for region y_solve:48  226.151768923 s  step: 20.001%
    parallel region z_solve:52  237.573958635 s  step: 21.011%
        for region z_solve:52  237.573958635 s  step: 21.011%
    J2025 final region=add:44 threads=64 mask=1111111111111111111111111111111111111111111111111111111111111111
    J2025 final region=ninvr:45 threads=64 mask=1111111111111111111111111111111111111111111111111111111111111111
    J2025 final region=pinvr:45 threads=42 mask=0000000000000000000000111111111111111111111111111111111111111111
    J2025 final region=compute_rhs:43 threads=64 mask=1111111111111111111111111111111111111111111111111111111111111111
    J2025 final region=txinvr:45 threads=48 mask=0000000000000000111111111111111111111111111111111111111111111111
    J2025 final region=tzetar:46 threads=48 mask=0000000000000000111111111111111111111111111111111111111111111111
    J2025 final region=x_solve:48 threads=64 mask=1111111111111111111111111111111111111111111111111111111111111111
    J2025 final region=y_solve:48 threads=58 mask=0111111111011111111110111111111101111111110111111111101111111111
    J2025 final region=z_solve:52 threads=64 mask=1111111111111111111111111111111111111111111111111111111111111111

@intel original 

    SP Benchmark Completed.
    Class           =                        U
    Size            =            408x 408x 408
    Iterations      =                       10
    Time in seconds =                    18.20
    Total threads   =                       64
    Avail threads   =                       64
    Mop/s total     =                 32458.54
    Mop/s/thread    =                   507.16
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
    iteration total: 18.199193954 s
    step %: accumulated region time / iteration total (average time-step basis)
    parallel region add:44  0.289857864 s  step: 1.593%
        for region add:44  0.289857864 s  step: 1.593%
    parallel region ninvr:45  0.193003893 s  step: 1.061%
        for region ninvr:45  0.193003893 s  step: 1.061%
    parallel region pinvr:45  0.193991661 s  step: 1.066%
        for region pinvr:45  0.193991661 s  step: 1.066%
    parallel region compute_rhs:43  3.522164822 s  step: 19.353%
        for region compute_rhs:50-78 (nowait)  0.395918369 s  step: 2.175%
        for region compute_rhs:78  0.170814991 s  step: 0.939%
        for region compute_rhs:92-182 (nowait)  0.720116615 s  step: 3.957%
        for region compute_rhs:182  0.644872665 s  step: 3.543%
        for region compute_rhs:276  0.760626078 s  step: 4.179%
        for region compute_rhs:322-371 (nowait)  0.633953094 s  step: 3.483%
        for region compute_rhs:371  0.004877806 s  step: 0.027%
        for region compute_rhs:381-391 (nowait)  0.190940142 s  step: 1.049%
    parallel region txinvr:45  0.310472965 s  step: 1.706%
        for region txinvr:45  0.310472965 s  step: 1.706%
    parallel region tzetar:46  0.391966581 s  step: 2.154%
        for region tzetar:46  0.391966581 s  step: 2.154%
    parallel region x_solve:48  4.338905096 s  step: 23.841%
        for region x_solve:48  4.338905096 s  step: 23.841%
    parallel region y_solve:48  4.302517414 s  step: 23.641%
        for region y_solve:48  4.302517414 s  step: 23.641%
    parallel region z_solve:52  4.656084061 s  step: 25.584%
        for region z_solve:52  4.656084061 s  step: 25.584%