校验时间戳正确性
BT +
CG + 
DC
EP
FT +
IS
LU +
MG +
SP +
UA


<!-- 以下结果都约束到 10 个time step -->

@zen4
校验 插桩性能差别
        没桩 带桩不开启 带桩开启
CG.C    0.30            0.30
BT.C    0.85            0.86
SP.C    0.28            0.28
LU.C    0.51            0.51
FT.C    0.62            0.62
MG.C    0.34            0.35

@intel

        没桩 带桩不开启 带桩开启
CG.C    0.54            0.53
BT.C    1.21            1.21
SP.C    0.84            0.84
LU.C    0.47            0.46
FT.C    1.25            1.26 
MG.C    0.77            0.78


@intel otter - TN - TP
CG.C    0.58    64, scatter  (0.54)
BT.C    1.26    64, scatter  (1.258)
SP.C    0.79    45, scatter  (0.785) (contigious: 0.899)
LU.C    0.475   57, contigious (0.470)
FT.C    1.65    64, contigious (1.257)
MG.C    0.98    64, congigious (0.776)


@amd  otter
CG.C  0.306     64
BT.C  0.934     61  SCATTER
SP.C  0.284     64
LU.C  0.497     54  contigious
FT.C  0.87      64  contigious  FT, 20 iters 显然迭代次数不够
MG.C  0.515     64              MG, 20 iters

