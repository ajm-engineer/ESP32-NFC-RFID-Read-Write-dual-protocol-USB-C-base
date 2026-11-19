#pragma once
#include <Arduino.h>

namespace iso14443 {

enum TagType { NONE = 0, ISODEP, TYPE2, OTHER };

struct Card {
  TagType type = NONE;
  uint8_t uid[10];
  uint8_t uidLen = 0;
  uint8_t atqa[2] = {0, 0};
  uint8_t sak = 0;
  uint8_t ats[32];
  uint8_t atsLen = 0;
};

bool activate(Card &card);

void resetField();

int apdu(const uint8_t *cmd, uint16_t len, uint8_t *resp, uint16_t respMax);
void deselect();

bool t2Read(uint8_t page, uint8_t out16[16]);
bool t2Write(uint8_t page, const uint8_t data4[4]);

const char *lastError();

}
