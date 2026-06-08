#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <FastLED.h>
#include <Preferences.h>
#include <ArduinoJson.h>   // Add "ArduinoJson" by Benoit Blanchon in Library Manager
#include <Update.h>

#define MAX_LEDS         150
#define GPIO_DRL_IN      4
#define GPIO_BRAKE_IN    5
#define GPIO_TURN_L_IN   7
#define GPIO_TURN_R_IN   11
#define GPIO_REVERSE_IN  12
#define GPIO_DATA_1      9
#define GPIO_DATA_2      10

CRGB leds1[MAX_LEDS];
CRGB leds2[MAX_LEDS];

Preferences prefs;

struct Config {
  // Per-side brake colors
  CRGB brakeColorL;
  CRGB brakeColorR;
  // Per-side DRL colors
  CRGB drlColorL;
  CRGB drlColorR;
  // Per-side turn colors
  CRGB turnColorL;
  CRGB turnColorR;
  uint8_t  brightness;
  bool     invert;
  // DRL mode: 0=solid, 1=rainbow, 2=breathe, 3=chase
  uint8_t  drlMode;
  // Turn mode: 0=solid, 1=rainbow/spectral, 2=chase
  uint8_t  turnMode;
  // Brake mode: 0=solid, 1=F1 strobe, 2=2-tap, 3=pulse, 4=fade-in
  uint8_t  brakeMode;
  // Reverse mode: 0=solid white, 1=fade-in white
  // Color is always white â€” upgrade to SK6812 RGBW for best white quality
  uint8_t  reverseMode;
  uint16_t wipeSpeed;
  int8_t   fineTune;
  uint16_t numLeds;
} settings;

// Per-side turn animation state machine
struct TurnAnim {
  bool          wasOn     = false;
  int           progress  = 0;       // LEDs lit so far (0..numLeds)
  unsigned long lastStep  = 0;       // time of last LED increment
  unsigned long syncStart = 0;       // for re-learn timing
  bool          holding   = false;   // wipe complete, holding until signal drops
};
TurnAnim animL, animR;

unsigned long brakeStartTime   = 0;
unsigned long lastBrakeRelease = 0;
unsigned long reverseStartTime = 0;
bool wasReversing   = false;
bool isBraking      = false;
bool showroomActive = false;
bool isSyncing      = false;

AsyncWebServer server(80);

// â”€â”€â”€ Helpers â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

String colorToHex(CRGB c) {
  char hex[7];
  sprintf(hex, "%02X%02X%02X", c.r, c.g, c.b);
  return String(hex);
}

CRGB hexToColor(const String& hex) {
  long val = strtol(hex.c_str(), NULL, 16);
  return CRGB((val >> 16) & 0xFF, (val >> 8) & 0xFF, val & 0xFF);
}

// UI is served from LittleFS: Firmware/AuraCore/data/index.html
// Upload via Arduino IDE -> Tools -> ESP32 LittleFS Data Upload

// ─── Save / Load ──────────────────────────────────────────────────────────────

void saveConfig() {
  prefs.begin(“aura_core”, false);
  prefs.putUInt("brkL",  ((uint32_t)settings.brakeColorL.r << 16) | ((uint32_t)settings.brakeColorL.g << 8) | settings.brakeColorL.b);
  prefs.putUInt("brkR",  ((uint32_t)settings.brakeColorR.r << 16) | ((uint32_t)settings.brakeColorR.g << 8) | settings.brakeColorR.b);
  prefs.putUInt("drlL",  ((uint32_t)settings.drlColorL.r  << 16) | ((uint32_t)settings.drlColorL.g  << 8) | settings.drlColorL.b);
  prefs.putUInt("drlR",  ((uint32_t)settings.drlColorR.r  << 16) | ((uint32_t)settings.drlColorR.g  << 8) | settings.drlColorR.b);
  prefs.putUInt("trnL",  ((uint32_t)settings.turnColorL.r << 16) | ((uint32_t)settings.turnColorL.g << 8) | settings.turnColorL.b);
  prefs.putUInt("trnR",  ((uint32_t)settings.turnColorR.r << 16) | ((uint32_t)settings.turnColorR.g << 8) | settings.turnColorR.b);
  prefs.putUChar("brt",  settings.brightness);
  prefs.putBool("inv",   settings.invert);
  prefs.putUChar("drlM", settings.drlMode);
  prefs.putUChar("trnM", settings.turnMode);
  prefs.putUChar("brkM", settings.brakeMode);
  prefs.putUChar("revM", settings.reverseMode);
  prefs.putUInt("spd",   settings.wipeSpeed);
  prefs.putChar("fine",  settings.fineTune);
  prefs.putUInt("nled",  settings.numLeds);
  prefs.end();
}

void loadConfig() {
  prefs.begin("aura_core", true);
  settings.brakeColorL = CRGB((prefs.getUInt("brkL", 0xFF0000) >> 16) & 0xFF, (prefs.getUInt("brkL", 0xFF0000) >> 8) & 0xFF, prefs.getUInt("brkL", 0xFF0000) & 0xFF);
  settings.brakeColorR = CRGB((prefs.getUInt("brkR", 0xFF0000) >> 16) & 0xFF, (prefs.getUInt("brkR", 0xFF0000) >> 8) & 0xFF, prefs.getUInt("brkR", 0xFF0000) & 0xFF);
  settings.drlColorL   = CRGB((prefs.getUInt("drlL", 0xE10000) >> 16) & 0xFF, (prefs.getUInt("drlL", 0xE10000) >> 8) & 0xFF, prefs.getUInt("drlL", 0xE10000) & 0xFF);
  settings.drlColorR   = CRGB((prefs.getUInt("drlR", 0xE10000) >> 16) & 0xFF, (prefs.getUInt("drlR", 0xE10000) >> 8) & 0xFF, prefs.getUInt("drlR", 0xE10000) & 0xFF);
  settings.turnColorL  = CRGB((prefs.getUInt("trnL", 0xFFA500) >> 16) & 0xFF, (prefs.getUInt("trnL", 0xFFA500) >> 8) & 0xFF, prefs.getUInt("trnL", 0xFFA500) & 0xFF);
  settings.turnColorR  = CRGB((prefs.getUInt("trnR", 0xFFA500) >> 16) & 0xFF, (prefs.getUInt("trnR", 0xFFA500) >> 8) & 0xFF, prefs.getUInt("trnR", 0xFFA500) & 0xFF);
  settings.brightness  = prefs.getUChar("brt",  200);
  settings.invert      = prefs.getBool("inv",   false);
  settings.drlMode     = prefs.getUChar("drlM", 0);
  settings.turnMode    = prefs.getUChar("trnM", 0);
  settings.brakeMode   = prefs.getUChar("brkM",  0);
  settings.reverseMode = prefs.getUChar("revM",  0);
  settings.wipeSpeed   = prefs.getUInt("spd",   35);
  settings.fineTune    = prefs.getChar("fine",  0);
  settings.numLeds     = prefs.getUInt("nled",  20);
  prefs.end();
}

// â”€â”€â”€ LED Logic â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

void applyDRL(CRGB* strip, CRGB color, uint8_t mode, unsigned long now) {
  switch (mode) {
    case 0: // Solid
      fill_solid(strip, settings.numLeds, color);
      break;
    case 1: // Rainbow
      fill_rainbow(strip, settings.numLeds, now / 20, 255 / settings.numLeds);
      break;
    case 2: { // Breathe
      uint8_t bri = (sin8(now / 8) / 2) + 128;
      CRGB c = color;
      c.nscale8(bri);
      fill_solid(strip, settings.numLeds, c);
      break;
    }
    case 3: { // Chase
      fill_solid(strip, settings.numLeds, CRGB::Black);
      int pos = (now / 40) % settings.numLeds;
      for (int i = 0; i < 4; i++) strip[(pos + i) % settings.numLeds] = color;
      break;
    }
  }
}

void applyBrake(CRGB* strip, CRGB color, uint8_t mode, unsigned long now) {
  switch (mode) {
    case 0: // Solid
      fill_solid(strip, settings.numLeds, color);
      break;
    case 1: // F1 Strobe (rapid 60ms flash for 600ms then solid)
      if ((now - brakeStartTime) < 600 && ((now - brakeStartTime) / 60) % 2 == 0)
        fill_solid(strip, settings.numLeds, CRGB::Black);
      else fill_solid(strip, settings.numLeds, color);
      break;
    case 2: // 2-Tap
      if ((now - brakeStartTime) < 400 && ((now - brakeStartTime) / 100) % 2 == 0)
        fill_solid(strip, settings.numLeds, CRGB::Black);
      else fill_solid(strip, settings.numLeds, color);
      break;
    case 3: { // Pulse
      uint8_t bri = (sin8((now - brakeStartTime) / 4) / 2) + 128;
      CRGB c = color; c.nscale8(bri);
      fill_solid(strip, settings.numLeds, c);
      break;
    }
    case 4: { // Fade-in
      uint8_t bri = min((uint32_t)255, (now - brakeStartTime) * 255 / 300);
      CRGB c = color; c.nscale8(bri);
      fill_solid(strip, settings.numLeds, c);
      break;
    }
  }
}

// Returns a solid base color for the turn-wipe background.
// Priority: reverse > brake (solid) > DRL (solid) > black
CRGB getBase(bool revOn, bool brkOn, bool drlOn, CRGB brakeC, CRGB drlC) {
  if (revOn) return CRGB::White;
  if (brkOn) return brakeC;
  if (drlOn) return drlC;
  return CRGB::Black;
}

// Applies the full animated base state to a strip when no turn is active.
// Priority: reverse > brake > DRL > off
void applyBase(CRGB* strip, bool revOn, bool brkOn, bool drlOn,
               CRGB brakeC, CRGB drlC, unsigned long now) {
  if (revOn) {
    if (settings.reverseMode == 1) {
      // Fade-in: ramp to white over 700ms
      uint8_t bri = (uint8_t)min((unsigned long)255, (now - reverseStartTime) * 255UL / 700);
      CRGB w = CRGB::White; w.nscale8(bri);
      fill_solid(strip, settings.numLeds, w);
    } else {
      fill_solid(strip, settings.numLeds, CRGB::White);
    }
  } else if (brkOn) {
    bool lockout = (now - lastBrakeRelease < 3000);
    if (lockout) fill_solid(strip, settings.numLeds, brakeC);
    else applyBrake(strip, brakeC, settings.brakeMode, now);
  } else if (drlOn) {
    applyDRL(strip, drlC, settings.drlMode, now);
  } else {
    fill_solid(strip, settings.numLeds, CRGB::Black);
  }
}

// Steps the turn-signal state machine and renders to the strip.
// When on: wipes turn color over the base color, then holds.
// When off: resets state.
void stepTurn(TurnAnim& anim, bool on, CRGB* strip,
              CRGB turnColor, CRGB baseColor, unsigned long now) {
  bool risingEdge  = on  && !anim.wasOn;
  bool fallingEdge = !on && anim.wasOn;

  if (risingEdge) {
    anim.progress = 0;
    anim.holding  = false;
    anim.lastStep = now;
    if (isSyncing) anim.syncStart = now;
  }

  if (on) {
    // Advance wipe one LED per wipeSpeed ms
    if (!anim.holding) {
      int stepMs = max((int)settings.wipeSpeed + (int)settings.fineTune, 1);
      while ((long)(now - anim.lastStep) >= stepMs && anim.progress < settings.numLeds) {
        anim.progress++;
        anim.lastStep += stepMs;
      }
      if (anim.progress >= settings.numLeds) anim.holding = true;
    }

    // Render: fill base, overlay wipe
    fill_solid(strip, settings.numLeds, baseColor);
    if (settings.turnMode == 2) {
      // Chase: single leading-edge dot
      if (anim.progress > 0) strip[anim.progress - 1] = turnColor;
    } else {
      for (int i = 0; i < anim.progress; i++) {
        strip[i] = (settings.turnMode == 1)
          ? CHSV((uint8_t)(now / 10 + i * 15), 255, 255)
          : turnColor;
      }
    }
  }

  if (fallingEdge) {
    // Re-learn: if syncing, measure one blink duration and set wipeSpeed
    if (isSyncing && anim.syncStart > 0) {
      uint16_t dur = now - anim.syncStart;
      if (dur > 150) { settings.wipeSpeed = dur / settings.numLeds; isSyncing = false; saveConfig(); }
      anim.syncStart = 0;
    }
    anim.progress = 0;
    anim.holding  = false;
  }

  anim.wasOn = on;
}

// â”€â”€â”€ Setup â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

void setup() {
  Serial.begin(115200);
  loadConfig();

  pinMode(GPIO_DRL_IN,     INPUT);
  pinMode(GPIO_BRAKE_IN,   INPUT);
  pinMode(GPIO_TURN_L_IN,  INPUT);
  pinMode(GPIO_TURN_R_IN,  INPUT);
  pinMode(GPIO_REVERSE_IN, INPUT);

  FastLED.addLeds<WS2812B, GPIO_DATA_1, GRB>(leds1, MAX_LEDS);
  FastLED.addLeds<WS2812B, GPIO_DATA_2, GRB>(leds2, MAX_LEDS);
  FastLED.setBrightness(settings.brightness);

  if (!LittleFS.begin(true)) Serial.println(“LittleFS mount failed”);

  WiFi.softAP(“AURA_CORE”, “aura1234”);
  Serial.println(“AP IP: “ + WiFi.softAPIP().toString());

  // ── Static files (index.html + any future assets) ──
  server.serveStatic(“/”, LittleFS, “/”).setDefaultFile(“index.html”);

  // ── /config — current settings as JSON (used by UI on page load) ──
  server.on(“/config”, HTTP_GET, [](AsyncWebServerRequest* r) {
    StaticJsonDocument<512> doc;
    doc[“brightness”]  = (int)round(settings.brightness / 255.0 * 100);
    doc[“brakeMode”]   = settings.brakeMode;
    doc[“brakeColorL”] = colorToHex(settings.brakeColorL);
    doc[“brakeColorR”] = colorToHex(settings.brakeColorR);
    doc[“reverseMode”] = settings.reverseMode;
    doc[“drlMode”]     = settings.drlMode;
    doc[“drlColorL”]   = colorToHex(settings.drlColorL);
    doc[“drlColorR”]   = colorToHex(settings.drlColorR);
    doc[“turnMode”]    = settings.turnMode;
    doc[“turnColorL”]  = colorToHex(settings.turnColorL);
    doc[“turnColorR”]  = colorToHex(settings.turnColorR);
    doc[“wipeSpeed”]   = settings.wipeSpeed;
    doc[“fineTune”]    = settings.fineTune;
    doc[“numLeds”]     = settings.numLeds;
    doc[“invert”]      = settings.invert;
    String out; serializeJson(doc, out);
    r->send(200, “application/json”, out);
  });

  // ── /status — heap readout for debug panel ──
  server.on(“/status”, HTTP_GET, [](AsyncWebServerRequest* r) {
    StaticJsonDocument<64> doc;
    doc[“heap”] = ESP.getFreeHeap();
    String out; serializeJson(doc, out);
    r->send(200, “application/json”, out);
  });

  // ── /update — OTA firmware upload ──
  server.on(“/update”, HTTP_POST,
    [](AsyncWebServerRequest* r) {
      bool ok = !Update.hasError();
      r->send(200, “text/plain”, ok ? “OK” : “FAIL”);
      if (ok) { delay(500); ESP.restart(); }
    },
    [](AsyncWebServerRequest* r, const String& filename, size_t index, uint8_t* data, size_t len, bool final) {
      if (!index) {
        Serial.printf(“OTA start: %s\n”, filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
      }
      if (Update.write(data, len) != len) Update.printError(Serial);
      if (final) {
        if (Update.end(true)) Serial.printf(“OTA done: %u bytes\n”, index + len);
        else Update.printError(Serial);
      }
    }
  );

  // Brightness
  server.on("/bright", HTTP_GET, [](AsyncWebServerRequest* r){
    if (r->hasParam("val")) { settings.brightness = r->getParam("val")->value().toInt(); FastLED.setBrightness(settings.brightness); saveConfig(); }
    r->send(200);
  });

  // Brake
  server.on("/bmode",   HTTP_GET, [](AsyncWebServerRequest* r){ if(r->hasParam("val")){ settings.brakeMode=r->getParam("val")->value().toInt(); saveConfig(); } r->send(200); });
  server.on("/bcolor_l",HTTP_GET, [](AsyncWebServerRequest* r){ if(r->hasParam("val")){ settings.brakeColorL=hexToColor(r->getParam("val")->value()); saveConfig(); } r->send(200); });
  server.on("/bcolor_r",HTTP_GET, [](AsyncWebServerRequest* r){ if(r->hasParam("val")){ settings.brakeColorR=hexToColor(r->getParam("val")->value()); saveConfig(); } r->send(200); });

  // DRL
  server.on("/drl_l",   HTTP_GET, [](AsyncWebServerRequest* r){ if(r->hasParam("val")){ settings.drlColorL=hexToColor(r->getParam("val")->value()); saveConfig(); } r->send(200); });
  server.on("/drl_r",   HTTP_GET, [](AsyncWebServerRequest* r){ if(r->hasParam("val")){ settings.drlColorR=hexToColor(r->getParam("val")->value()); saveConfig(); } r->send(200); });
  server.on("/mode_d",  HTTP_GET, [](AsyncWebServerRequest* r){
    if(r->hasParam("val")){
      String v = r->getParam("val")->value();
      if(v=="solid") settings.drlMode=0; else if(v=="rainbow") settings.drlMode=1; else if(v=="breathe") settings.drlMode=2; else settings.drlMode=3;
      saveConfig();
    } r->send(200);
  });

  // Turn
  server.on("/tcolor_l",HTTP_GET, [](AsyncWebServerRequest* r){ if(r->hasParam("val")){ settings.turnColorL=hexToColor(r->getParam("val")->value()); saveConfig(); } r->send(200); });
  server.on("/tcolor_r",HTTP_GET, [](AsyncWebServerRequest* r){ if(r->hasParam("val")){ settings.turnColorR=hexToColor(r->getParam("val")->value()); saveConfig(); } r->send(200); });
  server.on("/mode_t",  HTTP_GET, [](AsyncWebServerRequest* r){
    if(r->hasParam("val")){
      String v = r->getParam("val")->value();
      if(v=="solid") settings.turnMode=0; else if(v=="rainbow") settings.turnMode=1; else settings.turnMode=2;
      saveConfig();
    } r->send(200);
  });
  server.on("/wipe",    HTTP_GET, [](AsyncWebServerRequest* r){ if(r->hasParam("val")){ settings.wipeSpeed=r->getParam("val")->value().toInt(); saveConfig(); } r->send(200); });

  // Misc
  server.on("/setleds", HTTP_GET, [](AsyncWebServerRequest* r){ if(r->hasParam("val")){ settings.numLeds=min((uint16_t)r->getParam("val")->value().toInt(),(uint16_t)MAX_LEDS); saveConfig(); } r->send(200); });
  server.on("/invert",  HTTP_GET, [](AsyncWebServerRequest* r){ settings.invert=!settings.invert; saveConfig(); r->send(200); });
  server.on("/fine",    HTTP_GET, [](AsyncWebServerRequest* r){ if(r->hasParam("val")){ settings.fineTune=r->getParam("val")->value().toInt(); saveConfig(); } r->send(200); });
  server.on("/rmode",   HTTP_GET, [](AsyncWebServerRequest* r){ if(r->hasParam("val")){ settings.reverseMode=r->getParam("val")->value().toInt(); saveConfig(); } r->send(200); });
  server.on("/sync",    HTTP_GET, [](AsyncWebServerRequest* r){ isSyncing=true; r->send(200); });
  server.on("/showroom",HTTP_GET, [](AsyncWebServerRequest* r){ if(r->hasParam("state")){ showroomActive=(r->getParam("state")->value()=="on"); } r->send(200); });
  server.on("/reset",   HTTP_GET, [](AsyncWebServerRequest* r){ prefs.begin("aura_core",false); prefs.clear(); prefs.end(); r->send(200); delay(500); ESP.restart(); });

  server.begin();
}

// â”€â”€â”€ Loop â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

void loop() {
  unsigned long now = millis();

  if (showroomActive) {
    fill_rainbow(leds1, settings.numLeds, now / 20, 10);
    fill_rainbow(leds2, settings.numLeds, now / 20, 10);
    FastLED.show();
    return;
  }

  // Read all signals
  bool drlOn = digitalRead(GPIO_DRL_IN);
  bool brkOn = digitalRead(GPIO_BRAKE_IN);
  bool revOn = digitalRead(GPIO_REVERSE_IN);
  bool lTurn = digitalRead(GPIO_TURN_L_IN);
  bool rTurn = digitalRead(GPIO_TURN_R_IN);

  // Reverse rising-edge tracking (for fade-in start time)
  if (revOn && !wasReversing) reverseStartTime = now;
  wasReversing = revOn;

  // Brake edge tracking (for animated brake modes)
  if (brkOn && !isBraking)  { brakeStartTime = now; isBraking = true; }
  if (!brkOn && isBraking)  { lastBrakeRelease = now; isBraking = false; }

  CRGB* leftStrip  = settings.invert ? leds2 : leds1;
  CRGB* rightStrip = settings.invert ? leds1 : leds2;

  // Base color used behind the turn wipe (solid snapshot of current state)
  CRGB baseL = getBase(revOn, brkOn, drlOn, settings.brakeColorL, settings.drlColorL);
  CRGB baseR = getBase(revOn, brkOn, drlOn, settings.brakeColorR, settings.drlColorR);

  // Left side: turn wipe over base, or full animated base
  stepTurn(animL, lTurn, leftStrip, settings.turnColorL, baseL, now);
  if (!lTurn) applyBase(leftStrip, revOn, brkOn, drlOn, settings.brakeColorL, settings.drlColorL, now);

  // Right side
  stepTurn(animR, rTurn, rightStrip, settings.turnColorR, baseR, now);
  if (!rTurn) applyBase(rightStrip, revOn, brkOn, drlOn, settings.brakeColorR, settings.drlColorR, now);

  FastLED.show();
}
