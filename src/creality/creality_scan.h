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

enum CrealityWriteResult {
  CREALITY_WRITE_OK,
  CREALITY_WRITE_NO_TAG,        // nothing (or no 4 byte MIFARE tag) on the reader
  CREALITY_WRITE_IS_CREALITY,   // already opens with the Creality key: left alone
  CREALITY_WRITE_NOT_BLANK,     // other keys, or data in sector 1: left alone
  CREALITY_WRITE_FAILED,        // a block write was refused part way through
  CREALITY_WRITE_VERIFY_FAILED, // written, but did not read back as written
};

// Puts a 40 character Creality record (crealityBuildRecord) on the blank
// MIFARE Classic tag on the reader. Blank means: sector 1 opens with the
// factory key FFFFFFFFFFFF and its three data blocks hold nothing. Anything
// else, an original Creality tag above all, is refused before a byte changes.
// Sector 1 gets the encrypted record and the UID's derived key as key A and
// key B; sector 2 gets spaces, as Creality's own tags carry. The write is
// read back with the new key before it counts. uid_out receives the UID.
CrealityWriteResult crealityWriteTag(const char rec40[40], uint8_t uid_out[4]);

// Whether the card on the reader is a blank MIFARE Classic tag: sector 1
// opens with the factory key FFFFFFFFFFFF and its data blocks hold nothing.
// A blank tag refuses every Bambu and Creality key, and each refusal knocks
// it off the reader for a while, so the main poll lost it within two seconds
// and a link made on it found no UID any more (13.10.2026). The card is left
// selectable either way.
bool crealityProbeBlank(const uint8_t uid[4], const char* uid_str);
