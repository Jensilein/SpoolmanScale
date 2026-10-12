#pragma once

#include <stdint.h>

// ============================================================
//  TAG FAMILY MEMORY
//
//  Bambu and Creality CFS tags are both 4 byte MIFARE Classic cards, and
//  nothing readable without a key tells them apart. Every refused key leaves
//  the card unreachable for a while (seen on hardware, 12.10.2026), so trying
//  the wrong family first costs the right one its read. The scale therefore
//  remembers, per UID, which family opened the card, and goes straight to it
//  the next time.
//
//    TAG_FAMILY_UNKNOWN       never read: Creality first (when enabled)
//    TAG_FAMILY_CREALITY      opened with the Creality key
//    TAG_FAMILY_BAMBU         read as a Bambu tag
//    TAG_FAMILY_NOT_CREALITY  refused the Creality key on the last placement:
//                             Bambu first on the next one. RAM only, and used
//                             once: the next placement drops it, so an unknown
//                             tag alternates Creality first / Bambu first until
//                             one of them reads it. A Creality tag that was
//                             merely badly placed gets Creality again.
//
//  Creality and Bambu are stored in NVS (one string, at most 48 UIDs, the
//  oldest dropped first) and survive a restart. A write happens only when a
//  UID's family changes.
// ============================================================

enum TagFamily : char {
  TAG_FAMILY_UNKNOWN      = 0,
  TAG_FAMILY_CREALITY     = 'C',
  TAG_FAMILY_BAMBU        = 'B',
  TAG_FAMILY_NOT_CREALITY = 'n',
};

TagFamily tagFamilyGet(const uint8_t uid[4]);
// Records a family for the UID. TAG_FAMILY_UNKNOWN removes the entry.
void tagFamilySet(const uint8_t uid[4], TagFamily family);
