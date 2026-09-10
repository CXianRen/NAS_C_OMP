#include "region_info.h"

const npb_region_info npb_regions[] = {
  [R_RANK_PARALLEL_1] = {"rank:630", -1, 0},
  [R_RANK_FOR_1] = {"rank:654", R_RANK_PARALLEL_1, 0},
  [R_RANK_FOR_2] = {"rank:676", R_RANK_PARALLEL_1, 0},
  [R_RANK_FOR_3] = {"rank:699", R_RANK_PARALLEL_1, 0},
  [R_RANK_FOR_4] = {"rank:702", R_RANK_PARALLEL_1, 0},
  [R_RANK_FOR_5] = {"rank:750-761 (nowait)", R_RANK_PARALLEL_1, 0},
  [R_RANK_ACCUMULATE] = {"rank:767-771 (nowait)", R_RANK_PARALLEL_1, 0},
};

const int npb_region_count = R_COUNT;
