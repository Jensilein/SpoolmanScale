#pragma once
#include <stdint.h>

enum CrealityScanResult {
  CREALITY_SCAN_OK,       // sector 1 opened with the UID's key and decoded
  CREALITY_SCAN_NO_AUTH,  // not a Creality tag (or not one written yet)
  CREALITY_SCAN_GARBLED   // opened, but did not decode to a Creality record
};

// Reads sector 1 of a 4 byte MIFARE Classic tag with the key Creality derives
// from its UID and, on success, fills g_tag the way the Snapmaker decoder
// does, with g_tag.decoder set to "Creality".
// fresh: the poll has just selected the card and no key has been tried on it,
// so key A is tried straight away. Otherwise the card is woken first.
CrealityScanResult scanCrealityTag(uint8_t *uid, uint8_t uid_len, bool fresh);

// A Creality tag that was on the reader and stopped answering the polls is
// woken (release, then up to two field cycles). Same return codes as the
// "woke" in the log: 0 = no answer, 1 = plain wake, 2 = after release,
// 3/4 = after field cycles.
int crealityRecoverCard(const uint8_t uid[4], const char* uid_str);
