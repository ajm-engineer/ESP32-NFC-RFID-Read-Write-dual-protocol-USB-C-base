#pragma once
#include <Arduino.h>
#include <SPI.h>

namespace pn5180 {

enum Cmd : uint8_t {
  WRITE_REGISTER = 0x00,
  WRITE_REGISTER_OR_MASK = 0x01,
  WRITE_REGISTER_AND_MASK = 0x02,
  READ_REGISTER = 0x04,
  READ_EEPROM = 0x07,
  SEND_DATA = 0x09,
  READ_DATA = 0x0A,
  LOAD_RF_CONFIG = 0x11,
  RF_ON = 0x16,
  RF_OFF = 0x17,
};

enum Reg : uint8_t {
  SYSTEM_CONFIG = 0x00,
  IRQ_ENABLE = 0x01,
  IRQ_STATUS = 0x02,
  IRQ_CLEAR = 0x03,
  TRANSCEIVE_CONTROL = 0x04,
  TIMER1_RELOAD = 0x0C,
  TIMER1_CONFIG = 0x0F,
  RX_WAIT_CONFIG = 0x11,
  CRC_RX_CONFIG = 0x12,
  RX_STATUS = 0x13,
  TX_WAIT_CONFIG = 0x17,
  TX_CONFIG = 0x18,
  CRC_TX_CONFIG = 0x19,
  RF_STATUS = 0x1D,
  SYSTEM_STATUS = 0x24,
};

constexpr uint32_t IRQ_RX = 1u << 0;
constexpr uint32_t IRQ_TX = 1u << 1;
constexpr uint32_t IRQ_IDLE = 1u << 2;
constexpr uint32_t IRQ_GENERAL_ERROR = 1u << 17;

constexpr uint8_t EE_PRODUCT_VERSION = 0x10;
constexpr uint8_t EE_FIRMWARE_VERSION = 0x12;
constexpr uint8_t EE_EEPROM_VERSION = 0x14;

void begin(uint8_t nss, uint8_t busy, uint8_t rst);
void reset();

bool writeRegister(uint8_t reg, uint32_t value);
bool writeRegisterOr(uint8_t reg, uint32_t mask);
bool writeRegisterAnd(uint8_t reg, uint32_t mask);
bool readRegister(uint8_t reg, uint32_t *value);
bool readEeprom(uint8_t addr, uint8_t *buf, uint8_t len);

bool loadRfConfig(uint8_t tx, uint8_t rx);
bool rfOn();
bool rfOff();
bool fieldIsOn();

void setCrc(bool tx, bool rx);

int transceive(const uint8_t *tx, uint16_t len, uint8_t validBits, uint8_t *rx, uint16_t rxMax,
               uint32_t timeoutMs, uint8_t *rxBits = nullptr);

}
