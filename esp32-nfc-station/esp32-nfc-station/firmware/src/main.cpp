#include <Arduino.h>
#include <ArduinoJson.h>
#include "pins.h"
#include "pn5180.h"
#include "iso14443.h"
#include "rc522.h"
#include "led.h"

static const char *FW = "esp32-nfc-station 0.3.0";
static bool gPnOk = false, gHostSeen = false;
static bool gHasRc = false;

static void usePn() { if (gHasRc) rc522::off(); }
static void useRc() { iso14443::resetField(); }
static iso14443::Card gCard;
static uint32_t gLastActivity = 0;

static String toHex(const uint8_t *b, size_t n) {
  static const char *H = "0123456789ABCDEF";
  String s; s.reserve(n * 2);
  for (size_t i = 0; i < n; i++) { s += H[b[i] >> 4]; s += H[b[i] & 15]; }
  return s;
}
static int fromHex(const char *s, uint8_t *out, size_t max) {
  size_t len = strlen(s);
  if (len % 2) return -1;
  size_t n = len / 2;
  if (n > max) return -1;
  for (size_t i = 0; i < n; i++) {
    char a = s[2 * i], b = s[2 * i + 1];
    auto v = [](char c) -> int { if (c >= '0' && c <= '9') return c - '0'; c |= 0x20; if (c >= 'a' && c <= 'f') return c - 'a' + 10; return -1; };
    int hi = v(a), lo = v(b);
    if (hi < 0 || lo < 0) return -1;
    out[i] = (hi << 4) | lo;
  }
  return n;
}

static void reply(JsonDocument &doc) { serializeJson(doc, Serial); Serial.print('\n'); }
static void fail(int id, const char *msg) {
  JsonDocument d; d["id"] = id; d["ok"] = false; d["error"] = msg; reply(d);
  if (led::state() != led::FAULT) led::set(led::ERROR);
}

static const char *typeName(iso14443::TagType t) {
  switch (t) { case iso14443::ISODEP: return "isodep"; case iso14443::TYPE2: return "type2"; case iso14443::OTHER: return "other"; default: return "none"; }
}

static void handle(JsonDocument &in) {
  int id = in["id"] | 0;
  const char *cmd = in["cmd"] | "";
  JsonDocument out; out["id"] = id; out["ok"] = true;
  gLastActivity = millis();
  if (!gHostSeen) { gHostSeen = true; if (led::state() == led::WAITING) led::set(led::IDLE); }

  if (!strcmp(cmd, "hello")) {
    uint8_t pv[2] = {0}, fv[2] = {0}, ev[2] = {0};
    pn5180::readEeprom(pn5180::EE_PRODUCT_VERSION, pv, 2);
    pn5180::readEeprom(pn5180::EE_FIRMWARE_VERSION, fv, 2);
    pn5180::readEeprom(pn5180::EE_EEPROM_VERSION, ev, 2);
    out["fw"] = FW;
    JsonObject p = out["pn5180"].to<JsonObject>();
    p["product"] = String(pv[1]) + "." + String(pv[0]);
    p["firmware"] = String(fv[1]) + "." + String(fv[0]);
    p["eeprom"] = String(ev[1]) + "." + String(ev[0]);
    p["ok"] = !(pv[0] == 0xFF && pv[1] == 0xFF) && !(pv[0] == 0 && pv[1] == 0);
    JsonObject r = out["rc522"].to<JsonObject>();
    r["ok"] = gHasRc;
    if (gHasRc) { char v[5]; snprintf(v, sizeof v, "0x%02X", rc522::version()); r["version"] = v; }
    reply(out); return;
  }

  if (!strcmp(cmd, "detect") || !strcmp(cmd, "apdu") || !strcmp(cmd, "t2read") || !strcmp(cmd, "t2write") || !strcmp(cmd, "rf")) usePn();

  if (!strcmp(cmd, "detect")) {
    if (!iso14443::activate(gCard)) {
      out["present"] = false; out["reason"] = iso14443::lastError();
      reply(out); return;
    }
    out["present"] = true; led::blip();
    out["type"] = typeName(gCard.type);
    out["uid"] = toHex(gCard.uid, gCard.uidLen);
    out["atqa"] = toHex(gCard.atqa, 2);
    out["sak"] = toHex(&gCard.sak, 1);
    if (gCard.atsLen) out["ats"] = toHex(gCard.ats, gCard.atsLen);
    digitalWrite(PIN_LED, HIGH);
    reply(out); return;
  }

  if (!strcmp(cmd, "apdu")) {
    static uint8_t c[1024], r[1024];
    int n = fromHex(in["hex"] | "", c, sizeof c);
    if (n <= 0) return fail(id, "bad hex");
    int m = iso14443::apdu(c, n, r, sizeof r);
    if (m < 0) return fail(id, iso14443::lastError());
    out["hex"] = toHex(r, m);
    reply(out); return;
  }

  if (!strcmp(cmd, "t2read")) {
    uint8_t b[16];
    if (!iso14443::t2Read(in["page"] | 0, b)) return fail(id, iso14443::lastError());
    out["hex"] = toHex(b, 16);
    reply(out); return;
  }

  if (!strcmp(cmd, "t2write")) {
    uint8_t b[4];
    if (fromHex(in["hex"] | "", b, 4) != 4) return fail(id, "need exactly 4 bytes");
    if (!iso14443::t2Write(in["page"] | 0, b)) return fail(id, iso14443::lastError());
    reply(out); return;
  }

  if (!strcmp(cmd, "release")) {
    iso14443::deselect();
    iso14443::resetField();
    digitalWrite(PIN_LED, LOW);
    reply(out); return;
  }

  if (!strcmp(cmd, "rf")) {
    bool on = in["on"] | false;
    if (on) { pn5180::loadRfConfig(0x00, 0x80); pn5180::rfOn(); } else iso14443::resetField();
    digitalWrite(PIN_LED, on ? HIGH : LOW);
    reply(out); return;
  }

  if (!strcmp(cmd, "led")) {
    const char *st = in["state"] | "";
    led::State s;
    if (*st) { if (!led::fromName(st, s)) return fail(id, "unknown light state"); if (led::state() != led::FAULT || s == led::OFF) led::set(s); }
    if (!in["brightness"].isNull()) led::brightness(in["brightness"] | 60);
    reply(out); return;
  }

  if (!strncmp(cmd, "rc_", 3)) {
    if (!gHasRc) return fail(id, "RC522 not connected (check wiring and 3.3 V power)");
    useRc();
    if (!strcmp(cmd, "rc_detect")) {
      rc522::Card c;
      if (!rc522::detect(c)) { out["present"] = false; reply(out); return; }
      out["present"] = true; led::blip(); out["type"] = c.type; out["uid"] = toHex(c.uid, c.uidLen); out["sak"] = toHex(&c.sak, 1);
      reply(out); return;
    }
    if (!strcmp(cmd, "rc_mfc_read") || !strcmp(cmd, "rc_mfc_write")) {
      uint8_t key[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
      const char *k = in["key"] | "";
      if (*k && fromHex(k, key, 6) != 6) return fail(id, "key must be 6 bytes (12 hex characters)");
      const int block = in["block"] | -1;
      if (block < 0 || block > 255) return fail(id, "block out of range");
      const bool keyB = in["keyB"] | false;
      if (!strcmp(cmd, "rc_mfc_read")) {
        uint8_t b[16];
        if (!rc522::mfcRead(block, key, keyB, b)) return fail(id, rc522::lastError());
        out["hex"] = toHex(b, 16); reply(out); return;
      }
      if (block == 0 || (block % 4) == 3) { if (!(in["force"] | false)) return fail(id, "refusing to write block 0 or a sector trailer without force:true"); }
      uint8_t d[16];
      if (fromHex(in["hex"] | "", d, 16) != 16) return fail(id, "need exactly 16 bytes");
      if (!rc522::mfcWrite(block, key, keyB, d)) return fail(id, rc522::lastError());
      reply(out); return;
    }
    if (!strcmp(cmd, "rc_off")) { rc522::off(); reply(out); return; }
  }

  fail(id, "unknown command");
}

void setup() {
  led::begin(PIN_RGB_R, PIN_RGB_G, PIN_RGB_B, RGB_ACTIVE_LOW);
  Serial.setRxBufferSize(8192);
  Serial.begin(460800);
  pinMode(PIN_LED, OUTPUT);
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, -1);
  pn5180::begin(PIN_NSS, PIN_BUSY, PIN_RST);
  gHasRc = rc522::begin(PIN_RC_SS, PIN_RC_RST);
  { uint8_t pv[2] = {0}; pn5180::readEeprom(pn5180::EE_PRODUCT_VERSION, pv, 2);
    gPnOk = !(pv[0] == 0xFF && pv[1] == 0xFF) && !(pv[0] == 0 && pv[1] == 0); }
  const uint32_t t0 = millis(); while (millis() - t0 < 1200) { led::tick(); delay(10); }
  led::set(gPnOk ? led::WAITING : led::FAULT);
  JsonDocument d; d["event"] = "ready"; d["fw"] = FW; d["rc522"] = gHasRc; reply(d);
}

void loop() {
  led::tick();
  if (gHostSeen && millis() - gLastActivity > 7000) { gHostSeen = false; if (led::state() != led::FAULT) led::set(led::WAITING); }
  static String line;
  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch == '\n') {
      if (line.length()) {
        JsonDocument in;
        if (deserializeJson(in, line)) { JsonDocument e; e["ok"] = false; e["error"] = "bad json"; reply(e); }
        else handle(in);
      }
      line = "";
    } else if (ch != '\r') {
      if (line.length() < 4000) line += ch;
    }
  }
  if (pn5180::fieldIsOn() && millis() - gLastActivity > 15000) {
    iso14443::resetField();
    digitalWrite(PIN_LED, LOW);
  }
}
