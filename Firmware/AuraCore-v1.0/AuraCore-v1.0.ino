#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <FastLED.h>
#include <Preferences.h>

// --- GPIO Mapping (S3-WROOM-1) ---
#define GPIO_DRL_IN     4   
#define GPIO_BRAKE_IN   5   
#define GPIO_REVERSE_IN 6   
#define GPIO_TURN_L_IN  7   
#define GPIO_TURN_R_IN  11  
#define GPIO_DATA_1     9   
#define GPIO_DATA_2     10  

#define NUM_LEDS        20 

CRGB leds1[NUM_LEDS];
CRGB leds2[NUM_LEDS];

// --- Persistent Settings ---
Preferences prefs;
struct Config {
  CRGB drlColor;
  CRGB turnColor;
  uint8_t brightness;
  bool invert;
  bool drlRainbow;
  bool turnRainbow;
  uint16_t wipeSpeed;
  uint32_t sleepTimeout;  
  uint32_t hardKillTimer; 
} settings;

unsigned long turnStartL = 0, turnStartR = 0;
unsigned long lastActivity = 0;
bool isAwake = false;
bool showroomActive = false;

AsyncWebServer server(80);

// --- Helper: Convert CRGB to Clean HTML Hex String ---
String colorToHex(CRGB c) {
  char hex[8];
  sprintf(hex, "%02X%02X%02X", c.r, c.g, c.b);
  return String(hex);
}

// --- Web UI Content ---
String getHTML() {
  String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<style>:root{--accent:#007aff;--bg:#0e0e0e;--card:#1a1a1a;--rbow:linear-gradient(90deg,#f00,#ff8000,#ff0,#0f0,#00f,#8b00ff);}";
  html += "body{background:var(--bg);color:#eee;text-align:center;font-family:sans-serif;padding:15px;margin:0;transition:0.5s;}";
  html += ".card{background:var(--card);border-radius:18px;padding:20px;margin-bottom:15px;border:1px solid #2a2a2a;display:flex;flex-direction:column;align-items:center;}";
  html += "h1{color:var(--accent);letter-spacing:2px;margin:25px 0 5px 0;} .sub{color:#555;font-size:0.7rem;text-transform:uppercase;margin-bottom:25px;}";
  html += ".tabs{display:flex;background:#222;border-radius:10px;padding:3px;width:100%;margin:15px 0;border:1px solid #333;}";
  html += ".tab{flex:1;padding:10px;border-radius:8px;font-size:0.7rem;cursor:pointer;color:#666;font-weight:bold;}";
  html += ".active-tab{background:#333;color:var(--accent);box-shadow:0 2px 8px rgba(0,0,0,0.4);}";
  html += "input[type='color']{width:100%;height:45px;border:none;border-radius:10px;background:#222;cursor:pointer;padding:4px;box-sizing:border-box;}";
  html += "input[type='range']{width:100%;margin:15px 0;accent-color:var(--accent);}";
  html += "input[type='number']{width:75px;text-align:center;background:#222;border:1px solid #444;color:white;padding:10px;border-radius:8px;font-weight:bold;}";
  html += "button{width:100%;padding:16px;margin:10px 0;border-radius:12px;border:none;background:#2a2a2a;color:white;font-weight:bold;cursor:pointer;}";
  html += ".btn-sr{background:var(--accent);} .rbow-st{padding:15px;background:var(--rbow);border-radius:10px;margin-top:10px;font-size:0.75rem;font-weight:bold;text-shadow:1px 1px 2px #000;color:white;}";
  html += "body.sr-on{background:#000;} body.sr-on h1{color:#ff0055;text-shadow:0 0 15px #ff0055;}</style></head><body>";
  
  html += "<h1>AURA CORE</h1><div class='sub'>Universal Logic Controller</div>";

  // Lighting Config
  html += "<div class='card'><span style='color:var(--accent);font-size:0.7rem;font-weight:bold;'>BRIGHTNESS</span>";
  html += "<input type='range' min='10' max='255' value='" + String(settings.brightness) + "' onchange=\"fetch('/bright?val='+this.value)\"></div>";

  // DRL Section
  html += "<div class='card'><span style='font-size:0.7rem;font-weight:bold;'>DRL CONFIG</span>";
  html += "<div class='tabs'><div id='tDS' onclick=\"sM('d','solid')\" class='tab " + String(!settings.drlRainbow?"active-tab":"") + "'>SOLID</div>";
  html += "<div id='tDR' onclick=\"sM('d','rainbow')\" class='tab " + String(settings.drlRainbow?"active-tab":"") + "'>RAINBOW</div></div>";
  html += "<div id='dSC' style='width:100%;display:" + String(!settings.drlRainbow?"block":"none") + "'><input type='color' value='#" + colorToHex(settings.drlColor) + "' onchange=\"fetch('/color?val='+this.value.replace('#',''))\"></div>";
  html += "<div id='dRC' style='width:100%;display:" + String(settings.drlRainbow?"block":"none") + "'><div class='rbow-st'>RAINBOW ACTIVE</div></div></div>";

  // Turn Section
  html += "<div class='card'><span style='font-size:0.7rem;font-weight:bold;'>TURN SIGNAL CONFIG</span>";
  html += "<div class='tabs'><div id='tTS' onclick=\"sM('t','solid')\" class='tab " + String(!settings.turnRainbow?"active-tab":"") + "'>SOLID</div>";
  html += "<div id='tTR' onclick=\"sM('t','rainbow')\" class='tab " + String(settings.turnRainbow?"active-tab":"") + "'>RAINBOW</div></div>";
  html += "<div id='tSC' style='width:100%;display:" + String(!settings.turnRainbow?"block":"none") + "'><input type='color' value='#" + colorToHex(settings.turnColor) + "' onchange=\"fetch('/tcolor?val='+this.value.replace('#',''))\"></div>";
  html += "<div id='tRC' style='width:100%;display:" + String(settings.turnRainbow?"block":"none") + "'><div class='rbow-st'>SPECTRAL WIPE ACTIVE</div></div></div>";

  // Protection & Invert
  html += "<div class='card'><span style='font-size:0.7rem;font-weight:bold;'>PROTECTION & UTILITY</span>";
  html += "<div style='display:flex;justify-content:space-around;width:100%;margin:15px 0;'>";
  html += "<div><input type='number' value='" + String(settings.sleepTimeout/60000) + "' onchange=\"fetch('/c_s?val='+this.value)\"><span style='display:block;font-size:0.5rem;color:#555;'>SLEEP(M)</span></div>";
  html += "<div><input type='number' value='" + String(settings.hardKillTimer/3600000) + "' onchange=\"fetch('/c_k?val='+this.value)\"><span style='display:block;font-size:0.5rem;color:#555;'>KILL(H)</span></div></div>";
  html += "<button onclick=\"fetch('/invert')\" style='font-size:0.7rem;background:none;border:1px solid #333;color:#666;'>INVERT TURN SIGNALS (L/R)</button></div>";

  // Showroom
  html += "<div class='card'><button class='btn-sr' id='sb' onclick=\"tS()\">ACTIVATE SHOWROOM</button></div>";

  html += "<script>function fetch(u){var x=new XMLHttpRequest();x.open('GET',u,true);x.send();} function sM(t,m){let isR=(m=='rainbow'); if(t=='d'){";
  html += "document.getElementById('dSC').style.display=isR?'none':'block'; document.getElementById('dRC').style.display=isR?'block':'none';";
  html += "document.getElementById('tDS').className=isR?'tab':'tab active-tab'; document.getElementById('tDR').className=isR?'tab active-tab':'tab'; fetch('/mode_d?val='+m);";
  html += "}else{document.getElementById('tSC').style.display=isR?'none':'block'; document.getElementById('tRC').style.display=isR?'block':'none';";
  html += "document.getElementById('tTS').className=isR?'tab':'tab active-tab'; document.getElementById('tTR').className=isR?'tab active-tab':'tab'; fetch('/mode_t?val='+m);}}";
  html += "function tS(){let b=document.getElementById('sb'); if(b.innerText=='ACTIVATE SHOWROOM'){b.innerText='DEACTIVATE'; b.style.background='#ff0055'; document.body.classList.add('sr-on'); fetch('/showroom?state=on');}else{b.innerText='ACTIVATE SHOWROOM'; b.style.background='#007aff'; document.body.classList.remove('sr-on'); fetch('/showroom?state=off');}}</script></body></html>";
  return html;
}

void saveConfig() {
  prefs.begin("aura_core", false);
  prefs.putUInt("drl", (uint32_t)settings.drlColor);
  prefs.putUInt("turn", (uint32_t)settings.turnColor);
  prefs.putUChar("bright", settings.brightness);
  prefs.putBool("inv", settings.invert);
  prefs.putBool("dr_r", settings.drlRainbow);
  prefs.putBool("tr_r", settings.turnRainbow);
  prefs.putUInt("spd", settings.wipeSpeed);
  prefs.putUInt("s_t", settings.sleepTimeout);
  prefs.putUInt("h_k", settings.hardKillTimer);
  prefs.end();
}

void loadConfig() {
  prefs.begin("aura_core", true);
  uint32_t savedDrl = prefs.getUInt("drl", 0xE10000);
  uint32_t savedTurn = prefs.getUInt("turn", 0xFFA500);
  settings.drlColor = CRGB(savedDrl);
  settings.turnColor = CRGB(savedTurn);
  settings.brightness = prefs.getUChar("bright", 200);
  settings.invert = prefs.getBool("inv", false);
  settings.drlRainbow = prefs.getBool("dr_r", false);
  settings.turnRainbow = prefs.getBool("tr_r", false);
  settings.wipeSpeed = prefs.getUInt("spd", 35);
  settings.sleepTimeout = prefs.getUInt("s_t", 600000);
  settings.hardKillTimer = prefs.getUInt("h_k", 14400000);
  prefs.end();
}

void runWelcomeSequence(CRGB* l, CRGB* r) {
  for(int i = 0; i < NUM_LEDS; i++) {
    CRGB color = settings.drlRainbow ? CHSV((i * 256 / NUM_LEDS), 255, 255) : settings.drlColor;
    l[i] = r[i] = color; FastLED.show(); delay(30);
  }
}

void handleSideLogic(int turnGpio, CRGB* strip, unsigned long &startTime) {
  bool turnActive = digitalRead(turnGpio);
  if (turnActive) {
    if (startTime == 0) startTime = millis();
    int progress = (millis() - startTime) / settings.wipeSpeed;
    fadeToBlackBy(strip, NUM_LEDS, 110);
    for(int i = 0; i < min(progress, NUM_LEDS); i++) {
      strip[i] = settings.turnRainbow ? CHSV((millis()/10)+(i*15), 255, 255) : settings.turnColor;
    }
  } else {
    if (startTime != 0) {
      uint16_t duration = millis() - startTime;
      if (duration > 150) { settings.wipeSpeed = duration / NUM_LEDS; saveConfig(); }
      startTime = 0;
    }
    if (digitalRead(GPIO_BRAKE_IN)) fill_solid(strip, NUM_LEDS, CRGB::Red);
    else if (digitalRead(GPIO_REVERSE_IN)) fill_solid(strip, NUM_LEDS, CRGB::White);
    else if (digitalRead(GPIO_DRL_IN)) {
      if (settings.drlRainbow) fill_rainbow(strip, NUM_LEDS, millis()/20, 255/NUM_LEDS);
      else fill_solid(strip, NUM_LEDS, settings.drlColor);
    } else fill_solid(strip, NUM_LEDS, CRGB::Black);
  }
}

void setup() {
  loadConfig();
  pinMode(GPIO_DRL_IN, INPUT); pinMode(GPIO_BRAKE_IN, INPUT); pinMode(GPIO_REVERSE_IN, INPUT);
  pinMode(GPIO_TURN_L_IN, INPUT); pinMode(GPIO_TURN_R_IN, INPUT);

  FastLED.addLeds<WS2812B, GPIO_DATA_1, GRB>(leds1, NUM_LEDS);
  FastLED.addLeds<WS2812B, GPIO_DATA_2, GRB>(leds2, NUM_LEDS);
  FastLED.setBrightness(settings.brightness);

  WiFi.softAP(("AURA_" + WiFi.macAddress().substring(12)).c_str(), "aura1234");

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *r){ r->send(200, "text/html", getHTML()); });
  server.on("/bright", HTTP_GET, [](AsyncWebServerRequest *r){ if(r->hasParam("val")){settings.brightness=r->getParam("val")->value().toInt(); FastLED.setBrightness(settings.brightness); saveConfig();} r->send(200); });
  server.on("/color", HTTP_GET, [](AsyncWebServerRequest *r){ if(r->hasParam("val")){settings.drlColor=strtol(r->getParam("val")->value().c_str(),NULL,16); saveConfig();} r->send(200); });
  server.on("/tcolor", HTTP_GET, [](AsyncWebServerRequest *r){ if(r->hasParam("val")){settings.turnColor=strtol(r->getParam("val")->value().c_str(),NULL,16); saveConfig();} r->send(200); });
  server.on("/mode_d", HTTP_GET, [](AsyncWebServerRequest *r){ if(r->hasParam("val")){settings.drlRainbow=(r->getParam("val")->value()=="rainbow"); saveConfig();} r->send(200); });
  server.on("/mode_t", HTTP_GET, [](AsyncWebServerRequest *r){ if(r->hasParam("val")){settings.turnRainbow=(r->getParam("val")->value()=="rainbow"); saveConfig();} r->send(200); });
  server.on("/c_s", HTTP_GET, [](AsyncWebServerRequest *r){ if(r->hasParam("val")){settings.sleepTimeout=r->getParam("val")->value().toInt()*60000; saveConfig();} r->send(200); });
  server.on("/c_k", HTTP_GET, [](AsyncWebServerRequest *r){ if(r->hasParam("val")){settings.hardKillTimer=r->getParam("val")->value().toInt()*3600000; saveConfig();} r->send(200); });
  server.on("/invert", HTTP_GET, [](AsyncWebServerRequest *r){ settings.invert=!settings.invert; saveConfig(); r->send(200); });
  server.on("/showroom", HTTP_GET, [](AsyncWebServerRequest *r){ if(r->hasParam("state")){showroomActive=(r->getParam("state")->value()=="on"); lastActivity=millis();} r->send(200); });

  server.begin();
}

void loop() {
  bool anyIn = digitalRead(GPIO_DRL_IN) || digitalRead(GPIO_BRAKE_IN) || digitalRead(GPIO_TURN_L_IN) || digitalRead(GPIO_TURN_R_IN);
  if (anyIn) {
    if (!isAwake) { if(digitalRead(GPIO_DRL_IN)) runWelcomeSequence(leds1, leds2); isAwake=true; WiFi.setSleep(false); }
    lastActivity = millis();
  }

  unsigned long idle = millis() - lastActivity;
  if (!anyIn) {
    if (showroomActive && idle > settings.hardKillTimer) showroomActive = false;
    if (isAwake && !showroomActive && idle > settings.sleepTimeout) { isAwake=false; WiFi.setSleep(true); }
  }

  if (isAwake || showroomActive) {
    if (showroomActive && !anyIn) {
      fill_rainbow(leds1, NUM_LEDS, millis()/20, 255/NUM_LEDS); fill_rainbow(leds2, NUM_LEDS, millis()/20, 255/NUM_LEDS);
    } else {
      handleSideLogic(GPIO_TURN_L_IN, settings.invert?leds2:leds1, turnStartL);
      handleSideLogic(GPIO_TURN_R_IN, settings.invert?leds1:leds2, turnStartR);
    }
  } else { fill_solid(leds1, NUM_LEDS, CRGB::Black); fill_solid(leds2, NUM_LEDS, CRGB::Black); }
  FastLED.show();
}