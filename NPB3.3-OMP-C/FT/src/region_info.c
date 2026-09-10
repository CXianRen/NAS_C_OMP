#include "region_info.h"

const npb_region_info npb_regions[] = {
  [R_EVOLVE_PARALLEL_FOR_1] = {"evolve:226", -1, 1},
  [R_CFFTS1_PARALLEL_FOR_1] = {"cffts1:443", -1, 1},
  [R_CFFTS2_PARALLEL_FOR_1] = {"cffts2:478", -1, 1},
  [R_CFFTS3_PARALLEL_FOR_1] = {"cffts3:513", -1, 1},
  [R_CHECKSUM_PARALLEL_1] = {"checksum:678", -1, 0},
  [R_CHECKSUM_FOR_1] = {"checksum:683-690 (nowait)", R_CHECKSUM_PARALLEL_1, 0},
};

const int npb_region_count = R_COUNT;
