#include "creality_scan.h"
#include "creality_tag.h"
#include "app/app_state.h"
#include "hardware/nfc.h"
#include "hardware/sd_logger.h"
#include "bambu/bambu_tag.h"
#include "services/spool_color.h"
#include "services/tag_family.h"
#include <Arduino.h>
#include <cstring>
#include <stdio.h>

// Gets a card back that a refused key has left unreachable. Returns how it
// answered: 1 = plain wake, 2 = after the reader released its target,
// 3..4 = after one or two RF field cycles, 0 = not at all.
static int wakeCard(const uint8_t uid[4], const char* uid_str, bool* reader_silent) {
  uint8_t got[NFC_UID_MAX];
  uint8_t got_len = 0;
  auto same_card = [&]() -> bool {
    got_len = 0;
    return nfcReadPassiveTarget(got, &got_len, 300) &&
           got_len == 4 && memcmp(got, uid, 4) == 0;
  };
  if (same_card()) return 1;
  // The reader may still hold the card as its target from the refused key and
  // not list it again; releasing costs one command and resets nothing else.
  bool released = nfcInRelease();
  if (same_card()) return 2;
  for (int cycle = 1; cycle <= 2; cycle++) {
    uint8_t rf = nfcRfFieldCycle();
    if (!(rf & NFC_RF_CHIP_OK)) {   // do not pile more commands on it
      logSDf("NFC: %s field cycle %d left the reader SILENT (off=%s on=%s)",
             uid_str, cycle, (rf & NFC_RF_OFF_OK) ? "ok" : "fail",
             (rf & NFC_RF_ON_OK) ? "ok" : "fail");
      *reader_silent = true;
      return 0;
    }
    if (same_card()) return 2 + cycle;
    delay(30);
    if (same_card()) return 2 + cycle;
  }
  logSDf("NFC: %s no answer to a wake (release %s, 2 field cycles)",
         uid_str, released ? "ok" : "failed");
  return 0;
}

CrealityScanResult scanCrealityTag(uint8_t *uid, uint8_t uid_len, bool fresh) {
  if (uid_len != 4) return CREALITY_SCAN_NO_AUTH;

  uint8_t key[6];
  if (!crealityDeriveSectorKey(uid, key)) return CREALITY_SCAN_NO_AUTH;

  char uid_str[24];
  snprintf(uid_str, sizeof(uid_str), "%02X:%02X:%02X:%02X", uid[0], uid[1], uid[2], uid[3]);

  // Called first now, on a card the poll has just selected and no key has
  // been tried on (12.10.2026): when the Bambu probe ran first, every refused
  // key left the card unreachable, and neither a wake, nor a field cycle of
  // 20 or 100 ms, brought it back reliably. On a fresh card, key A goes
  // straight through, the way the Bambu scan reads its sector 0.
  //
  // Creality's own spools and the current writers set key A and key B to the
  // same derived key. FilaStation up to v2.10 set only key B, so that is the
  // second try, after a wake. Whatever the outcome, a refused card is woken
  // once more before returning, so the Bambu probe that follows finds it.
  // As with Snapmaker, no key goes to the log.
  uint8_t blocks[4][16];
  bool reader_silent = false;

  int woke_a = fresh ? 1 : wakeCard(uid, uid_str, &reader_silent);
  bool ok_a = woke_a && nfcReadMifareSector(1, key, uid, blocks, NFC_KEY_A);
  int woke_b = 0;
  bool ok_b = false;
  if (!ok_a && !reader_silent) {
    woke_b = wakeCard(uid, uid_str, &reader_silent);
    ok_b = woke_b && nfcReadMifareSector(1, key, uid, blocks, NFC_KEY_B);
  }
  if (!ok_a && !ok_b) {
    // woke: 1 = plain wake (or fresh), 2 = after release, 3/4 = after field
    // cycles, 0 = never.
    logSDf("NFC: %s not opened with the Creality key (key A: woke=%d, key B: woke=%d)",
           uid_str, woke_a, woke_b);
    if (!reader_silent) wakeCard(uid, uid_str, &reader_silent);
    return CREALITY_SCAN_NO_AUTH;
  }
  logSDf("NFC: %s opened with the Creality key (%s, woke=%d%s)",
         uid_str, ok_a ? "key A" : "key B", ok_a ? woke_a : woke_b,
         (ok_a && fresh) ? ", first try" : "");

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
  // What v0.8.2 uses to create a spool from a tag. Creality's record has
  // no diameter, and CFS only takes 1.75 mm.
  if (t.material[0]) {
    snprintf(g_tag.filament_type, sizeof(g_tag.filament_type), "%s", t.material);
  }
  g_tag.length_m = t.length_m;
  g_tag.diameter_mm = 1.75f;
  g_tag.color_count = t.has_color ? 1 : 0;
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

  // The card is still authenticated after the read and does not answer the
  // next poll, so the loop declared it removed 1.5 s after every read while
  // the spool lay on the pad (12.10.2026), and found it again moments later.
  // Bring it back to a freshly selected state, the one the polls see.
  bool silent = false;
  int back = wakeCard(uid, uid_str, &silent);
  if (back != 1) {
    logSDf("NFC: %s after the read: %s", uid_str,
           back ? "awake again after a reset" : "no answer to the next poll");
  }

  g_tag_ready = true;
  return CREALITY_SCAN_OK;
}

int crealityRecoverCard(const uint8_t uid[4], const char* uid_str) {
  bool silent = false;
  return wakeCard(uid, uid_str, &silent);
}

// ============================================================
//  WRITING A BLANK TAG
// ============================================================

static const uint8_t FACTORY_KEY[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// A data block nobody has written: all zeros, all FF, or all spaces.
static bool blockIsBlank(const uint8_t b[16]) {
  const uint8_t first = b[0];
  if (first != 0x00 && first != 0xFF && first != 0x20) return false;
  for (int i = 1; i < 16; i++) if (b[i] != first) return false;
  return true;
}

CrealityWriteResult crealityWriteTag(const char rec40[40], uint8_t uid_out[4]) {
  uint8_t uid[NFC_UID_MAX] = {0};
  uint8_t uid_len = 0;
  if (!nfcReadPassiveTarget(uid, &uid_len, 600) || uid_len != 4) {
    return CREALITY_WRITE_NO_TAG;
  }
  memcpy(uid_out, uid, 4);
  char uid_str[24];
  snprintf(uid_str, sizeof(uid_str), "%02X:%02X:%02X:%02X", uid[0], uid[1], uid[2], uid[3]);

  uint8_t key[6];
  uint8_t cipher[48];
  if (!crealityDeriveSectorKey(uid, key) || !crealityEncryptRecord(rec40, cipher)) {
    logSDf("CFS write: %s AES failed", uid_str);
    return CREALITY_WRITE_FAILED;
  }

  bool silent = false;

  // 1. Is sector 1 still on the factory key? A refused key halts the card,
  //    so every "no" is followed by a wake before the next question.
  if (!nfcAuthMifareSector(1, FACTORY_KEY, uid, NFC_KEY_A)) {
    int woke = wakeCard(uid, uid_str, &silent);
    uint8_t blocks[4][16];
    if (woke && !silent && nfcReadMifareSector(1, key, uid, blocks, NFC_KEY_A)) {
      CrealityTag t;
      const bool rec = crealityDecode(blocks, &t);
      logSDf("CFS write: %s already opens with the Creality key%s - not written",
             uid_str, rec ? " and holds a record" : "");
      wakeCard(uid, uid_str, &silent);
      if (rec) tagFamilySet(uid, TAG_FAMILY_CREALITY);
      return CREALITY_WRITE_IS_CREALITY;
    }
    logSDf("CFS write: %s refuses the factory key - not a blank tag, not written", uid_str);
    if (!silent) wakeCard(uid, uid_str, &silent);
    return CREALITY_WRITE_NOT_BLANK;
  }

  uint8_t old[3][16];
  for (int b = 0; b < 3; b++) {
    if (!nfcReadMifareBlock(4 + b, old[b])) {
      logSDf("CFS write: %s block %d unreadable - not written", uid_str, 4 + b);
      wakeCard(uid, uid_str, &silent);
      return CREALITY_WRITE_FAILED;
    }
    if (!blockIsBlank(old[b])) {
      logSDf("CFS write: %s block %d holds data - not written", uid_str, 4 + b);
      wakeCard(uid, uid_str, &silent);
      return CREALITY_WRITE_NOT_BLANK;
    }
  }

  // 2. Sector 1: the record, then the trailer that locks it to the new key.
  //    The trailer goes last, so a failure before it leaves a tag that is
  //    still on the factory key and can simply be written again.
  for (int b = 0; b < 3; b++) {
    if (!nfcWriteMifareBlock(4 + b, cipher + b * 16)) {
      logSDf("CFS write: %s block %d refused", uid_str, 4 + b);
      wakeCard(uid, uid_str, &silent);
      return CREALITY_WRITE_FAILED;
    }
  }
  uint8_t trailer[16];
  memcpy(trailer, key, 6);
  trailer[6] = 0xFF; trailer[7] = 0x07; trailer[8] = 0x80; trailer[9] = 0x69;
  memcpy(trailer + 10, key, 6);
  if (!nfcWriteMifareBlock(7, trailer)) {
    logSDf("CFS write: %s trailer refused", uid_str);
    wakeCard(uid, uid_str, &silent);
    return CREALITY_WRITE_FAILED;
  }

  // 3. Sector 2 holds spaces on Creality's own tags. Only on a blank one, and
  //    nothing depends on it: the printer reads sector 1.
  if (nfcAuthMifareSector(2, FACTORY_KEY, uid, NFC_KEY_A)) {
    uint8_t spaces[16];
    memset(spaces, ' ', 16);
    bool blank2 = true;
    for (int b = 0; b < 3 && blank2; b++) {
      uint8_t d[16];
      blank2 = nfcReadMifareBlock(8 + b, d) && blockIsBlank(d);
    }
    if (blank2) {
      for (int b = 0; b < 3; b++) {
        if (!nfcWriteMifareBlock(8 + b, spaces)) {
          logSDf("CFS write: %s sector 2 block %d refused (not needed)", uid_str, 8 + b);
          break;
        }
      }
    }
  } else {
    logSDf("CFS write: %s sector 2 not on the factory key, left as it is", uid_str);
  }

  // 4. Read it back the way the scale and the printer will.
  int woke = wakeCard(uid, uid_str, &silent);
  uint8_t back[4][16];
  bool verified = false;
  if (woke && !silent && nfcReadMifareSector(1, key, uid, back, NFC_KEY_A)) {
    verified = memcmp(back[0], cipher, 16) == 0 &&
               memcmp(back[1], cipher + 16, 16) == 0 &&
               memcmp(back[2], cipher + 32, 16) == 0;
  }
  if (!silent) wakeCard(uid, uid_str, &silent);
  if (!verified) {
    logSDf("CFS write: %s did not read back as written", uid_str);
    return CREALITY_WRITE_VERIFY_FAILED;
  }

  tagFamilySet(uid, TAG_FAMILY_CREALITY);
  logSDf("CFS write: %s written and verified (%.40s)", uid_str, rec40);
  return CREALITY_WRITE_OK;
}
