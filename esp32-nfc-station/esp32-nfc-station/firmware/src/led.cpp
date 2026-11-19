#include "led.h"
#include <math.h>

namespace led {
static uint8_t sPins[3];
static bool sLow = true;
static State sState = BOOT, sPrevSteady = WAITING;
static uint32_t sSince = 0, sBlipUntil = 0;
static float sScale = 0.6f;

static const int CH[3] = {0, 1, 2};
static void writeRaw(int i, uint16_t v) {
  const uint16_t duty = sLow ? 1023 - v : v;
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(sPins[i], duty);
#else
  ledcWrite(CH[i], duty);
#endif
}
static void rgb(float r, float g, float b) {
  const float c[3] = {r, g, b};
  for (int i = 0; i < 3; i++) {
    float x = c[i] * sScale; if (x < 0) x = 0; if (x > 1) x = 1;
    writeRaw(i, (uint16_t)(x * x * 1023.0f + 0.5f));
  }
}

void begin(uint8_t pinR, uint8_t pinG, uint8_t pinB, bool activeLow) {
  sPins[0] = pinR; sPins[1] = pinG; sPins[2] = pinB; sLow = activeLow;
  for (int i = 0; i < 3; i++) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcAttach(sPins[i], 5000, 10);
#else
    ledcSetup(CH[i], 5000, 10);
    ledcAttachPin(sPins[i], CH[i]);
#endif
  }
  rgb(0, 0, 0);
  set(BOOT);
}

void set(State s) {
  if (s == sState && s != SUCCESS && s != ERROR) return;
  if (s == WAITING || s == IDLE || s == ATTENTION || s == BUSY || s == FAULT || s == OFF) sPrevSteady = s;
  sState = s; sSince = millis();
}
State state() { return sState; }
void blip() { sBlipUntil = millis() + 90; }
void brightness(uint8_t pct) { if (pct < 5) pct = 5; if (pct > 100) pct = 100; sScale = pct / 100.0f; }

bool fromName(const char *n, State &out) {
  static const struct { const char *n; State s; } T[] = {
    {"waiting", WAITING}, {"idle", IDLE}, {"attention", ATTENTION}, {"busy", BUSY},
    {"success", SUCCESS}, {"error", ERROR}, {"fault", FAULT}, {"off", OFF}};
  for (auto &t : T) if (!strcmp(n, t.n)) { out = t.s; return true; }
  return false;
}

void tick() {
  const uint32_t now = millis(), t = now - sSince;
  if (now < sBlipUntil) { rgb(1, 1, 1); return; }
  const float breathe = 0.5f - 0.5f * cosf(t / 1000.0f * 2.0f * (float)M_PI / 3.2f);
  switch (sState) {
    case BOOT: {
      if (t > 1200) { set(WAITING); tick(); return; }
      const float h = t / 1200.0f * 6.0f; const int k = (int)h; const float f = h - k;
      float r = 0, g = 0, b = 0;
      switch (k % 6) { case 0: r = 1; g = f; break; case 1: r = 1 - f; g = 1; break; case 2: g = 1; b = f; break;
                       case 3: g = 1 - f; b = 1; break; case 4: r = f; b = 1; break; default: r = 1; b = 1 - f; }
      rgb(r, g, b); break;
    }
    case WAITING:   rgb(0, 0.15f + 0.55f * breathe, 0.25f + 0.75f * breathe); break;
    case IDLE:      rgb(0.45f, 0.50f, 0.56f); break;
    case ATTENTION: rgb(0.25f + 0.75f * breathe, 0.10f + 0.40f * breathe, 0); break;
    case BUSY: {    const float p = 0.5f - 0.5f * cosf(t / 1000.0f * 2.0f * (float)M_PI / 0.6f); rgb(0, 0.4f + 0.6f * p, 0.5f + 0.5f * p); break; }
    case SUCCESS:   if (t > 1400) { set(IDLE); tick(); return; } rgb(0, 1.0f - (t > 1000 ? (t - 1000) / 400.0f : 0), 0.15f); break;
    case ERROR: {
      if (t > 5 * 320 + 200) { set(IDLE); tick(); return; }
      const bool on = t < 5 * 320 && (t % 320) < 180; rgb(on ? 1 : 0, 0, 0); break; }
    case FAULT:     rgb(((now / 700) % 2) ? 0.9f : 0.05f, 0, 0); break;
    case OFF:       rgb(0, 0, 0); break;
  }
}
}
