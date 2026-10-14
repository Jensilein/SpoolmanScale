#include "ams_pick.h"

#include <Arduino.h>
#include <string.h>

#include "app/app_state.h"
#include "hardware/sd_logger.h"
#include "services/auto_weight_state.h"
#include "services/ams_presence.h"
#include "services/backend_api.h"
#include "services/server_reach.h"
#include "services/tag_field.h"
#include "services/location_state.h"
#include "services/prefs_store.h"
#include "services/user_options.h"
#include "ui/ams_view.h"
#include "ui/info_popup.h"
#include "ui/loading_overlay.h"
#include "ui/more_info_screen.h"

#include "lang.h"

namespace {

struct Pending {
  bool          active;
  int           spool_id;
  char          name[32];
  unsigned long noted_ms;
};

Pending s_pending;

// An assignment sent from the picker, watched for a few minutes. BamBuddy
// releases a bay that the AMS reports as unreadable ("?") once that has
// lasted two minutes - which is what an assignment sent before the spool went
// in turns into (14.10.2026, Overture #10 in AMS-A bay 4). Sent again while
// the spool sits in the bay, it holds, because BamBuddy then configures the
// bay. So the order of "put it in" and "tap the bay" no longer matters.
struct Recheck {
  bool          active;
  int           spool_id;
  int           printer_id;
  int           ams_id;
  int           tray_id;
  unsigned long since_ms;
  uint8_t       round;      // index into kRecheckAtMs
  uint8_t       resent;
};

Recheck s_recheck;

// After BamBuddy's two minute hold, then twice more for a spool that went in
// late. Ends after the last one.
const unsigned long kRecheckAtMs[] = {150000UL, 300000UL, 480000UL};
const uint8_t       kRecheckRounds = sizeof(kRecheckAtMs) / sizeof(kRecheckAtMs[0]);

// Big enough to stay off the loop task's stack.
AmsSlotState s_recheck_state;

// "Loading AMS bays" from the moment the weight leaves the pad until the
// question stands. Lifting, the two removal checks and the requests to
// BamBuddy and Spoolman take about two seconds, and a blank screen in that
// time looked like nothing had happened (14.10.2026).
bool          s_hint_up = false;
unsigned long s_hint_ms = 0;
const unsigned long kHintMaxMs = 6000;

}  // namespace

static void rememberBay(int spool_id, int ams, int tray);
static bool lastBay(int spool_id, int* ams, int* tray);

bool amsPickActive() {
  return g_ams_pick_ask && backendCanAssignAmsSlot();
}

static void notePending(int spool_id, const char* spool_name) {
  if (spool_id <= 0) return;
  s_pending.active   = true;
  s_pending.spool_id = spool_id;
  s_pending.noted_ms = millis();
  s_pending.name[0]  = '\0';
  if (spool_name) {
    strncpy(s_pending.name, spool_name, sizeof(s_pending.name) - 1);
    s_pending.name[sizeof(s_pending.name) - 1] = '\0';
  }
}

void amsPickNote(int spool_id, const char* spool_name) {
  if (spool_id <= 0) return;
  // A spool with a Bambu tag is not asked about: the AMS reads the tag and
  // BamBuddy puts the spool into its bay by the tray uuid on its own. The
  // question could only add a wrong answer. g_tag is cleared for every tag
  // that is not a Bambu one, so a tray uuid here belongs to this spool.
  if (tagIsBambu(g_tag.tray_uuid)) {
    logSDf("AMSPICK: spool %d carries a Bambu tag, BamBuddy assigns it by itself - not asking",
           spool_id);
    return;
  }
  // A spool that already has a bay is not noted at all, so lifting it shows
  // no "loading" either. amsPickShow() asks once more at the lift, because
  // the bay can have been released in between.
  const int printer_id = amsPresencePrinterId();
  if (printer_id > 0) {
    int ams = -1, tray = -1;
    if (backendFindSpoolSlot(spool_id, printer_id, &ams, &tray, 4000) == 200 && ams >= 0) {
      logSDf("AMSPICK: spool %d sits in bay %d/%d, no question on the lift", spool_id, ams, tray);
      rememberBay(spool_id, ams, tray);
      return;
    }
  }
  notePending(spool_id, spool_name);
}

bool amsPickHasPending()    { return s_pending.active; }
int  amsPickPendingSpoolId(){ return s_pending.active ? s_pending.spool_id : -1; }

unsigned long amsPickPendingAgeMs() {
  return s_pending.active ? (millis() - s_pending.noted_ms) : 0;
}

void amsPickDropPending() { s_pending.active = false; }

// The location question the picker took the removal event away from. A
// spool that went into a bay needs no shelf; one whose picker was closed
// without a tap is going somewhere else, and that is what the location
// prompt is for. Same conditions and same guard as the AMS question's "no"
// branch in ams_assign_popup.cpp: the NFC poll kept running under the page,
// and a spool that landed meanwhile moved sm_id.
static void offerLocationAfterPick(int spool_id) {
  // Without a load cell nothing is asked on its own: the location has its
  // own button there, and the picker was opened by a tap, not by a removal.
  if (!g_scale_fitted) return;
  if (!g_auto_loc_popup || !wifi_ok) return;
  if (!sm_found || sm_archived || sm_id != spool_id) {
    logSDf("LOC: not offered after the picker, id=%d sm_id=%d found=%d archived=%d",
           spool_id, sm_id, (int)sm_found, (int)sm_archived);
    return;
  }
  if (g_loc_popup_shown_for_id == spool_id) return;
  g_loc_popup_shown_for_id = spool_id;
  logSDf("LOC: offered after the picker was closed, id=%d", spool_id);
  requestLocationPicker(true);
}

// Runs one pass after the picker closed, never from the tap itself.
static void onPicked(int ams_id, int tray_id) {
  // The page went away without a tap: back, a backend switch, a navigation
  // that tore it down. The bay question was seen and not answered, so it is
  // not asked again for this spool - the location question still is, because
  // a spool that is not going into the printer is going onto a shelf.
  if (ams_id < 0 || tray_id < 0) {
    const int spool_id = amsPickPendingSpoolId();
    logSD("AMSPICK: picker closed without an answer, note dropped");
    amsPickDropPending();
    offerLocationAfterPick(spool_id);
    return;
  }

  const int spool_id = amsPickPendingSpoolId();
  if (spool_id <= 0) {
    logSD("AMSPICK: answer arrived with no spool remembered");
    return;
  }
  // Another spool can have landed on the pad while the question stood. The
  // answer belongs to the one that was asked about, or to nothing.
  if (sm_found && sm_id > 0 && sm_id != spool_id) {
    logSDf("AMSPICK: answer discarded, parked spool changed from %d to %d",
           spool_id, sm_id);
    amsPickDropPending();
    return;
  }

  const int printer_id = amsViewPrinterId();
  if (printer_id <= 0) {
    logSD("AMSPICK: no printer to assign to");
    amsPickDropPending();
    return;
  }

  // Where it sits now, so the result can say "moved" rather than "assigned"
  // when it was already somewhere. A failure here is not fatal: it only
  // costs the nicer wording.
  int was_ams = -1, was_tray = -1;
  backendFindSpoolSlot(spool_id, printer_id, &was_ams, &was_tray, 6000);

  const int code = serverReachNote(backendAssignAmsSlot(spool_id, printer_id, ams_id, tray_id, 8000), true);
  amsPickDropPending();

  if (code == 200) {
    const bool moved = (was_ams >= 0 && (was_ams != ams_id || was_tray != tray_id));
    logSDf("AMSPICK: spool %d -> printer %d bay %d/%d (%s)",
           spool_id, printer_id, ams_id, tray_id, moved ? "moved" : "new");
    rememberBay(spool_id, ams_id, tray_id);
    s_recheck.active     = true;
    s_recheck.spool_id   = spool_id;
    s_recheck.printer_id = printer_id;
    s_recheck.ams_id     = ams_id;
    s_recheck.tray_id    = tray_id;
    s_recheck.since_ms   = millis();
    s_recheck.round      = 0;
    s_recheck.resent     = 0;
    showInfoPopup(STR_AMSV_TITLE, moved ? STR_AMSV_MOVED : STR_AMSV_ASSIGNED,
                  INFO_DONE);
    // A spool going into the printer does not also need the storage location
    // question on top of it.
    g_loc_popup_shown_for_id = spool_id;
  } else {
    // The code goes to the log, not into the popup: showInfoPopup takes
    // string ids so the handler can still read them after the caller's
    // buffers are gone, and a number cannot ride along in one.
    logSDf("AMSPICK: assignment of spool %d failed, HTTP %d", spool_id, code);
    showInfoPopup(STR_AMSV_TITLE, STR_AMSV_ASSIGN_FAIL, INFO_WARN);
  }
}

// The page itself, headed with the remembered spool's name.
static void openPicker() {
  char fmt[48], head[80];
  copyT(fmt, sizeof(fmt), STR_AMSV_PICK_HEAD);
  // The hint stays up over the page until its bays are drawn, see
  // amsPickTick().
  snprintf(head, sizeof(head), fmt,
           s_pending.name[0] ? s_pending.name : "");
  requestAmsView(AMS_VIEW_PICK, onPicked, head);
  int last_ams = -1, last_tray = -1;
  if (lastBay(s_pending.spool_id, &last_ams, &last_tray)) {
    logSDf("AMSPICK: spool %d sat in bay %d/%d last, marked", s_pending.spool_id, last_ams, last_tray);
    amsViewSetLastBay(last_ams, last_tray);
  }
}

// The bay a spool sat in last, kept across restarts: one small key per spool
// ("lb" + id), ams * 16 + tray + 1, 0 for none. Written whenever the scale
// learns where a spool is - an assignment from here, or one it finds on file.
static void rememberBay(int spool_id, int ams, int tray) {
  if (spool_id <= 0 || ams < 0 || tray < 0) return;
  char key[16];
  snprintf(key, sizeof(key), "lb%d", spool_id);
  const int v = ams * 16 + tray + 1;
  if (prefsGetInt(key, 0) != v) prefsPutInt(key, v);
}

static bool lastBay(int spool_id, int* ams, int* tray) {
  char key[16];
  snprintf(key, sizeof(key), "lb%d", spool_id);
  const int v = prefsGetInt(key, 0);
  if (v <= 0) return false;
  *ams  = (v - 1) / 16;
  *tray = (v - 1) % 16;
  return true;
}

void amsPickHintShow() {
  if (s_hint_up) return;
  s_hint_up = true;
  s_hint_ms = millis();
  loadingOverlayShow(T(STR_AMSPICK_LOADING));
}

void amsPickHintHide() {
  if (!s_hint_up) return;
  s_hint_up = false;
  loadingOverlayHide();
}

bool amsPickHintUp() { return s_hint_up; }

void amsPickShow() {
  if (!s_pending.active) { amsPickHintHide(); return; }
  // A note taken under one backend can be asked about under another only if
  // that one can assign bays too, and the option is still on.
  if (!amsPickActive()) {
    amsPickDropPending();
    amsPickHintHide();
    return;
  }

  // A spool that already has a bay is not asked about again: weighed and
  // put back, it goes into the slot it came from. On 14.10.2026 every lift
  // of spool 5 asked once more after it had been assigned. One that moves
  // to another slot is reassigned with the AMS chip. When the bay cannot be
  // looked up, the question is asked as before.
  const int printer_id = amsPresencePrinterId();
  if (printer_id > 0) {
    int ams = -1, tray = -1;
    const int code = backendFindSpoolSlot(s_pending.spool_id, printer_id, &ams, &tray, 6000);
    if (code == 200 && ams >= 0) {
      logSDf("AMSPICK: spool %d already sits in bay %d/%d, not asking",
             s_pending.spool_id, ams, tray);
      rememberBay(s_pending.spool_id, ams, tray);
      amsPickDropPending();
      amsPickHintHide();
      return;
    }
  }

  openPicker();
}

void amsPickOpenFor(int spool_id, const char* spool_name) {
  if (spool_id <= 0 || !backendCanAssignAmsSlot()) return;
  notePending(spool_id, spool_name);
  logSDf("AMSPICK: picker opened by the AMS button for id=%d", spool_id);
  openPicker();
}

void amsPickTick() {
  // Never left standing: a lift that turned out to be none, or a question
  // that did not come, takes it down after a few seconds.
  if (s_hint_up && (millis() - s_hint_ms > kHintMaxMs ||
                    (isAmsViewOpen() && !amsViewLoading()))) amsPickHintHide();
  if (!s_pending.active) return;
  if (isAmsViewOpen()) return;          // the question is on screen, let it stand
  if (amsPickPendingAgeMs() <= AMS_PICK_MAX_MS) return;
  logSDf("AMSPICK: note for spool %d expired", s_pending.spool_id);
  amsPickDropPending();
}

// Whether the AMS reports a spool in the bay: the printer's presence bit, as
// BamBuddy passes it on. An unreadable spool ("?") counts as present.
static bool bayHoldsSpool(int printer_id, int ams_id, int tray_id, bool* known) {
  *known = false;
  if (backendGetAmsState(printer_id, s_recheck_state, 8000) != 200 ||
      !s_recheck_state.valid) return false;
  for (uint8_t u = 0; u < s_recheck_state.unit_count && u < AMS_UNITS_TOTAL; u++) {
    const AmsSlotUnit& unit = s_recheck_state.unit[u];
    if (unit.is_ext || unit.ams_id != ams_id) continue;
    if (tray_id < 0 || tray_id >= unit.tray_count || tray_id >= AMS_MAX_TRAYS) return false;
    *known = true;
    return unit.tray[tray_id].exists;
  }
  return false;
}

void amsPickRecheckTick() {
  if (!s_recheck.active) return;
  if (!backendCanAssignAmsSlot()) { s_recheck.active = false; return; }
  if (millis() - s_recheck.since_ms < kRecheckAtMs[s_recheck.round]) return;

  Recheck& r = s_recheck;
  const bool last = (r.round + 1 >= kRecheckRounds);
  r.round++;
  if (last) r.active = false;

  int ams = -1, tray = -1;
  const int code = backendFindSpoolSlot(r.spool_id, r.printer_id, &ams, &tray, 6000);
  if (code != 200) {
    logSDf("AMSPICK: recheck of spool %d could not ask BamBuddy, HTTP %d", r.spool_id, code);
    return;
  }
  if (ams == r.ams_id && tray == r.tray_id) {
    logSDf("AMSPICK: recheck %u, spool %d still in bay %d/%d", (unsigned)r.round,
           r.spool_id, r.ams_id, r.tray_id);
    return;
  }
  if (ams >= 0) {
    // Moved since, by a tap here or in BamBuddy. Not ours to undo.
    logSDf("AMSPICK: recheck, spool %d is in bay %d/%d now, not %d/%d - leaving it",
           r.spool_id, ams, tray, r.ams_id, r.tray_id);
    r.active = false;
    return;
  }
  // Gone. Only sent again when the bay holds no other spool on file and the
  // AMS reports something in it - an empty bay means the spool went
  // elsewhere, or has not gone in yet.
  const int other = backendFindBaySpool(r.printer_id, r.ams_id, r.tray_id, 6000);
  if (other > 0 && other != r.spool_id) {
    logSDf("AMSPICK: recheck, bay %d/%d holds spool %d now - leaving it",
           r.ams_id, r.tray_id, other);
    r.active = false;
    return;
  }
  bool known = false;
  const bool present = bayHoldsSpool(r.printer_id, r.ams_id, r.tray_id, &known);
  if (!known || !present) {
    logSDf("AMSPICK: recheck, assignment of spool %d to bay %d/%d is gone and the bay %s",
           r.spool_id, r.ams_id, r.tray_id, known ? "is empty" : "could not be read");
    return;
  }
  if (r.resent >= 2) {
    logSDf("AMSPICK: recheck, assignment of spool %d dropped again, giving up", r.spool_id);
    r.active = false;
    return;
  }
  const int ac = serverReachNote(
      backendAssignAmsSlot(r.spool_id, r.printer_id, r.ams_id, r.tray_id, 8000), true);
  r.resent++;
  logSDf("AMSPICK: recheck, BamBuddy had dropped spool %d from bay %d/%d - sent again, HTTP %d",
         r.spool_id, r.ams_id, r.tray_id, ac);
  // Looked at once more in the next round, if there is one.
}
