#include "creality_scan.h"
#include "creality_tag.h"
#include "app/app_state.h"
#include "hardware/nfc.h"
#include "hardware/sd_logger.h"
#include "bambu/bambu_tag.h"
#include "services/spool_color.h"
#include <Arduino.h>
#include <cstring>
#include <stdio.h>

CrealityScanResult scanCrealityTag(uint8_t *uid, uint8_t uid_len) {
  if (uid_len != 4) return CREALITY_SCAN_NO_AUTH;

  uint8_t key[6];
  if (!crealityDeriveSectorKey(uid, key)) return CREALITY_SCAN_NO_AUTH;

  char uid_str[24];
  snprintf(uid_str, sizeof(uid_str), "%02X:%02X:%02X:%02X", uid[0], uid[1], uid[2], uid[3]);

  // The Bambu probe ran first and every sector refused it, so the card has to
  // be selected again before the derived key can be tried. On hardware
  // (12.10.2026) an original Creality spool did not answer a plain wake (REQA)
  // at all afterwards, three times in a row: a refused key leaves it halted,
  // and only losing power brings it back. So when the plain wake gets no
  // answer, the RF field is switched off and on and the wake tried again.
  // A first attempt at that left the reader hanging; nfcRfFieldCycle() now
  // reads each response off the bus and reports which step worked.
  //
  // Creality's own spools and the current writers set key A and key B to the
  // same derived key. FilaStation up to v2.10 set only key B, so that is the
  // second try. As with Snapmaker, no key goes to the log.
  uint8_t dummy_uid[NFC_UID_MAX];
  uint8_t dummy_uid_len = 0;
  uint8_t blocks[4][16];

  // Returns how the card answered: 1 = plain wake, n+1 = after n field
  // cycles, 0 = not at all. Up to three cycles: one was enough only about
  // every second time. Each cycle is logged with what worked.
  bool reader_silent = false;
  auto same_card = [&]() -> bool {
    dummy_uid_len = 0;
    return nfcReadPassiveTarget(dummy_uid, &dummy_uid_len, 300) &&
           dummy_uid_len == 4 && memcmp(dummy_uid, uid, 4) == 0;
  };
  auto wake = [&]() -> int {
    if (same_card()) return 1;
    for (int cycle = 1; cycle <= 3; cycle++) {
      uint8_t rf = nfcRfFieldCycle();
      if (!(rf & NFC_RF_CHIP_OK)) {   // do not pile more commands on it
        logSDf("NFC: %s field cycle %d left the reader SILENT (off=%s on=%s)",
               uid_str, cycle, (rf & NFC_RF_OFF_OK) ? "ok" : "fail",
               (rf & NFC_RF_ON_OK) ? "ok" : "fail");
        reader_silent = true;
        return 0;
      }
      if (same_card()) return cycle + 1;
      delay(30);
      if (same_card()) return cycle + 1;
    }
    logSDf("NFC: %s no answer after 3 field cycles", uid_str);
    return 0;
  };

  int woke_a = wake();
  bool ok_a = woke_a && nfcReadMifareSector(1, key, uid, blocks, NFC_KEY_A);
  int woke_b = 0;
  bool ok_b = false;
  if (!ok_a && !reader_silent) {
    woke_b = wake();
    ok_b = woke_b && nfcReadMifareSector(1, key, uid, blocks, NFC_KEY_B);
  }
  if (!ok_a && !ok_b) {
    // woke: 1 = answered a plain wake, n+1 = after n field cycles, 0 = never.
    logSDf("NFC: %s not opened with the Creality key (key A: woke=%d read=no, key B: woke=%d read=no)",
           uid_str, woke_a, woke_b);
    return CREALITY_SCAN_NO_AUTH;
  }
  logSDf("NFC: %s opened with the Creality key (%s, woke=%d)",
         uid_str, ok_a ? "key A" : "key B", ok_a ? woke_a : woke_b);

  CrealityTag t;
  if (!crealityDecode(blocks, &t)) {
    logSDf("NFC: %s opens with the Creality key but holds no Creality record", uid_str);
    return CREALITY_SCAN_GARBLED;
  }

  memset(&g_tag, 0, sizeof(g_tag));
  memcpy(g_tag.uid, uid, 4);
  snprintf(g_tag.uid_str, sizeof(g_tag.uid_str), "%s", uid_str);
  snprintf(g_tag.decoder, sizeof(g_tag.decoder), "Creality");
  snprintf(g_tag.serial, sizeof(g_tag.serial), "%s", t.serial);

  // The base material ("PLA", "PETG-CF") is what the link list filters on by
  // its first three letters; the product name goes to detailed_filament.
  if (t.material[0]) {
    snprintf(g_tag.material, sizeof(g_tag.material), "%s", t.material);
  }
  if (t.name[0]) {
    snprintf(g_tag.detailed_filament, sizeof(g_tag.detailed_filament), "%s", t.name);
  } else {
    snprintf(g_tag.detailed_filament, sizeof(g_tag.detailed_filament), "Creality ID %s", t.filament_id);
  }
  snprintf(g_tag.vendor, sizeof(g_tag.vendor), "%s", t.brand[0] ? t.brand : "Creality");
  snprintf(g_tag.material_id, sizeof(g_tag.material_id), "%s", t.filament_id);

  if (t.has_color) {
    snprintf(g_tag.color_hex, sizeof(g_tag.color_hex), "#%02X%02X%02X", t.r, t.g, t.b);
    g_tag.color = spoolColorFromRgba(t.r, t.g, t.b, 0xFF);
  }
  g_tag.temp_min = t.temp_min;
  g_tag.temp_max = t.temp_max;
  g_tag.spool_weight = t.weight_g;
  snprintf(g_tag.production_date, sizeof(g_tag.production_date), "%s", t.date);

  // Looked up by UID, like every non-Bambu MIFARE tag.
  snprintf(g_tag.tray_uuid, sizeof(g_tag.tray_uuid), "%s", uid_str);

  logSDf("NFC: Creality tag %s, %s %s, colour %s, %u g, serial %s", uid_str,
         g_tag.vendor, g_tag.detailed_filament,
         g_tag.color_hex[0] ? g_tag.color_hex : "-",
         (unsigned)t.weight_g, t.serial);

  g_tag_ready = true;
  return CREALITY_SCAN_OK;
}
