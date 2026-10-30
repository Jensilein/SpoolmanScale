#pragma once

#include <ArduinoJson.h>
#include <stddef.h>
#include <stdint.h>

#include "services/mifare_write.h"

// ============================================================
//  CREALITY CFS AS A MIFARE WRITE FORMAT
//
//  What goes onto a blank tag for the CFS: sector 1 gets the encrypted
//  40 character record (creality_tag.h) and the key Creality derives from the
//  UID as key A and key B; sector 2 gets spaces, as Creality's own tags carry.
//  The writing itself is services/mifare_write.cpp.
// ============================================================

// The 40 character record the spool stands for right now. The spool as the
// backend returned it, in Spoolman's shape.
bool crealityRecordForSpool(JsonObjectConst spool, int spool_id, char rec[41]);

// The two entries of the format table, services/mifare_formats.cpp.
bool crealityBuildJob(JsonObjectConst spool, int spool_id, MifareWriteJob* job,
                      char* text, size_t text_len);
void crealityWritten(const uint8_t uid[4], const char* text);

// Sector 1's data blocks hold a Creality record. Also what the factory key
// probe asks to recognise a CFS write that stopped before the trailer.
bool crealityIsRecord(const uint8_t blocks[MIFARE_DATA_BLOCKS_MAX][MIFARE_BLOCK_BYTES]);
