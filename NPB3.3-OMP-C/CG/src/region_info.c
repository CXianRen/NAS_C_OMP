#include "region_info.h"

const npb_region_info npb_regions[] = {
  [R_MAIN_PARALLEL_FOR_1] = {"main:343", -1, 1},
  [R_MAIN_PARALLEL_FOR_2] = {"main:362", -1, 1},
  [R_CONJ_GRAD_PARALLEL_1] = {"conj_grad:443", -1, 0},
  [R_CONJ_GRAD_FOR_1] = {"conj_grad:450", R_CONJ_GRAD_PARALLEL_1, 0},
  [R_CONJ_GRAD_FOR_2] = {"conj_grad:464", R_CONJ_GRAD_PARALLEL_1, 0},
  [R_CONJ_GRAD_FOR_3] = {"conj_grad:500", R_CONJ_GRAD_PARALLEL_1, 0},
  [R_CONJ_GRAD_FOR_4] = {"conj_grad:552", R_CONJ_GRAD_PARALLEL_1, 0},
  [R_CONJ_GRAD_FOR_5] = {"conj_grad:568", R_CONJ_GRAD_PARALLEL_1, 0},
  [R_CONJ_GRAD_FOR_6] = {"conj_grad:590", R_CONJ_GRAD_PARALLEL_1, 0},
  [R_CONJ_GRAD_FOR_7] = {"conj_grad:603", R_CONJ_GRAD_PARALLEL_1, 0},
  [R_CONJ_GRAD_FOR_8] = {"conj_grad:617-622 (nowait)", R_CONJ_GRAD_PARALLEL_1, 0},
};

const int npb_region_count = R_COUNT;
