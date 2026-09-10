#ifndef NPB_LU_REGION_INFO_H
#define NPB_LU_REGION_INFO_H

/* Fixed timer indexes for the instrumented OpenMP regions. */
enum {
  R_BLTS_FOR_1,
  R_BUTS_FOR_1,
  R_JACLD_FOR_1,
  R_JACU_FOR_1,
  R_L2NORM_PARALLEL,
  R_L2NORM_FOR_1,
  R_RHS_PARALLEL,
  R_RHS_FOR_1,
  R_RHS_FOR_2,
  R_RHS_FOR_3,
  R_RHS_FOR_4,
  R_SSOR_PARALLEL,
  R_SSOR_FOR_1,
  R_SSOR_FOR_2,
  R_COUNT
};

#endif
