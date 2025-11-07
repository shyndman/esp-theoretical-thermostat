#pragma once

#include "lvgl.h"

LV_FONT_DECLARE(Figtree_Tnum_SemiBold_120);
LV_FONT_DECLARE(Figtree_Tnum_Medium_50);
LV_FONT_DECLARE(Figtree_Medium_39);
LV_FONT_DECLARE(Figtree_Medium_34);

#define THERMOSTAT_FONT_SETPOINT_PRIMARY   (&Figtree_Tnum_SemiBold_120)
#define THERMOSTAT_FONT_SETPOINT_SECONDARY (&Figtree_Tnum_Medium_50)
#define THERMOSTAT_FONT_TOP_BAR_LARGE      (&Figtree_Medium_39)
#define THERMOSTAT_FONT_TOP_BAR_MEDIUM     (&Figtree_Medium_34)

