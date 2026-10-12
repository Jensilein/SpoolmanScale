#include "nfc.h"

#include <Adafruit_PN532.h>
#include <string.h>

static Adafruit_PN532* nfc = nullptr;
static constexpr uint8_t MIFARE_SECTOR_READ_ATTEMPTS = 3;

// Remembered so the reader can be brought back up without the caller having to
// hand the wiring in again - see nfcHardwareReinit().
static TwoWire* nfc_wire = nullptr;
static int8_t   nfc_reset_pin = -1;

bool nfcHardwareBegin(TwoWire* wire, int8_t reset_pin, int8_t irq_pin,
                      uint32_t* firmware_version) {
  if (!nfc) nfc = new Adafruit_PN532(irq_pin, reset_pin, wire);
  if (!nfc) return false;
  nfc_wire = wire;
  nfc_reset_pin = reset_pin;
  nfc->begin();
  delay(100);
  uint32_t version = nfc->getFirmwareVersion();
  if (firmware_version) *firmware_version = version;
  if (!version) return false;
  nfc->SAMConfig();
  return true;
}

// Cheap liveness probe. A PN532 that has locked up stops answering
// GetFirmwareVersion, which is the same symptom that takes the shared I2C bus
// (and with it the scale) down with it.
bool nfcHardwarePing() {
  return nfc && nfc->getFirmwareVersion() != 0;
}

// Re-runs begin() and SAMConfig() on the existing instance. Returns true once
// the reader answers again.
bool nfcHardwareReinit(uint32_t* firmware_version) {
  if (!nfc) return false;
  nfc->begin();
  delay(100);
  uint32_t version = nfc->getFirmwareVersion();
  if (firmware_version) *firmware_version = version;
  if (!version) return false;
  nfc->SAMConfig();
  return true;
}

bool nfcReadPassiveTarget(uint8_t* uid, uint8_t* uid_len, uint16_t timeout_ms) {
  return nfc && nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, uid_len, timeout_ms);
}

// One RFConfiguration "RF field" command. The library's sendCommandCheckAck()
// returns once the response is ready but leaves it unread, which its own
// callers then read with a private function. Left unread here, the next
// command would be written over a pending response; that is the most likely
// reason the first attempt at this (12.10.2026) left the chip unresponsive.
// So the 9 byte response frame (00 00 FF 02 FE D5 33 F8 00), with the leading
// ready byte, is read off the bus here and checked.
static bool rfField(bool on) {
  uint8_t cmd[3] = { 0x32 /* RFConfiguration */, 0x01 /* RF field */,
                     (uint8_t)(on ? 0x01 : 0x00) };
  if (!nfc->sendCommandCheckAck(cmd, 3)) return false;
  if (!nfc_wire) return false;
  uint8_t rx[10] = {0};
  size_t got = nfc_wire->requestFrom((uint8_t)PN532_I2C_ADDRESS, (uint8_t)sizeof(rx));
  for (size_t i = 0; i < got && i < sizeof(rx); i++) rx[i] = nfc_wire->read();
  while (nfc_wire->available()) nfc_wire->read();
  // rx[0] is the ready byte, the frame starts at rx[1].
  return got == sizeof(rx) && rx[0] == PN532_I2C_READY && rx[6] == 0xD5 && rx[7] == 0x33;
}

uint8_t nfcRfFieldCycle() {
  if (!nfc) return 0;
  uint8_t r = 0;
  // 20 ms off and 10 ms to settle worked for about every second wake on an
  // original Creality spool (12.10.2026): the tag's capacitor carries it over
  // a short gap and it stays halted. 100 ms off is far past that, and 50 ms
  // after switching on gives it time to power up before it is spoken to.
  if (rfField(false)) r |= NFC_RF_OFF_OK;
  delay(100);
  if (rfField(true))  r |= NFC_RF_ON_OK;
  delay(50);
  if (nfc->getFirmwareVersion() != 0) r |= NFC_RF_CHIP_OK;
  return r;
}

static bool reselectMifareUid(const uint8_t expected_uid[4], uint16_t timeout_ms) {
  uint8_t uid[NFC_UID_MAX] = {0};
  uint8_t uid_len = 0;
  return nfcReadPassiveTarget(uid, &uid_len, timeout_ms) &&
         uid_len == 4 &&
         memcmp(uid, expected_uid, 4) == 0;
}

static bool authenticateMifareTrailer(uint8_t trailer_block, uint8_t key[6], uint8_t uid[4],
                                      uint8_t key_type) {
  if (key_type == NFC_KEY_A) {
    return nfc->mifareclassic_AuthenticateBlock(uid, 4, trailer_block, NFC_KEY_A, key);
  }
  // Reads as "key B, then key A" and never was: the library turns its key
  // argument into a command with (keyNumber) ? AUTH_B : AUTH_A, and both
  // opcodes passed here are non-zero, so the second call is key B once more.
  // Bambu tags read with key B and have been proven on hardware with exactly
  // these two calls, so they stay as they are.
  if (nfc->mifareclassic_AuthenticateBlock(uid, 4, trailer_block, MIFARE_CMD_AUTH_B, key)) {
    return true;
  }
  return nfc->mifareclassic_AuthenticateBlock(uid, 4, trailer_block, MIFARE_CMD_AUTH_A, key);
}

bool nfcReadMifareSector(int sector, uint8_t key[6], uint8_t uid[4], uint8_t blocks[4][16],
                         uint8_t key_type) {
  uint8_t trailer_block = sector * 4 + 3;

  if (!nfc) return false;

  for (uint8_t attempt = 0; attempt < MIFARE_SECTOR_READ_ATTEMPTS; attempt++) {
    if (attempt > 0) {
      delay(20);
      if (!reselectMifareUid(uid, 120)) {
        continue;
      }
    }

    if (!authenticateMifareTrailer(trailer_block, key, uid, key_type)) {
      continue;
    }

    bool ok = true;
    for (int b = 0; b < 3; b++) {
      int block_num = sector * 4 + b;
      if (!nfc->mifareclassic_ReadDataBlock(block_num, blocks[b])) {
        memset(blocks[b], 0, 16);
        ok = false;
        break;
      }
    }
    if (ok) return true;
  }

  memset(blocks, 0, 4 * 16);
  return false;
}

bool nfcReadMifareBlock(uint8_t block, uint8_t data[16]) {
  return nfc && nfc->mifareclassic_ReadDataBlock(block, data);
}

bool nfcReadNtagPage(uint8_t page, uint8_t* data) {
  if (!nfc) return false;
  for (int retry = 0; retry < 3; retry++) {
    if (nfc->ntag2xx_ReadPage(page, data)) return true;
    delay(2);
  }
  return false;
}

bool nfcWriteNtagPage(uint8_t page, uint8_t* data) {
  return nfc && nfc->ntag2xx_WritePage(page, data);
}
