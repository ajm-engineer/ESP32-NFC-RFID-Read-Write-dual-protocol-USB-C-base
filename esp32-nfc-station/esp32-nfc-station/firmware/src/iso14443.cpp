#include "iso14443.h"
#include "pn5180.h"

using namespace pn5180;

namespace iso14443 {

static const char *sErr = "";
const char *lastError() { return sErr; }

static uint8_t sBlock = 0;
static uint16_t sFsc = 32;
static uint32_t sFwtMs = 80;
static bool sIsoDepActive = false;

void resetField() {
  sIsoDepActive = false;
  rfOff();
  delay(15);
}

static bool selectLevel(uint8_t sel, uint8_t *uidPart , uint8_t *sakOut) {
  uint8_t rx[16];
  setCrc(false, false);
  uint8_t ac[2] = {sel, 0x20};
  int n = transceive(ac, 2, 0, rx, sizeof rx, 10);
  if (n != 5) { sErr = "no answer to anticollision"; return false; }
  uint8_t bcc = rx[0] ^ rx[1] ^ rx[2] ^ rx[3];
  if (bcc != rx[4]) { sErr = "UID checksum mismatch (two cards in the field?)"; return false; }
  memcpy(uidPart, rx, 4);
  setCrc(true, true);
  uint8_t s[7] = {sel, 0x70, rx[0], rx[1], rx[2], rx[3], rx[4]};
  n = transceive(s, 7, 0, rx, sizeof rx, 10);
  if (n < 1) { sErr = "no answer to select"; return false; }
  *sakOut = rx[0];
  return true;
}

static bool rats(Card &card) {
  uint8_t r[2] = {0xE0, 0x80}, rx[64];
  setCrc(true, true);
  int n = transceive(r, 2, 0, rx, sizeof rx, 60);
  if (n < 1) { sErr = "no answer to RATS"; return false; }
  card.atsLen = min<int>(n, sizeof card.ats);
  memcpy(card.ats, rx, card.atsLen);

  static const uint16_t FSC[] = {16, 24, 32, 40, 48, 64, 96, 128, 256};
  sFsc = 32;
  uint8_t fwi = 4;
  if (n >= 2) {
    uint8_t t0 = rx[1];
    uint8_t fsci = t0 & 0x0F;
    sFsc = FSC[fsci > 8 ? 8 : fsci];
    int i = 2;
    if (t0 & 0x10) i++;
    if ((t0 & 0x20) && i < n) fwi = rx[i] >> 4;
  }
  sFwtMs = (uint32_t)(0.302f * (1u << (fwi > 14 ? 14 : fwi))) + 50;
  if (sFwtMs < 80) sFwtMs = 80;
  sBlock = 0;
  sIsoDepActive = true;
  return true;
}

bool activate(Card &card) {
  card = Card();
  sIsoDepActive = false;
  if (!loadRfConfig(0x00, 0x80)) { sErr = "PN5180 not responding"; return false; }
  if (!fieldIsOn() && !rfOn()) { sErr = "could not turn the field on"; return false; }
  writeRegisterAnd(SYSTEM_CONFIG, 0xFFFFFFBF);

  setCrc(false, false);
  uint8_t reqa = 0x26, rx[8];
  int n = transceive(&reqa, 1, 7, rx, sizeof rx, 5);
  if (n != 2) { sErr = "no card"; return false; }
  card.atqa[0] = rx[0]; card.atqa[1] = rx[1];

  const uint8_t SEL[3] = {0x93, 0x95, 0x97};
  for (int lvl = 0; lvl < 3; lvl++) {
    uint8_t part[4], sak = 0;
    if (!selectLevel(SEL[lvl], part, &sak)) return false;
    if (part[0] == 0x88) {
      memcpy(card.uid + card.uidLen, part + 1, 3); card.uidLen += 3;
    } else {
      memcpy(card.uid + card.uidLen, part, 4); card.uidLen += 4;
    }
    card.sak = sak;
    if (!(sak & 0x04)) break;
  }

  if (card.sak & 0x20) {
    card.type = ISODEP;
    if (!rats(card)) return false;
  } else if ((card.sak & 0x7F) == 0x00) {
    card.type = TYPE2;
  } else {
    card.type = OTHER;
  }
  sErr = "";
  return true;
}

static int exchangeBlock(const uint8_t *tx, uint16_t len, uint8_t *rx, uint16_t rxMax) {
  int n = transceive(tx, len, 0, rx, rxMax, sFwtMs);
  for (int guard = 0; n > 0 && (rx[0] & 0xF7) == 0xF2 && guard < 200; guard++) {
    uint8_t wtxm = (n > 1 ? rx[1] : 1) & 0x3F;
    uint8_t s[2] = {0xF2, wtxm};
    n = transceive(s, 2, 0, rx, rxMax, sFwtMs * (wtxm ? wtxm : 1) + 100);
  }
  return n;
}

int apdu(const uint8_t *cmd, uint16_t len, uint8_t *resp, uint16_t respMax) {
  if (!sIsoDepActive) { sErr = "not an ISO-DEP card (detect first)"; return -1; }
  setCrc(true, true);
  uint8_t frame[260], rx[260];
  const uint16_t maxInf = (sFsc > 3 ? sFsc - 3 : 13);
  uint16_t off = 0;

  int n = 0;
  while (true) {
    uint16_t chunk = min<uint16_t>(maxInf, len - off);
    bool more = (off + chunk) < len;
    frame[0] = 0x02 | sBlock | (more ? 0x10 : 0x00);
    memcpy(frame + 1, cmd + off, chunk);
    n = exchangeBlock(frame, chunk + 1, rx, sizeof rx);
    if (n <= 0) { sErr = "no answer from card"; return -1; }
    if (more) {
      if ((rx[0] & 0xF6) != 0xA2) { sErr = "card did not acknowledge chained block"; return -1; }
      sBlock ^= 1; off += chunk;
      continue;
    }
    break;
  }

  uint16_t outLen = 0;
  while (true) {
    if ((rx[0] & 0xE2) != 0x02) { sErr = "unexpected block from card"; return -1; }
    sBlock ^= 1;
    uint16_t inf = n - 1;
    if (outLen + inf > respMax) { sErr = "response too large"; return -1; }
    memcpy(resp + outLen, rx + 1, inf);
    outLen += inf;
    if (!(rx[0] & 0x10)) break;
    uint8_t ack = 0xA2 | sBlock;
    n = exchangeBlock(&ack, 1, rx, sizeof rx);
    if (n <= 0) { sErr = "card stopped mid-response"; return -1; }
  }
  return outLen;
}

void deselect() {
  if (!sIsoDepActive) return;
  uint8_t s = 0xC2, rx[4];
  setCrc(true, true);
  transceive(&s, 1, 0, rx, sizeof rx, 20);
  sIsoDepActive = false;
}

bool t2Read(uint8_t page, uint8_t out16[16]) {
  setCrc(true, true);
  uint8_t c[2] = {0x30, page}, rx[20];
  int n = transceive(c, 2, 0, rx, sizeof rx, 10);
  if (n < 16) { sErr = "read failed"; return false; }
  memcpy(out16, rx, 16);
  return true;
}

bool t2Write(uint8_t page, const uint8_t data4[4]) {
  setCrc(true, false);
  uint8_t c[6] = {0xA2, page, data4[0], data4[1], data4[2], data4[3]}, rx[4], bits = 0;
  int n = transceive(c, 6, 0, rx, sizeof rx, 15, &bits);
  setCrc(true, true);
  if (n < 1 || (rx[0] & 0x0F) != 0x0A) { sErr = "write not acknowledged (page locked?)"; return false; }
  return true;
}

}
