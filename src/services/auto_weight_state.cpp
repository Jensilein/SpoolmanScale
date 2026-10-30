#include "auto_weight_state.h"

bool g_auto_weight = false;
bool g_auto_loc_popup = false;
bool g_auto_tare = AUTO_TARE_DEFAULT;
float auto_weight_last_val = -9999.0f;
unsigned long auto_weight_stable_ms = 0;
bool auto_weight_done = false;

bool autoWeightPadTooLight(float grams, float spool_weight_g, bool own_tare) {
  float floor_g = AUTO_WEIGHT_EMPTY_PAD_G;
  if (own_tare && spool_weight_g - AUTO_WEIGHT_UNDER_TARE_G > floor_g)
    floor_g = spool_weight_g - AUTO_WEIGHT_UNDER_TARE_G;
  return grams < floor_g;
}
