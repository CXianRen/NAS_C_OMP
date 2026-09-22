#ifndef SP_REGIONS_H
#define SP_REGIONS_H

#include "region_auto.h"

/* OpenMP regions and their metadata are generated from the source pragmas. */
#define SP_REGION_COUNT REGION_AUTO_COUNT
#define sp_regions region_auto_info
extern region_control sp_control;

#endif
