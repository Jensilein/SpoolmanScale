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
CrealityScanResult scanCrealityTag(uint8_t *uid, uint8_t uid_len);
