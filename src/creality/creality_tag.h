#pragma once

#include <stddef.h>
#include <stdint.h>

// ============================================================
//  CREALITY CFS TAGS (K2 Plus, K2 Pro, K2 Max, Hi)
//
//  MIFARE Classic 1K. Sector 1 (blocks 4..6) holds 48 bytes, AES-128-ECB
//  encrypted with a fixed key. Its key A (and key B) is derived from the UID:
//
//    sector_key = AES-128-ECB(SECTOR_SECRET, uid4 x 4)[0..5]
//
//  Decrypted, the 48 bytes are ASCII: a 40 character record, padded with
//  spaces (or zeros, depending on who wrote it):
//
//    date(5) vendor(4) batch(2) filament(6) color(7) length(4) serial(6) reserve(6)
//    AB124   0276      A2       101001      0FFFFFF  0330      000001    000000
//
//  filament is "1" + the five character id of Creality's material database
//  (01001 = Hyper PLA). color is "0" + RRGGBB. length is the filament length
//  in metres, written as decimal digits: 0330 for 1 kg, 0165 for 500 g.
//
//  Everything here is plain C++ without Arduino or reader code, so the decoder
//  can be checked on a PC against a known tag. The reader side is in
//  creality_scan.cpp.
// ============================================================

// The 6 byte key A/B of sector 1. False only if AES itself failed.
bool crealityDeriveSectorKey(const uint8_t uid4[4], uint8_t key_out[6]);

struct CrealityTag {
  char     date[6];        // as written, not decoded: writers disagree on it
  char     vendor_id[5];   // 0276 is Creality
  char     batch[3];
  char     filament_id[7]; // the 6 characters on the tag
  char     serial[7];
  bool     has_color;
  uint8_t  r, g, b;
  uint16_t length_m;       // 0 when the field did not parse
  uint16_t weight_g;       // net filament weight derived from length_m, 0 if unknown

  // From the material table, empty when the id is not in it.
  const char* brand;       // "Creality", "Generic", "eSUN", "Polymaker"
  const char* name;        // "Hyper PLA"
  const char* material;    // "PLA", "PETG-CF"
  uint16_t temp_min, temp_max;
};

// Decrypts the three data blocks of sector 1 and parses them. False when the
// result is not a Creality record, which is what a tag that merely happened to
// authenticate with the derived key would give.
bool crealityDecode(const uint8_t sector1[3][16], CrealityTag* out);

// Exposed for the host test and for the log line.
bool crealityDecrypt48(const uint8_t in[48], uint8_t out[48]);
bool crealityParseRecord(const char* rec40, CrealityTag* out);
