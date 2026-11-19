#include "rc522.h"
#include <SPI.h>
#include <MFRC522.h>

namespace rc522 {
static MFRC522 *sDev = nullptr;
static const char *sErr = "";
const char *lastError() { return sErr; }

bool begin(uint8_t ss, uint8_t rst) {
  static MFRC522 dev(ss, rst);
  sDev = &dev;
  sDev->PCD_Init();
  sDev->PCD_AntennaOff();
  const uint8_t v = version();
  return v != 0x00 && v != 0xFF;
}
uint8_t version() { return sDev ? sDev->PCD_ReadRegister(MFRC522::VersionReg) : 0; }

void off() {
  if (!sDev) return;
  sDev->PICC_HaltA();
  sDev->PCD_StopCrypto1();
  sDev->PCD_AntennaOff();
}

bool detect(Card &c) {
  c = Card();
  if (!sDev) { sErr = "RC522 not connected"; return false; }
  sDev->PCD_AntennaOn();
  byte atqa[2]; byte len = sizeof atqa;
  if (sDev->PICC_WakeupA(atqa, &len) != MFRC522::STATUS_OK) { sErr = "no card"; return false; }
  if (sDev->PICC_Select(&sDev->uid) != MFRC522::STATUS_OK) { sErr = "card did not answer select"; return false; }
  c.uidLen = sDev->uid.size;
  memcpy(c.uid, sDev->uid.uidByte, c.uidLen);
  c.sak = sDev->uid.sak;
  switch (sDev->PICC_GetType(c.sak)) {
    case MFRC522::PICC_TYPE_MIFARE_1K: c.type = "mifare_classic_1k"; break;
    case MFRC522::PICC_TYPE_MIFARE_4K: c.type = "mifare_classic_4k"; break;
    case MFRC522::PICC_TYPE_MIFARE_MINI: c.type = "mifare_mini"; break;
    case MFRC522::PICC_TYPE_MIFARE_UL: c.type = "type2"; break;
    case MFRC522::PICC_TYPE_ISO_14443_4: c.type = "isodep"; break;
    case MFRC522::PICC_TYPE_MIFARE_PLUS: c.type = "mifare_plus"; break;
    default: c.type = "other";
  }
  sErr = "";
  return true;
}

static bool auth(uint8_t block, const uint8_t key[6], bool keyB) {
  MFRC522::MIFARE_Key k; memcpy(k.keyByte, key, 6);
  const auto cmd = keyB ? MFRC522::PICC_CMD_MF_AUTH_KEY_B : MFRC522::PICC_CMD_MF_AUTH_KEY_A;
  if (sDev->PCD_Authenticate(cmd, block, &k, &sDev->uid) != MFRC522::STATUS_OK) { sErr = "authentication failed (wrong key?)"; return false; }
  return true;
}

bool mfcRead(uint8_t block, const uint8_t key[6], bool keyB, uint8_t out16[16]) {
  Card c; if (!detect(c)) return false;
  if (!auth(block, key, keyB)) { sDev->PCD_StopCrypto1(); return false; }
  byte buf[18]; byte n = sizeof buf;
  const bool ok = sDev->MIFARE_Read(block, buf, &n) == MFRC522::STATUS_OK;
  sDev->PCD_StopCrypto1();
  if (!ok) { sErr = "read failed"; return false; }
  memcpy(out16, buf, 16);
  return true;
}

bool mfcWrite(uint8_t block, const uint8_t key[6], bool keyB, const uint8_t data16[16]) {
  Card c; if (!detect(c)) return false;
  if (!auth(block, key, keyB)) { sDev->PCD_StopCrypto1(); return false; }
  byte buf[16]; memcpy(buf, data16, 16);
  const bool ok = sDev->MIFARE_Write(block, buf, 16) == MFRC522::STATUS_OK;
  sDev->PCD_StopCrypto1();
  if (!ok) { sErr = "write failed"; return false; }
  return true;
}
}
