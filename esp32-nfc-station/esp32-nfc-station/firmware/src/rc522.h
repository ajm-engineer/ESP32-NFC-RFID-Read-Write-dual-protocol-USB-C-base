#pragma once
#include <Arduino.h>

namespace rc522 {
bool begin(uint8_t ss, uint8_t rst);
uint8_t version();
void off();
struct Card { uint8_t uid[10]; uint8_t uidLen = 0; uint8_t sak = 0; const char *type = "none"; };
bool detect(Card &c);
bool mfcRead(uint8_t block, const uint8_t key[6], bool keyB, uint8_t out16[16]);
bool mfcWrite(uint8_t block, const uint8_t key[6], bool keyB, const uint8_t data16[16]);
const char *lastError();
}
