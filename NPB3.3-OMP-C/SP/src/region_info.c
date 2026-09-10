#include "header.h"

/* name, physical parallel parent, combined parallel for */
const npb_region_info npb_regions[] = {
  [R_ADD_PARALLEL] = {"add:46", -1, 1},
  [R_NINVR_PARALLEL] = {"ninvr:47", -1, 1},
  [R_PINVR_PARALLEL] = {"pinvr:47", -1, 1},
  [R_COMPUTE_RHS_PARALLEL] = {"compute_rhs:45", -1, 0},
  [R_COMPUTE_RHS_FOR_1] = {"compute_rhs:53-81 (nowait)", R_COMPUTE_RHS_PARALLEL, 0},
  [R_COMPUTE_RHS_FOR_2] = {"compute_rhs:83", R_COMPUTE_RHS_PARALLEL, 0},
  [R_COMPUTE_RHS_FOR_3] = {"compute_rhs:101-195 (nowait)", R_COMPUTE_RHS_PARALLEL, 0},
  [R_COMPUTE_RHS_FOR_4] = {"compute_rhs:197", R_COMPUTE_RHS_PARALLEL, 0},
  [R_COMPUTE_RHS_FOR_5] = {"compute_rhs:297", R_COMPUTE_RHS_PARALLEL, 0},
  [R_COMPUTE_RHS_FOR_6] = {"compute_rhs:345-394 (nowait)", R_COMPUTE_RHS_PARALLEL, 0},
  [R_COMPUTE_RHS_FOR_7] = {"compute_rhs:396", R_COMPUTE_RHS_PARALLEL, 0},
  [R_COMPUTE_RHS_FOR_8] = {"compute_rhs:410-420 (nowait)", R_COMPUTE_RHS_PARALLEL, 0},
  [R_TXINVR_PARALLEL] = {"txinvr:47", -1, 1},
  [R_TZETAR_PARALLEL] = {"tzetar:48", -1, 1},
  [R_X_SOLVE_PARALLEL] = {"x_solve:50", -1, 1},
  [R_Y_SOLVE_PARALLEL] = {"y_solve:50", -1, 1},
  [R_Z_SOLVE_PARALLEL] = {"z_solve:54", -1, 1},
};

const int npb_region_count = R_COUNT;
