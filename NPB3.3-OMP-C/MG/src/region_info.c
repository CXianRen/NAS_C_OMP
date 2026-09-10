#include "region_info.h"

const npb_region_info npb_regions[] = {
  [R_PSINV_PARALLEL_FOR_1] = {"psinv:480", -1, 1},
  [R_RESID_PARALLEL_FOR_1] = {"resid:545", -1, 1},
  [R_RPRJ3_PARALLEL_FOR_1] = {"rprj3:625", -1, 1},
  [R_INTERP_PARALLEL_FOR_1] = {"interp:697", -1, 1},
  [R_INTERP_PARALLEL_1] = {"interp:759", -1, 0},
  [R_INTERP_FOR_1] = {"interp:762", R_INTERP_PARALLEL_1, 0},
  [R_INTERP_FOR_2] = {"interp:793-825 (nowait)", R_INTERP_PARALLEL_1, 0},
  [R_NORM2U3_PARALLEL_1] = {"norm2u3:867", -1, 0},
  [R_NORM2U3_FOR_1] = {"norm2u3:871-881 (nowait)", R_NORM2U3_PARALLEL_1, 0},
  [R_COMM3_PARALLEL_1] = {"comm3:920", -1, 0},
  [R_COMM3_FOR_1] = {"comm3:923", R_COMM3_PARALLEL_1, 0},
  [R_COMM3_FOR_2] = {"comm3:940-947 (nowait)", R_COMM3_PARALLEL_1, 0},
  [R_ZERO3_PARALLEL_FOR_1] = {"zero3:1297", -1, 1},
};

const int npb_region_count = R_COUNT;
