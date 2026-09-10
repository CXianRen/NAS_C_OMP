#include "header.h"

/* name, physical parallel parent, combined parallel for */
const npb_region_info npb_regions[] = {
  [R_ADD_PARALLEL] = {"add:47", -1, 1},
  [R_COMPUTE_RHS_PARALLEL] = {"compute_rhs:45", -1, 0},
  [R_COMPUTE_RHS_FOR_1] = {"compute_rhs:53-76 (nowait)", R_COMPUTE_RHS_PARALLEL, 0},
  [R_COMPUTE_RHS_FOR_2] = {"compute_rhs:78", R_COMPUTE_RHS_PARALLEL, 0},
  [R_COMPUTE_RHS_FOR_3] = {"compute_rhs:96-207 (nowait)", R_COMPUTE_RHS_PARALLEL, 0},
  [R_COMPUTE_RHS_FOR_4] = {"compute_rhs:209", R_COMPUTE_RHS_PARALLEL, 0},
  [R_COMPUTE_RHS_FOR_5] = {"compute_rhs:321", R_COMPUTE_RHS_PARALLEL, 0},
  [R_COMPUTE_RHS_FOR_6] = {"compute_rhs:380-430 (nowait)", R_COMPUTE_RHS_PARALLEL, 0},
  [R_COMPUTE_RHS_FOR_7] = {"compute_rhs:432", R_COMPUTE_RHS_PARALLEL, 0},
  [R_COMPUTE_RHS_FOR_8] = {"compute_rhs:447-457 (nowait)", R_COMPUTE_RHS_PARALLEL, 0},
  [R_X_SOLVE_PARALLEL] = {"x_solve:72", -1, 1},
  [R_Y_SOLVE_PARALLEL] = {"y_solve:71", -1, 1},
  [R_Z_SOLVE_PARALLEL] = {"z_solve:71", -1, 1},
};

const int npb_region_count = R_COUNT;
