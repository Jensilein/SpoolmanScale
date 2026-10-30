#pragma once

extern bool g_auto_weight;
extern bool g_auto_loc_popup;
extern bool g_auto_tare;
extern float auto_weight_last_val;
extern unsigned long auto_weight_stable_ms;
// This spool is dealt with - written, found unchanged, or held back - until
// it comes off the pad. A send from the weight popup sets it too.
extern bool auto_weight_done;

#define AUTO_WEIGHT_STABLE_MS 3000
// The strict test for a settled weight (AMS report, BamBuddy's "stable").
#define AUTO_WEIGHT_THRESH_G 0.5f
// The automatic weighing's own window: the weight has to stay within
// AUTO_WEIGHT_BAND_G of where it started. For AUTO_WEIGHT_QUIET_MS nothing
// shows, then the button counts AUTO_WEIGHT_STABLE_MS down. With 0.5 g and
// no quiet phase the countdown started the moment a spool touched the pad
// and broke off again and again while the average was still climbing; 2 g
// ignores a reading that wobbles by a gram, and still restarts for a spool
// that is really moving.
#define AUTO_WEIGHT_BAND_G    2.0f
#define AUTO_WEIGHT_QUIET_MS  1000
// Auto-tare until it is switched off in the scale menu: an empty pad that
// drifts a few grams is fixed without anyone noticing.
#define AUTO_TARE_DEFAULT     true
// Below this much change against the backend nothing is written: the value
// is the same, and a write would only cost a request.
#define AUTO_WEIGHT_MIN_CHANGE_G 1.0f
// A spool that reads this much heavier than stored is not written on its
// own. Filament only ever leaves a spool; more weight means a bag, another
// spool, or a refill, and the user is asked (Nikolai, 26.10.2026: 5 g).
#define AUTO_WEIGHT_RISE_G 5.0f
// Below this much nothing is on the pad, whatever the spool: a tag held to
// the reader alone reads a few grams of drift at most.
#define AUTO_WEIGHT_EMPTY_PAD_G 20.0f
// A spool weighed empty reads within this much under its own measured empty
// weight (scale drift, a spool that lost a few grams of label or dust).
#define AUTO_WEIGHT_UNDER_TARE_G 15.0f

// True while the pad holds too little for the spool to be on it: less than
// AUTO_WEIGHT_EMPTY_PAD_G, or, when the empty weight was measured for this
// very spool (own_tare), less than it by more than AUTO_WEIGHT_UNDER_TARE_G.
// A filament's or vendor's default empty weight is only a guess - a lighter
// cardboard spool of the same filament is normal - so only the absolute floor
// applies then. Nothing is written while this holds.
bool autoWeightPadTooLight(float grams, float spool_weight_g, bool own_tare);
