#include "applu.incl"

/* name, physical parallel parent, combined parallel for */
const npb_region_info npb_regions[] = {
  [R_BLTS_FOR_1] = {"blts:70-207 (nowait)", R_SSOR_PARALLEL, 0},
  [R_BUTS_FOR_1] = {"buts:68-207 (nowait)", R_SSOR_PARALLEL, 0},
  [R_JACLD_FOR_1] = {"jacld:56-339 (nowait)", R_SSOR_PARALLEL, 0},
  [R_JACU_FOR_1] = {"jacu:56-359 (nowait)", R_SSOR_PARALLEL, 0},
  [R_L2NORM_PARALLEL] = {"l2norm:60", -1, 0},
  [R_L2NORM_FOR_1] = {"l2norm:66-76 (nowait)", R_L2NORM_PARALLEL, 0},
  [R_RHS_PARALLEL] = {"rhs:59", -1, 0},
  [R_RHS_FOR_1] = {"rhs:65", R_RHS_PARALLEL, 0},
  [R_RHS_FOR_2] = {"rhs:89-214 (nowait)", R_RHS_PARALLEL, 0},
  [R_RHS_FOR_3] = {"rhs:216", R_RHS_PARALLEL, 0},
  [R_RHS_FOR_4] = {"rhs:351-473 (nowait)", R_RHS_PARALLEL, 0},
  [R_SSOR_PARALLEL] = {"ssor:137", -1, 0},
  [R_SSOR_FOR_1] = {"ssor:144-163 (nowait)", R_SSOR_PARALLEL, 0},
  [R_SSOR_FOR_2] = {"ssor:229-239 (nowait)", R_SSOR_PARALLEL, 0},
};

const int npb_region_count = R_COUNT;
