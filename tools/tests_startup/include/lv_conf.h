#pragma once
#include "../../../firmware/main/lv_conf.h"

// An isolated LVGL arena lets the lifecycle test force a real allocation
// failure without interposing the host allocator or adding firmware hooks.
#undef LV_MEM_CUSTOM
#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE (256U * 1024U)
#define LV_USE_ASSERT_MALLOC 0
