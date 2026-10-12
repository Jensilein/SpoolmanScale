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
  // be selected again before the derived key can be tried. That is the same
  // plain wake the Snapmaker probe uses. A field reset (RFConfiguration off/on)
  // was tried here on 12.10.2026 and made it worse: the reader stopped finding
  // the card at all and briefly dropped off the bus. Instead the wake is
  // retried, and every step goes to the log, so a tag that does not open says
  // where it stopped.
  //
  // Creality's own spools and the current writers set key A and key B to the
  // same derived key. FilaStation up to v2.10 set only key B, so that is the
  // second try. As with Snapmaker, no key goes to the log.
  uint8_t dummy_uid[NFC_UID_MAX];
  uint8_t dummy_uid_len = 0;
  uint8_t blocks[4][16];

  auto wake = [&]() -> int {
    for (int i = 1; i <= 3; i++) {
      dummy_uid_len = 0;
      if (nfcReadPassiveTarget(dummy_uid, &dummy_uid_len, 300) &&
          dummy_uid_len == 4 && memcmp(dummy_uid, uid, 4) == 0) {
        return i;   // answered on try i
      }
      delay(20);
    }
    return 0;
  };

  int woke_a = wake();
  bool ok_a = woke_a && nfcReadMifareSector(1, key, uid, blocks, NFC_KEY_A);
  int woke_b = 0;
  bool ok_b = false;
  if (!ok_a) {
    woke_b = wake();
    ok_b = woke_b && nfcReadMifareSector(1, key, uid, blocks, NFC_KEY_B);
  }
  if (!ok_a && !ok_b) {
    // woke = on which wake attempt the card answered (0 = never).
    logSDf("NFC: %s not opened with the Creality key (key A: woke=%d read=no, key B: woke=%d read=no)",
           uid_str, woke_a, woke_b);
    return CREALITY_SCAN_NO_AUTH;
  }
  logSDf("NFC: %s opened with the Creality key (%s, woke on try %d)",
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
