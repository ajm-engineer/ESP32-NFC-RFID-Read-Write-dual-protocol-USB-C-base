#include "pn5180.h"

namespace pn5180 {

static uint8_t sNss, sBusy, sRst;
static bool sFieldOn = false;
static SPISettings sSpi(7000000, MSBFIRST, SPI_MODE0);

static bool waitBusy(bool level, uint32_t timeoutMs = 50) {
  uint32_t t0 = millis();
  while (digitalRead(sBusy) != (level ? HIGH : LOW)) {
    if (millis() - t0 > timeoutMs) return false;
  }
  return true;
}

static bool frame(const uint8_t *out, uint8_t *in, uint16_t len) {
  if (!waitBusy(false)) return false;
  SPI.beginTransaction(sSpi);
  digitalWrite(sNss, LOW);
  delayMicroseconds(2);
  for (uint16_t i = 0; i < len; i++) {
    uint8_t b = SPI.transfer(out ? out[i] : 0xFF);
    if (in) in[i] = b;
  }
  bool ok = waitBusy(true);
  digitalWrite(sNss, HIGH);
  SPI.endTransaction();
  return ok && waitBusy(false);
}

static bool command(const uint8_t *cmd, uint16_t cmdLen, uint8_t *resp = nullptr, uint16_t respLen = 0) {
  if (!frame(cmd, nullptr, cmdLen)) return false;
  if (respLen) return frame(nullptr, resp, respLen);
  return true;
}

void begin(uint8_t nss, uint8_t busy, uint8_t rst) {
  sNss = nss; sBusy = busy; sRst = rst;
  pinMode(sNss, OUTPUT); digitalWrite(sNss, HIGH);
  pinMode(sBusy, INPUT);
  pinMode(sRst, OUTPUT); digitalWrite(sRst, HIGH);
  reset();
}

void reset() {
  digitalWrite(sRst, LOW);
  delay(10);
  digitalWrite(sRst, HIGH);
  delay(10);
  uint32_t t0 = millis();
  uint32_t irq = 0;
  while (millis() - t0 < 200) {
    if (readRegister(IRQ_STATUS, &irq) && (irq & IRQ_IDLE)) break;
    delay(2);
  }
  writeRegister(IRQ_CLEAR, 0xFFFFFFFF);
  sFieldOn = false;
}

bool writeRegister(uint8_t reg, uint32_t v) {
  uint8_t c[6] = {WRITE_REGISTER, reg, (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
  return command(c, 6);
}
bool writeRegisterOr(uint8_t reg, uint32_t v) {
  uint8_t c[6] = {WRITE_REGISTER_OR_MASK, reg, (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
  return command(c, 6);
}
bool writeRegisterAnd(uint8_t reg, uint32_t v) {
  uint8_t c[6] = {WRITE_REGISTER_AND_MASK, reg, (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
  return command(c, 6);
}
bool readRegister(uint8_t reg, uint32_t *value) {
  uint8_t c[2] = {READ_REGISTER, reg}, r[4] = {0};
  if (!command(c, 2, r, 4)) return false;
  *value = (uint32_t)r[0] | ((uint32_t)r[1] << 8) | ((uint32_t)r[2] << 16) | ((uint32_t)r[3] << 24);
  return true;
}
bool readEeprom(uint8_t addr, uint8_t *buf, uint8_t len) {
  uint8_t c[3] = {READ_EEPROM, addr, len};
  return command(c, 3, buf, len);
}

bool loadRfConfig(uint8_t tx, uint8_t rx) {
  uint8_t c[3] = {LOAD_RF_CONFIG, tx, rx};
  return command(c, 3);
}

bool rfOn() {
  uint8_t c[2] = {RF_ON, 0x00};
  if (!command(c, 2)) return false;
  delay(10);
  sFieldOn = true;
  return true;
}
bool rfOff() {
  uint8_t c[2] = {RF_OFF, 0x00};
  bool ok = command(c, 2);
  sFieldOn = false;
  return ok;
}
bool fieldIsOn() { return sFieldOn; }

void setCrc(bool tx, bool rx) {
  if (tx) writeRegisterOr(CRC_TX_CONFIG, 0x01); else writeRegisterAnd(CRC_TX_CONFIG, 0xFFFFFFFE);
  if (rx) writeRegisterOr(CRC_RX_CONFIG, 0x01); else writeRegisterAnd(CRC_RX_CONFIG, 0xFFFFFFFE);
}

int transceive(const uint8_t *tx, uint16_t len, uint8_t validBits, uint8_t *rx, uint16_t rxMax,
               uint32_t timeoutMs, uint8_t *rxBits) {
  if (len > 260) return -1;
  writeRegister(IRQ_CLEAR, 0xFFFFFFFF);
  writeRegisterAnd(SYSTEM_CONFIG, 0xFFFFFFF8);
  writeRegisterOr(SYSTEM_CONFIG, 0x00000003);

  uint8_t buf[262];
  buf[0] = SEND_DATA;
  buf[1] = validBits & 0x07;
  memcpy(buf + 2, tx, len);
  if (!command(buf, len + 2)) return -1;

  uint32_t t0 = millis(), irq = 0;
  while (true) {
    if (!readRegister(IRQ_STATUS, &irq)) return -1;
    if (irq & IRQ_RX) break;
    if (irq & IRQ_GENERAL_ERROR) { writeRegister(IRQ_CLEAR, 0xFFFFFFFF); return -1; }
    if (millis() - t0 > timeoutMs) {
      writeRegisterAnd(SYSTEM_CONFIG, 0xFFFFFFF8);
      return 0;
    }
    delayMicroseconds(200);
  }

  uint32_t st = 0;
  readRegister(RX_STATUS, &st);
  uint16_t n = st & 0x1FF;
  if (rxBits) *rxBits = (st >> 13) & 0x07;
  bool rxErr = (st >> 16) & 0x07;
  if (n > rxMax) n = rxMax;
  if (n) {
    uint8_t c[2] = {READ_DATA, 0x00};
    if (!command(c, 2, rx, n)) return -1;
  }
  writeRegister(IRQ_CLEAR, 0xFFFFFFFF);
  if (rxErr && n == 0) return -1;
  return n;
}

}
