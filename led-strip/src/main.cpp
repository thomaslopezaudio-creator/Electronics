// LED strip controller for ESP32-S3 + WS2812B, with a local web UI.
//
// The strip is one 150-LED path around the desk. We split it into SEGMENTS
// (one per physical "side"), grouped into SECTIONS (one per physical object):
//
//   Pegboard: bottom, right, top, left
//   Panel:    down
//   Desk:     back, leg
//
// Segment i covers LEDs [bounds[i], bounds[i+1]). Boundaries are calibrated
// from the web UI's Layout panel and saved to flash.
//
// On top of that sit SCENES (named looks: Focus, Cook, Movie, Night...) and a
// SCHEDULE that applies scenes at times of day, crossfading slowly so the room
// changes without anyone noticing a switch flip.
//
// Structure, top to bottom:
//   1. hardware config      — pins, LED count, current limit
//   2. layout + state       — segments, scenes, schedule, persisted struct
//   3. persistence          — save/load to flash (NVS)
//   4. time + schedule      — NTP, which scene should be on right now
//   5. rendering            — state -> pixels, with crossfade
//   6. HTTP API             — lets the web page read/modify everything
//   7. setup()/loop()       — boot sequence and the frame loop
//
// The network code NEVER touches the LEDs directly. It only edits state.
// loop() redraws the strip from state ~45x per second.

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <FastLED.h>
#include <ESPAsyncWebServer.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <time.h>
#include "secrets.h"
#include "web_page.h"

// ---------- 1. hardware config ----------
#ifndef LED_PIN
#define LED_PIN 4
#endif
#ifndef NUM_LEDS
#define NUM_LEDS 150
#endif
#define LED_TYPE      WS2812B
#define COLOR_ORDER   GRB
#define ONBOARD_PIN   48       // devkit's own RGB LED; mirrors strip pixel 0 as a diagnostic
#define MAX_MILLIAMPS 1000     // FastLED dims the strip to stay under this. Raise once the
                               // strip's power wiring is heavy enough (18 AWG).

CRGB leds[NUM_LEDS];     // physical strip, what FastLED pushes out
CRGB frame[NUM_LEDS];    // logical layout; copied to leds (optionally mirrored)
CRGB onboard[1];

// ---------- 2. layout + state ----------
struct SegDef { const char* section; const char* side; uint8_t sectionIdx; };
const SegDef SEG_DEFS[] = {
  { "Pegboard", "bottom", 0 },
  { "Pegboard", "right",  0 },
  { "Pegboard", "top",    0 },
  { "Pegboard", "left",   0 },
  { "Panel",    "down",   1 },
  { "Desk",     "back",   2 },
  { "Desk",     "leg",    2 },
};
const uint8_t NUM_SEGS     = sizeof(SEG_DEFS) / sizeof(SEG_DEFS[0]);
const uint8_t NUM_SECTIONS = 3;
const char*   SECTION_NAMES[NUM_SECTIONS] = { "Pegboard", "Panel", "Desk" };

enum Effect : uint8_t { SOLID, RAINBOW, BREATHE, CHASE, EFFECT_COUNT };
const char* EFFECT_NAMES[EFFECT_COUNT] = { "solid", "rainbow", "breathe", "chase" };

struct SegState {
  bool    on     = true;
  uint8_t effect = SOLID;
  uint8_t speed  = 128;
  CRGB    color  = CRGB(255, 170, 90);
  uint8_t level  = 255;   // per-segment dimmer, used by scenes to balance sections
};

// Scenes are compile-time: a curated set of looks. Kelvin values follow the
// circadian-lighting convention — cool and bright by day, warm and dim at night.
struct Scene {
  const char* id;
  const char* name;
  const char* hint;
  bool        on;
  uint16_t    kelvin;      // 0 => use rgb instead
  uint32_t    rgb;
  uint8_t     brightness;
  uint8_t     effect;
  uint8_t     speed;
  uint8_t     scale[NUM_SECTIONS];   // per-section level, lets a scene favour one area
};

const Scene SCENES[] = {
  // id           name          hint                        on  K     rgb      bri  effect   spd  scale{Peg,Panel,Desk}
  { "off",       "Off",        "everything dark",          false,    0, 0x000000,   0, SOLID,   128, {255,255,255} },
  { "wake",      "Wake",       "soft warm start",           true, 2700, 0,          70, SOLID,   128, {255,255,255} },
  { "focus",     "Focus",      "cool + bright, for work",   true, 5000, 0,         210, SOLID,   128, {255,255,255} },
  { "midday",    "Midday",     "brightest, daylight",     true, 5600, 0,         235, SOLID,   128, {255,255,255} },
  { "cook",      "Cook",       "bright neutral room light", true, 4200, 0,         235, SOLID,   128, {255,255,255} },
  { "afternoon", "Afternoon",  "warming up",                true, 4000, 0,         190, SOLID,   128, {255,255,255} },
  { "evening",   "Evening",    "warm, relaxed",             true, 2700, 0,         120, SOLID,   128, {255,255,255} },
  { "movie",     "Movie",      "dim bias light for TV",     true, 2000, 0,          35, SOLID,   128, { 70,200,255} },
  { "winddown",  "Wind down",  "low + warm, pre-sleep",     true, 2200, 0,          70, SOLID,   128, {255,255,255} },
  { "night",     "Night",      "minimum amber, night vision",true,1800, 0,          18, SOLID,   128, {160,255,255} },
  { "party",     "Party",      "rainbow, full send",        true,    0, 0xff0080,  170, RAINBOW, 170, {255,255,255} },
};
const uint8_t NUM_SCENES = sizeof(SCENES) / sizeof(SCENES[0]);

struct SchedEntry {
  uint16_t minutes;   // minutes since local midnight
  uint8_t  scene;     // index into SCENES
  bool     enabled;
};
const uint8_t MAX_SCHED = 8;

// Everything saved to flash lives in this one struct so a single
// putBytes/getBytes handles it. Bump VERSION when the layout changes.
const uint32_t PERSIST_VERSION = 3;
struct Persisted {
  uint32_t version    = PERSIST_VERSION;
  uint8_t  brightness = 120;
  bool     flipped    = false;      // mirror the layout onto the physical strip
  bool     autoMode   = true;       // follow the schedule
  uint8_t  fadeMin    = 20;         // crossfade length for scheduled changes, minutes
  // Calibrated on the real desk 2026-09-23. These are the defaults so a
  // settings reset doesn't lose the layout.
  uint16_t bounds[NUM_SEGS + 1] = { 0, 23, 45, 68, 91, 96, 118, NUM_LEDS };
  SegState seg[NUM_SEGS];
  SchedEntry sched[MAX_SCHED] = {
    {  6 * 60 + 30, 1, true },   // 06:30 Wake
    {  8 * 60 +  0, 2, true },   // 08:00 Focus
    { 12 * 60 +  0, 3, true },   // 12:00 Midday
    { 17 * 60 +  0, 5, true },   // 17:00 Afternoon
    { 19 * 60 +  0, 6, true },   // 19:00 Evening
    { 21 * 60 + 30, 8, true },   // 21:30 Wind down
    { 23 * 60 +  0, 9, true },   // 23:00 Night
    {  1 * 60 +  0, 0, true },   // 01:00 Off
  };
} P;

int8_t  activeScene  = -1;   // last scene applied (manual or scheduled)
int8_t  activeSched  = -1;   // schedule slot currently in force

// Locate mode: temporarily override the render so the user can see a
// segment or a boundary on the physical strip. Expires on its own.
int8_t   locateSeg   = -1;
int8_t   locateBound = -1;
uint32_t locateUntil = 0;

// ---------- 3. persistence ----------
Preferences prefs;
uint32_t dirtyAt = 0;   // millis() of last change; 0 = clean
uint32_t frames  = 0;   // frames pushed to the strip since boot (diagnostic)

void markDirty() { dirtyAt = millis(); }

void savePersisted() {
  prefs.putBytes("p", &P, sizeof P);
  dirtyAt = 0;
}

void loadPersisted() {
  prefs.begin("led", false);
  if (prefs.getBytesLength("p") == sizeof P) {
    Persisted tmp;
    prefs.getBytes("p", &tmp, sizeof tmp);
    if (tmp.version == PERSIST_VERSION) P = tmp;
    else Serial.printf("saved settings are v%lu, expected v%lu — using defaults\n",
                       (unsigned long)tmp.version, (unsigned long)PERSIST_VERSION);
  }
  // Bounds must be monotonic and within the strip, whatever flash says.
  P.bounds[0] = 0; P.bounds[NUM_SEGS] = NUM_LEDS;
  for (uint8_t i = 1; i < NUM_SEGS; i++)
    P.bounds[i] = constrain(P.bounds[i], P.bounds[i - 1], NUM_LEDS);
}

// ---------- 4. time + schedule ----------
bool timeValid = false;

// Minutes since local midnight, or -1 if the clock isn't set yet.
int minutesNow() {
  struct tm t;
  if (!getLocalTime(&t, 0)) return -1;
  return t.tm_hour * 60 + t.tm_min;
}

String clockString() {
  struct tm t;
  if (!getLocalTime(&t, 0)) return String("--:--");
  char buf[8];
  snprintf(buf, sizeof buf, "%02d:%02d", t.tm_hour, t.tm_min);
  return String(buf);
}

// Which schedule slot is in force right now: the most recent enabled entry
// at or before the current time, wrapping around midnight.
int currentSchedIdx() {
  int now = minutesNow();
  if (now < 0) return -1;
  int best = -1, bestDelta = 1 << 30;
  for (uint8_t i = 0; i < MAX_SCHED; i++) {
    if (!P.sched[i].enabled) continue;
    int d = now - (int)P.sched[i].minutes;
    if (d < 0) d += 24 * 60;              // it fired yesterday
    if (d < bestDelta) { bestDelta = d; best = i; }
  }
  return best;
}

// The next slot that will fire, for the "next change" line in the UI.
int nextSchedIdx() {
  int now = minutesNow();
  if (now < 0) return -1;
  int best = -1, bestDelta = 1 << 30;
  for (uint8_t i = 0; i < MAX_SCHED; i++) {
    if (!P.sched[i].enabled) continue;
    int d = (int)P.sched[i].minutes - now;
    if (d <= 0) d += 24 * 60;
    if (d < bestDelta) { bestDelta = d; best = i; }
  }
  return best;
}

// ---------- 5. rendering ----------
// Crossfade: `fadeFrom*` holds what was on screen when the change started;
// each frame we blend toward the target in P by however far along we are.
// Manual changes use a short fade, scheduled ones a long one, so the room
// drifts between scenes instead of snapping.
CRGB    fadeFromColor[NUM_SEGS];
uint8_t fadeFromLevel[NUM_SEGS];
uint8_t fadeFromBright;
CRGB    liveColor[NUM_SEGS];     // what's actually rendered this frame
uint8_t liveLevel[NUM_SEGS];
uint8_t liveBright;
uint32_t fadeStart = 0, fadeDur = 1;

void startFade(uint32_t durMs) {
  for (uint8_t i = 0; i < NUM_SEGS; i++) {
    fadeFromColor[i] = liveColor[i];
    fadeFromLevel[i] = liveLevel[i];
  }
  fadeFromBright = liveBright;
  fadeStart = millis();
  fadeDur   = max((uint32_t)1, durMs);
}

// Approximate black-body colour for a temperature in Kelvin (Tanner Helland's
// curve fit). Lets scenes be written as "2700 K" instead of magic RGB values.
CRGB kelvinToRGB(uint16_t k) {
  float t = k / 100.0f, r, g, b;
  if (t <= 66) { r = 255; g = 99.4708025861f * logf(t) - 161.1195681661f; }
  else { r = 329.698727446f * powf(t - 60, -0.1332047592f);
         g = 288.1221695283f * powf(t - 60, -0.0755148492f); }
  if (t >= 66)      b = 255;
  else if (t <= 19) b = 0;
  else              b = 138.5177312231f * logf(t - 10) - 305.0447927307f;
  return CRGB(constrain((int)r, 0, 255), constrain((int)g, 0, 255), constrain((int)b, 0, 255));
}

void applyScene(uint8_t idx, uint32_t fadeMs) {
  if (idx >= NUM_SCENES) return;
  const Scene& s = SCENES[idx];
  startFade(fadeMs);
  P.brightness = s.brightness;
  for (uint8_t i = 0; i < NUM_SEGS; i++) {
    SegState& seg = P.seg[i];
    seg.on     = s.on;
    seg.effect = s.effect;
    seg.speed  = s.speed;
    seg.color  = s.kelvin ? kelvinToRGB(s.kelvin) : CRGB(s.rgb);
    seg.level  = s.scale[SEG_DEFS[i].sectionIdx];
  }
  activeScene = idx;
  markDirty();
}

void renderSegment(uint8_t i, uint32_t t) {
  // Snapshot both ends: the web task may move a boundary mid-frame, and a
  // momentarily inverted pair would make `len` wrap to 65535.
  uint16_t a = P.bounds[i], b = P.bounds[i + 1];
  if (b <= a || b > NUM_LEDS) return;
  CRGB* p = frame + a;
  uint16_t len = b - a;
  const SegState& s = P.seg[i];
  CRGB c = liveColor[i];
  uint8_t phase = (t * (s.speed + 1)) >> 9;

  switch (s.effect) {
    case SOLID:   fill_solid(p, len, c); break;
    case RAINBOW: fill_rainbow(p, len, phase, 255 / len + 1); break;
    case BREATHE: {
      CRGB d = c;
      d.nscale8_video(beatsin8(s.speed / 8 + 4, 24, 255));
      fill_solid(p, len, d);
      break;
    }
    case CHASE: {
      fadeToBlackBy(p, len, 40);
      p[((t * (s.speed + 1)) >> 10) % len] = c;
      break;
    }
  }
  // Per-segment dimmer last, so it applies to every effect uniformly and the
  // on/off transition is a smooth fade rather than a snap.
  if (liveLevel[i] < 255)
    for (uint16_t j = 0; j < len; j++) p[j].nscale8(liveLevel[i]);
}

void renderLocate() {
  for (int i = 0; i < NUM_LEDS; i++) frame[i].nscale8(24);
  auto paint = [](int8_t seg, CRGB c) {
    if (seg < 0 || seg >= NUM_SEGS) return;
    uint16_t a = P.bounds[seg], b = P.bounds[seg + 1];
    if (b > a && b <= NUM_LEDS) fill_solid(frame + a, b - a, c);
  };
  if (locateSeg >= 0) paint(locateSeg, CRGB::White);
  if (locateBound >= 0) {           // the two segments that meet at this boundary
    paint(locateBound - 1, CRGB::Blue);
    paint(locateBound,     CRGB::OrangeRed);
  }
}

void renderFrame() {
  uint32_t t = millis();

  // Advance the crossfade, then render from the blended values.
  uint32_t elapsed = t - fadeStart;
  uint8_t mix = elapsed >= fadeDur ? 255 : (uint8_t)((elapsed * 255) / fadeDur);
  for (uint8_t i = 0; i < NUM_SEGS; i++) {
    CRGB target = P.seg[i].color;
    liveColor[i] = fadeFromColor[i];
    nblend(liveColor[i], target, mix);
    uint8_t targetLevel = P.seg[i].on ? P.seg[i].level : 0;
    liveLevel[i] = lerp8by8(fadeFromLevel[i], targetLevel, mix);
  }
  liveBright = lerp8by8(fadeFromBright, P.brightness, mix);

  for (uint8_t i = 0; i < NUM_SEGS; i++) renderSegment(i, t);
  if (t < locateUntil) renderLocate();
  // Logical -> physical. "Flipped" means the layout's LED 0 is at the far end
  // of the strip, i.e. the data connection is at what we call the end.
  for (int i = 0; i < NUM_LEDS; i++) leds[i] = P.flipped ? frame[NUM_LEDS - 1 - i] : frame[i];
}

// ---------- 6. HTTP API ----------
AsyncWebServer server(80);

const char* resetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "power-on";
    case ESP_RST_SW:        return "software";
    case ESP_RST_PANIC:     return "panic (firmware crash)";
    case ESP_RST_INT_WDT:   return "interrupt watchdog";
    case ESP_RST_TASK_WDT:  return "task watchdog";
    case ESP_RST_WDT:       return "other watchdog";
    case ESP_RST_BROWNOUT:  return "brownout (voltage dipped)";
    default:                return "unknown";
  }
}

String stateJson() {
  String j; j.reserve(2560);
  char buf[224];

  j += "{\"brightness\":" + String(P.brightness);
  j += ",\"flipped\":" + String(P.flipped ? "true" : "false");
  j += ",\"autoMode\":" + String(P.autoMode ? "true" : "false");
  j += ",\"fadeMin\":" + String(P.fadeMin);
  j += ",\"numLeds\":" + String(NUM_LEDS);
  j += ",\"clock\":\"" + clockString() + "\"";
  j += ",\"timeValid\":" + String(timeValid ? "true" : "false");
  j += ",\"activeScene\":" + String(activeScene);

  int nx = nextSchedIdx();
  j += ",\"nextIdx\":" + String(nx);

  j += ",\"bounds\":[";
  for (uint8_t i = 0; i <= NUM_SEGS; i++) j += (i ? "," : "") + String(P.bounds[i]);

  j += "],\"segments\":[";
  for (uint8_t i = 0; i < NUM_SEGS; i++) {
    const SegState& s = P.seg[i];
    snprintf(buf, sizeof buf,
      "%s{\"id\":%u,\"section\":\"%s\",\"side\":\"%s\",\"on\":%s,\"effect\":\"%s\",\"speed\":%u,"
      "\"color\":\"%02x%02x%02x\",\"level\":%u}",
      i ? "," : "", i, SEG_DEFS[i].section, SEG_DEFS[i].side, s.on ? "true" : "false",
      EFFECT_NAMES[s.effect], s.speed, s.color.r, s.color.g, s.color.b, s.level);
    j += buf;
  }

  j += "],\"scenes\":[";
  for (uint8_t i = 0; i < NUM_SCENES; i++) {
    const Scene& s = SCENES[i];
    CRGB sw = s.kelvin ? kelvinToRGB(s.kelvin) : CRGB(s.rgb);
    snprintf(buf, sizeof buf,
      "%s{\"i\":%u,\"id\":\"%s\",\"name\":\"%s\",\"hint\":\"%s\",\"k\":%u,\"bri\":%u,\"sw\":\"%02x%02x%02x\"}",
      i ? "," : "", i, s.id, s.name, s.hint, s.kelvin, s.brightness, sw.r, sw.g, sw.b);
    j += buf;
  }

  j += "],\"schedule\":[";
  for (uint8_t i = 0; i < MAX_SCHED; i++) {
    snprintf(buf, sizeof buf, "%s{\"i\":%u,\"h\":%u,\"m\":%u,\"scene\":%u,\"on\":%s}",
      i ? "," : "", i, P.sched[i].minutes / 60, P.sched[i].minutes % 60,
      P.sched[i].scene, P.sched[i].enabled ? "true" : "false");
    j += buf;
  }

  j += "],\"uptime_s\":" + String(millis() / 1000);
  j += ",\"frames\":" + String(frames);
  j += ",\"last_reset\":\"" + String(resetReason()) + "\"}";
  return j;
}

// Does segment i match a target string?  "all" | "sec:Pegboard" | "seg:3"
bool inTarget(const String& target, uint8_t i) {
  if (target == "all") return true;
  if (target.startsWith("sec:")) return target.substring(4) == SEG_DEFS[i].section;
  if (target.startsWith("seg:")) return target.substring(4).toInt() == (int)i;
  return false;
}

// POST /api/set?target=sec:Pegboard&on=1&color=ff8800&effect=chase&speed=100
// Any subset of fields. brightness=, flip= and fade= are global.
void handleSet(AsyncWebServerRequest* req) {
  String target = req->hasParam("target") ? req->getParam("target")->value() : "all";
  startFade(400);   // manual changes ease in quickly

  if (req->hasParam("brightness"))
    P.brightness = constrain(req->getParam("brightness")->value().toInt(), 0, 255);
  if (req->hasParam("flip")) P.flipped = req->getParam("flip")->value().toInt() != 0;
  if (req->hasParam("fade"))
    P.fadeMin = constrain(req->getParam("fade")->value().toInt(), 0, 120);

  bool touchedLook = false;
  for (uint8_t i = 0; i < NUM_SEGS; i++) {
    if (!inTarget(target, i)) continue;
    SegState& s = P.seg[i];
    if (req->hasParam("on"))    { s.on = req->getParam("on")->value().toInt() != 0; touchedLook = true; }
    if (req->hasParam("speed")) { s.speed = constrain(req->getParam("speed")->value().toInt(), 0, 255); touchedLook = true; }
    if (req->hasParam("level")) { s.level = constrain(req->getParam("level")->value().toInt(), 0, 255); touchedLook = true; }
    if (req->hasParam("color")) {
      s.color = CRGB(strtoul(req->getParam("color")->value().c_str(), nullptr, 16));
      s.level = 255;   // a hand-picked colour shouldn't stay dimmed by a scene
      touchedLook = true;
    }
    if (req->hasParam("effect")) {
      String name = req->getParam("effect")->value();
      for (uint8_t e = 0; e < EFFECT_COUNT; e++) if (name == EFFECT_NAMES[e]) s.effect = e;
      touchedLook = true;
    }
  }
  // Hand-editing the look means we're no longer showing a named scene, but the
  // schedule stays armed — the next slot will take over on its own.
  if (touchedLook) activeScene = -1;
  markDirty();
  req->send(200, "application/json", stateJson());
}

// POST /api/scene?i=2   — apply a scene by index, right now
void handleScene(AsyncWebServerRequest* req) {
  if (!req->hasParam("i")) { req->send(400, "text/plain", "need i"); return; }
  int i = req->getParam("i")->value().toInt();
  if (i < 0 || i >= NUM_SCENES) { req->send(400, "text/plain", "bad i"); return; }
  applyScene(i, 800);
  req->send(200, "application/json", stateJson());
}

// POST /api/auto?on=1
void handleAuto(AsyncWebServerRequest* req) {
  if (req->hasParam("on")) P.autoMode = req->getParam("on")->value().toInt() != 0;
  if (P.autoMode) {
    activeSched = -1;       // force the schedule to re-apply on the next tick
  }
  markDirty();
  req->send(200, "application/json", stateJson());
}

// POST /api/schedule?i=3&h=19&m=0&scene=6&on=1
void handleSchedule(AsyncWebServerRequest* req) {
  if (!req->hasParam("i")) { req->send(400, "text/plain", "need i"); return; }
  int i = req->getParam("i")->value().toInt();
  if (i < 0 || i >= MAX_SCHED) { req->send(400, "text/plain", "bad i"); return; }
  SchedEntry& e = P.sched[i];
  int h = req->hasParam("h") ? req->getParam("h")->value().toInt() : e.minutes / 60;
  int m = req->hasParam("m") ? req->getParam("m")->value().toInt() : e.minutes % 60;
  e.minutes = constrain(h, 0, 23) * 60 + constrain(m, 0, 59);
  if (req->hasParam("scene")) e.scene = constrain(req->getParam("scene")->value().toInt(), 0, NUM_SCENES - 1);
  if (req->hasParam("on"))    e.enabled = req->getParam("on")->value().toInt() != 0;
  activeSched = -1;           // re-evaluate immediately
  markDirty();
  req->send(200, "application/json", stateJson());
}

// POST /api/bounds?i=3&led=82   — move inner boundary i (1..NUM_SEGS-1)
void handleBounds(AsyncWebServerRequest* req) {
  if (!req->hasParam("i") || !req->hasParam("led")) { req->send(400, "text/plain", "need i and led"); return; }
  int i = req->getParam("i")->value().toInt();
  if (i < 1 || i >= NUM_SEGS) { req->send(400, "text/plain", "bad i"); return; }
  P.bounds[i] = constrain(req->getParam("led")->value().toInt(), P.bounds[i - 1], P.bounds[i + 1]);
  locateBound = i; locateSeg = -1; locateUntil = millis() + 3000;
  markDirty();
  req->send(200, "application/json", stateJson());
}

// POST /api/locate?seg=3   or   /api/locate?bound=2
void handleLocate(AsyncWebServerRequest* req) {
  locateSeg   = req->hasParam("seg")   ? req->getParam("seg")->value().toInt()   : -1;
  locateBound = req->hasParam("bound") ? req->getParam("bound")->value().toInt() : -1;
  locateUntil = millis() + 3000;
  req->send(200, "application/json", stateJson());
}

void logRequest(AsyncWebServerRequest* req) {
  Serial.printf("[%6lu] %s %s  heap=%u\n", millis() / 1000,
                req->methodToString(), req->url().c_str(), ESP.getFreeHeap());
}

void setupServer() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) { req->send(200, "text/html", INDEX_HTML); });
  server.on("/api/state",    HTTP_GET,  [](AsyncWebServerRequest* req) { req->send(200, "application/json", stateJson()); });
  server.on("/api/set",      HTTP_POST, [](AsyncWebServerRequest* r) { logRequest(r); handleSet(r); });
  server.on("/api/scene",    HTTP_POST, [](AsyncWebServerRequest* r) { logRequest(r); handleScene(r); });
  server.on("/api/auto",     HTTP_POST, [](AsyncWebServerRequest* r) { logRequest(r); handleAuto(r); });
  server.on("/api/schedule", HTTP_POST, [](AsyncWebServerRequest* r) { logRequest(r); handleSchedule(r); });
  server.on("/api/bounds",   HTTP_POST, [](AsyncWebServerRequest* r) { logRequest(r); handleBounds(r); });
  server.on("/api/locate",   HTTP_POST, [](AsyncWebServerRequest* r) { logRequest(r); handleLocate(r); });
  server.onNotFound([](AsyncWebServerRequest* req) { req->send(404, "text/plain", "not found"); });
  server.begin();
}

// ---------- 7. boot + loop ----------
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.printf("\nLED strip controller — %d LEDs on GPIO %d, %d segments, %d scenes\n",
                NUM_LEDS, LED_PIN, NUM_SEGS, NUM_SCENES);
  Serial.printf("Last reset: %s\n", resetReason());

  loadPersisted();

  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.addLeds<WS2812B, ONBOARD_PIN, GRB>(onboard, 1);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, MAX_MILLIAMPS);
  FastLED.setBrightness(P.brightness);

  // Start the fade machinery from black so boot is a gentle ramp-up.
  for (uint8_t i = 0; i < NUM_SEGS; i++) { fadeFromColor[i] = CRGB::Black; fadeFromLevel[i] = 0; liveColor[i] = CRGB::Black; liveLevel[i] = 0; }
  fadeFromBright = 0; liveBright = 0;
  startFade(2000);

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setSleep(false);   // power-save mode makes the web server laggy/flaky
  if (strlen(STATIC_IP)) {
    IPAddress ip, gw, sn;
    ip.fromString(STATIC_IP); gw.fromString(GATEWAY); sn.fromString(SUBNET);
    WiFi.config(ip, gw, sn, gw);
  }
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("Connecting to %s", WIFI_SSID);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    renderFrame(); FastLED.show();
    if ((millis() / 500) % 2 == 0) Serial.print(".");
    delay(16);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\nConnected. Open http://%s\n", WiFi.localIP().toString().c_str());
    if (!MDNS.begin(HOSTNAME)) Serial.println("mDNS failed (IP still works)");
    // Local time, so the schedule means what the wall clock means.
    configTzTime(TZ_POSIX, "pool.ntp.org", "time.nist.gov");
    ArduinoOTA.setHostname(HOSTNAME);
    ArduinoOTA.onStart([]() {
      esp_task_wdt_delete(NULL);   // the upload blocks loop() longer than the watchdog allows
      fill_solid(leds, NUM_LEDS, CRGB::Black); FastLED.show();
    });
    ArduinoOTA.begin();
  } else {
    Serial.printf("\nWiFi.status() = %d (1=no SSID found, 4=bad password, 6=disconnected)\n", WiFi.status());
    WiFi.mode(WIFI_AP);
    WiFi.softAP("LEDStrip-Setup");
    Serial.printf("WiFi failed. Join 'LEDStrip-Setup' and open http://%s\n", WiFi.softAPIP().toString().c_str());
  }
  setupServer();

  // If loop() ever stalls for 8 s, reboot instead of freezing the strip.
  esp_task_wdt_init(8, true);
  esp_task_wdt_add(NULL);
}

void loop() {
  esp_task_wdt_reset();
  ArduinoOTA.handle();

  // Schedule tick: once a second is plenty for minute resolution.
  EVERY_N_SECONDS(1) {
    if (!timeValid && minutesNow() >= 0) {
      timeValid = true;
      Serial.printf("[%6lu] clock set: %s\n", millis() / 1000, clockString().c_str());
    }
    if (P.autoMode && timeValid) {
      int idx = currentSchedIdx();
      if (idx >= 0 && idx != activeSched) {
        // On the very first evaluation after boot, snap into the right scene
        // quickly; later transitions get the slow, unnoticeable crossfade.
        uint32_t ms = (activeSched < 0) ? 3000UL : (uint32_t)P.fadeMin * 60UL * 1000UL;
        activeSched = idx;
        Serial.printf("[%6lu] %s -> scene '%s' over %lus\n", millis() / 1000,
                      clockString().c_str(), SCENES[P.sched[idx].scene].id, (unsigned long)(ms / 1000));
        applyScene(P.sched[idx].scene, ms);
      }
    }
  }

  EVERY_N_SECONDS(30) {
    Serial.printf("[%6lu] alive %s  heap=%u  wifi=%d  scene=%d\n", millis() / 1000,
                  clockString().c_str(), ESP.getFreeHeap(), WiFi.status(), activeScene);
  }

  renderFrame();
  FastLED.setBrightness(liveBright);
  onboard[0] = leds[0];
  FastLED.show();
  frames++;

  // Save to flash 1.5 s after the last change, not on every slider tick —
  // flash has a finite number of write cycles.
  if (dirtyAt && millis() - dirtyAt > 1500) savePersisted();
  delay(16);
}
