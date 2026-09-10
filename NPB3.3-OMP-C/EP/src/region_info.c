#include "region_info.h"

const npb_region_info npb_regions[] = {
  [R_MAIN_PARALLEL_1] = {"main:173", -1, 0},
  [R_MAIN_FOR_1] = {"main:180-227 (nowait)", R_MAIN_PARALLEL_1, 0},
};

const int npb_region_count = R_COUNT;
