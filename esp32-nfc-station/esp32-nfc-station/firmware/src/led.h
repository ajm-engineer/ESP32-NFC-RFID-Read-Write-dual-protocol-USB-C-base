#pragma once
#include <Arduino.h>

namespace led {
enum State : uint8_t {
  BOOT,
  WAITING,
  IDLE,
  ATTENTION,
  BUSY,
  SUCCESS,
  ERROR,
  FAULT,
  OFF,
};
void begin(uint8_t pinR, uint8_t pinG, uint8_t pinB, bool activeLow);
void set(State s);
State state();
bool fromName(const char *name, State &out);
void blip();
void tick();
void brightness(uint8_t pct);
}
