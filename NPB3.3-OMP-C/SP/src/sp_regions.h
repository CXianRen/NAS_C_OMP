#ifndef SP_REGIONS_H
#define SP_REGIONS_H

#include "../../../framework/region_control/region_control.h"

/* Stable IDs; hook names and lines are resolved into sp_regions at build time. */
enum {
  SP_P_ADD,
  SP_P_ERROR_NORM,
  SP_F_ERROR_NORM_1,
  SP_P_RHS_NORM,
  SP_F_RHS_NORM_1,
  SP_P_EXACT_RHS,
  SP_F_EXACT_RHS_1,
  SP_F_EXACT_RHS_2,
  SP_F_EXACT_RHS_3,
  SP_F_EXACT_RHS_4,
  SP_F_EXACT_RHS_5,
  SP_P_INITIALIZE,
  SP_F_INITIALIZE_1,
  SP_F_INITIALIZE_2,
  SP_F_INITIALIZE_3,
  SP_F_INITIALIZE_4,
  SP_P_NINVR,
  SP_P_PINVR,
  SP_P_COMPUTE_RHS,
  SP_F_COMPUTE_RHS_1,
  SP_F_COMPUTE_RHS_2,
  SP_F_COMPUTE_RHS_3,
  SP_F_COMPUTE_RHS_4,
  SP_F_COMPUTE_RHS_5,
  SP_F_COMPUTE_RHS_6,
  SP_F_COMPUTE_RHS_7,
  SP_F_COMPUTE_RHS_8,
  SP_P_TXINVR,
  SP_P_TZETAR,
  SP_P_X_SOLVE,
  SP_P_Y_SOLVE,
  SP_P_Z_SOLVE,
  SP_REGION_COUNT
};

extern region_control sp_control;
extern region_info sp_regions[SP_REGION_COUNT];

#endif
