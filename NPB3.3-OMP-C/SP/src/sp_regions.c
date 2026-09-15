#include "sp_regions.h"

/* One hook context shared by all SP regions. */
region_control sp_control;

/* Only structure is manual: names and source positions come from start hooks. */
region_info sp_regions[SP_REGION_COUNT] = {
  [SP_P_ADD] = {.parent = -1, .combined = 1},
  [SP_P_ERROR_NORM] = {.parent = -1},
  [SP_F_ERROR_NORM_1] = {.parent = SP_P_ERROR_NORM, .nowait = 1},
  [SP_P_RHS_NORM] = {.parent = -1},
  [SP_F_RHS_NORM_1] = {.parent = SP_P_RHS_NORM, .nowait = 1},
  [SP_P_EXACT_RHS] = {.parent = -1},
  [SP_F_EXACT_RHS_1] = {.parent = SP_P_EXACT_RHS},
  [SP_F_EXACT_RHS_2] = {.parent = SP_P_EXACT_RHS, .nowait = 1},
  [SP_F_EXACT_RHS_3] = {.parent = SP_P_EXACT_RHS},
  [SP_F_EXACT_RHS_4] = {.parent = SP_P_EXACT_RHS},
  [SP_F_EXACT_RHS_5] = {.parent = SP_P_EXACT_RHS, .nowait = 1},
  [SP_P_INITIALIZE] = {.parent = -1},
  [SP_F_INITIALIZE_1] = {.parent = SP_P_INITIALIZE},
  [SP_F_INITIALIZE_2] = {.parent = SP_P_INITIALIZE, .nowait = 1},
  [SP_F_INITIALIZE_3] = {.parent = SP_P_INITIALIZE},
  [SP_F_INITIALIZE_4] = {.parent = SP_P_INITIALIZE, .nowait = 1},
  [SP_P_NINVR] = {.parent = -1, .combined = 1},
  [SP_P_PINVR] = {.parent = -1, .combined = 1},
  [SP_P_COMPUTE_RHS] = {.parent = -1},
  [SP_F_COMPUTE_RHS_1] = {.parent = SP_P_COMPUTE_RHS, .nowait = 1},
  [SP_F_COMPUTE_RHS_2] = {.parent = SP_P_COMPUTE_RHS},
  [SP_F_COMPUTE_RHS_3] = {.parent = SP_P_COMPUTE_RHS, .nowait = 1},
  [SP_F_COMPUTE_RHS_4] = {.parent = SP_P_COMPUTE_RHS},
  [SP_F_COMPUTE_RHS_5] = {.parent = SP_P_COMPUTE_RHS},
  [SP_F_COMPUTE_RHS_6] = {.parent = SP_P_COMPUTE_RHS, .nowait = 1},
  [SP_F_COMPUTE_RHS_7] = {.parent = SP_P_COMPUTE_RHS},
  [SP_F_COMPUTE_RHS_8] = {.parent = SP_P_COMPUTE_RHS, .nowait = 1},
  [SP_P_TXINVR] = {.parent = -1, .combined = 1},
  [SP_P_TZETAR] = {.parent = -1, .combined = 1},
  [SP_P_X_SOLVE] = {.parent = -1, .combined = 1},
  [SP_P_Y_SOLVE] = {.parent = -1, .combined = 1},
  [SP_P_Z_SOLVE] = {.parent = -1, .combined = 1},
};
