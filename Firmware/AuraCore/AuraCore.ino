#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <FastLED.h>
#include <Preferences.h>

#define MAX_LEDS        150 
#define GPIO_DRL_IN     4   
#define GPIO_BRAKE_IN   5   
#define GPIO_TURN_L_IN  7   
#define GPIO_TURN_R_IN  11  
#define GPIO_DATA_1     9   
#define GPIO_DATA_2     10  

CRGB leds1[MAX_LEDS];
CRGB leds2[MAX_LEDS];

Preferences prefs;
struct Config {
  CRGB drlColor;
  CRGB turnColor;
  CRGB brakeColor;      // New
  uint8_t brightness;
  bool invert;
  bool drlRainbow;
  bool turnRainbow;
  uint8_t brakeMode;    // New: 0=Solid, 1=F1, 2=2-Tap
  uint16_t wipeSpeed;
  int8_t fineTune; 
  uint16_t numLeds; 
} settings;

unsigned long turnStartL = 0, turnStartR = 0;
unsigned long brakeStartTime = 0;
unsigned long lastBrakeRelease = 0;
bool isBraking = false;
bool showroomActive = false;
bool isSyncing = false; 

AsyncWebServer server(80);

String colorToHex(CRGB c) {
  char hex[8];
  sprintf(hex, "%02X%02X%02X", c.r, c.g, c.b);
  return String(hex);
}

String getHTML() {
  String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no'>";
  html += "<style>:root{--accent:#007aff;--bg:#0e0e0e;--card:#1a1a1a;--rbow:linear-gradient(90deg,#f00,#ff8000,#ff0,#0f0,#00f,#8b00ff);}";
  html += "body{background:var(--bg);color:#eee;text-align:center;font-family:sans-serif;padding:15px;margin:0;display:flex;justify-content:center;}";
  html += ".container{max-width:400px;width:100%;} .card{background:var(--card);border-radius:18px;padding:20px;margin-bottom:15px;border:1px solid #2a2a2a;}";
  html += "h1{color:var(--accent);letter-spacing:2px;margin:25px 0 5px 0;cursor:pointer;user-select:none;} .sub{color:#555;font-size:0.7rem;text-transform:uppercase;margin-bottom:25px;}";
  html += ".tabs{display:flex;background:#222;border-radius:10px;padding:3px;margin:15px 0;border:1px solid #333;}";
  html += ".tab{flex:1;padding:10px;border-radius:8px;font-size:0.6rem;cursor:pointer;color:#666;font-weight:bold;} .active-tab{background:#333;color:var(--accent);}";
  html += "input[type='color'], input[type='number']{width:100%;height:45px;border:none;border-radius:10px;background:#222;color:white;padding:4px;text-align:center;font-weight:bold;}";
  html += "input[type='range']{width:100%;margin:15px 0;accent-color:var(--accent);} button{width:100%;padding:16px;margin:10px 0;border-radius:12px;border:none;background:#2a2a2a;color:white;font-weight:bold;cursor:pointer;}";
  html += ".btn-sr{background:var(--accent);margin-bottom:20px;} .btn-sync{background:#ffa500;color:black;} .btn-rst{background:#331111;color:#ff5555;font-size:0.7rem;margin-top:25px;}";
  html += ".toggle-box{display:flex;justify-content:space-between;align-items:center;background:#222;padding:12px;border-radius:12px;margin:5px 0;} .drop-content{display:none; overflow:hidden;} .val-label{color:var(--accent);float:right;}";
  html += "label{font-size:0.6rem;color:#888;text-transform:uppercase;font-weight:bold;display:block;margin-bottom:5px; text-align:left;}</style></head><body>";
  
  html += "<div class='container'><h1 onclick='dCheck()'>AURA CORE</h1><div class='sub'>Logic Controller v1.5</div>";

  // Brightness
  int brightPct = round((settings.brightness / 255.0) * 100);
  html += "<div class='card'><label>Brightness <span id='bPct' class='val-label'>" + String(brightPct) + "%</span></label>";
  html += "<input type='range' min='0' max='255' value='" + String(settings.brightness) + "' oninput='uB(this.value)' onchange=\"fetch('/bright?val='+this.value)\"></div>";

  // Brake Card
  html += "<div class='card'><label>Brake Style</label><div class='tabs'>";
  html += "<div onclick=\"sB(0)\" id='b0' class='tab "+String(settings.brakeMode==0?"active-tab":"")+"'>SOLID</div>";
  html += "<div onclick=\"sB(1)\" id='b1' class='tab "+String(settings.brakeMode==1?"active-tab":"")+"'>F1</div>";
  html += "<div onclick=\"sB(2)\" id='b2' class='tab "+String(settings.brakeMode==2?"active-tab":"")+"'>2-TAP</div></div>";
  html += "<label>Brake Color</label><input type='color' value='#" + colorToHex(settings.brakeColor) + "' onchange=\"fetch('/bcolor?val='+this.value.replace('#',''))\"></div>";

  // DRL & Turn
  html += "<div class='card'><label>DRL Mode</label><div class='tabs'><div id='tDS' onclick=\"sM('d','solid')\" class='tab " + String(!settings.drlRainbow?"active-tab":"") + "'>SOLID</div><div id='tDR' onclick=\"sM('d','rainbow')\" class='tab " + String(settings.drlRainbow?"active-tab":"") + "'>RAINBOW</div></div><div id='dSC' style='display:" + String(!settings.drlRainbow?"block":"none") + "'><input type='color' value='#" + colorToHex(settings.drlColor) + "' onchange=\"fetch('/color?val='+this.value.replace('#',''))\"></div></div>";
  html += "<div class='card'><label>Turn Mode</label><div class='tabs'><div id='tTS' onclick=\"sM('t','solid')\" class='tab " + String(!settings.turnRainbow?"active-tab":"") + "'>SOLID</div><div id='tTR' onclick=\"sM('t','rainbow')\" class='tab " + String(settings.turnRainbow?"active-tab":"") + "'>RAINBOW</div></div><div id='tSC' style='display:" + String(!settings.turnRainbow?"block":"none") + "'><input type='color' value='#" + colorToHex(settings.turnColor) + "' onchange=\"fetch('/tcolor?val='+this.value.replace('#',''))\"></div></div>";

  html += "<button class='btn-sr' id='sb' onclick=\"tS()\">ACTIVATE SHOWROOM</button>";

  // Tuner Tools
  html += "<div class='card'><div onclick='tT()' style='cursor:pointer;'><span style='color:var(--accent);font-size:1.1rem;font-weight:bold;'>TUNER TOOLS &#9662;</span></div><div id='tc' class='drop-content'>";
  html += "<div style='margin-top:20px;'><label>LED Count</label><input type='number' value='" + String(settings.numLeds) + "' onchange=\"fetch('/setleds?val='+this.value)\"></div>";
  html += "<div class='toggle-box' style='margin-top:10px;'><span>Swap L/R</span><button onclick=\"fetch('/invert')\" style='width:auto;padding:8px 15px;margin:0;font-size:0.7rem;background:" + String(settings.invert ? "#ff0055" : "#333") + "'>" + String(settings.invert ? "INVERTED" : "NORMAL") + "</button></div>";
  html += "<div style='margin-top:20px;'><label>Fine-Tune <span id='fV' class='val-label'>" + String(settings.fineTune) + "ms</span></label><input type='range' min='-15' max='15' value='" + String(settings.fineTune) + "' oninput='uF(this.value)' onchange=\"fetch('/fine?val='+this.value)\"></div>";
  html += "<button class='btn-sync' onclick=\"this.innerText='WAITING...';fetch('/sync')\">RE-LEARN BLINKER</button>";
  html += "<button class='btn-rst' onclick=\"if(confirm('Reset?'))fetch('/reset')\">FACTORY RESET</button></div></div></div>";

  html += "<script>let tC=0; function dCheck(){tC++; if(tC>=3){document.getElementById('db').style.display='block';} setTimeout(()=>tC=0,1000);}";
  html += "function tT(){let c=document.getElementById('tc'); c.style.display=(c.style.display=='block')?'none':'block';}";
  html += "function uB(v){document.getElementById('bPct').innerText=Math.round((v/255)*100)+'%';}";
  html += "function uF(v){document.getElementById('fV').innerText=v+'ms';}";
  html += "function sB(m){fetch('/bmode?val='+m); setTimeout(()=>location.reload(),200);}";
  html += "function fetch(u){var x=new XMLHttpRequest();x.open('GET',u,true);x.onload=function(){if(u.includes('/invert')||u.includes('/sync')||u.includes('/reset')||u.includes('/setleds'))location.reload();};x.send();}";
  html += "function sM(t,m){let isR=(m=='rainbow'); fetch((t=='d'?'/mode_d?val=':'/mode_t?val=')+m); setTimeout(()=>location.reload(),200);}";
  html += "function tS(){let b=document.getElementById('sb'); if(b.innerText=='ACTIVATE SHOWROOM'){fetch('/showroom?state=on'); b.innerText='DEACTIVATE'; b.style.background='#ff0055';}else{fetch('/showroom?state=off'); b.innerText='ACTIVATE SHOWROOM'; b.style.background='#007aff';}}</script></body></html>";
  return html;
}

void saveConfig() {
  prefs.begin("aura_core", false);
  prefs.putUInt("drl", (uint32_t)settings.drlColor);
  prefs.putUInt("turn", (uint32_t)settings.turnColor);
  prefs.putUInt("brake", (uint32_t)settings.brakeColor);
  prefs.putUChar("bright", settings.brightness);
  prefs.putBool("inv", settings.invert);
  prefs.putBool("dr_r", settings.drlRainbow);
  prefs.putBool("tr_r", settings.turnRainbow);
  prefs.putUChar("b_md", settings.brakeMode);
  prefs.putUInt("spd", settings.wipeSpeed);
  prefs.putChar("fine", settings.fineTune);
  prefs.putUInt("nled", settings.numLeds);
  prefs.end();
}

void loadConfig() {
  prefs.begin("aura_core", true);
  settings.drlColor = CRGB(prefs.getUInt("drl", 0xE10000));
  settings.turnColor = CRGB(prefs.getUInt("turn", 0xFFA500));
  settings.brakeColor = CRGB(prefs.getUInt("brake", 0xFF0000));
  settings.brightness = prefs.getUChar("bright", 200);
  settings.invert = prefs.getBool("inv", false);
  settings.drlRainbow = prefs.getBool("dr_r", false);
  settings.turnRainbow = prefs.getBool("tr_r", false);
  settings.brakeMode = prefs.getUChar("b_md", 0);
  settings.wipeSpeed = prefs.getUInt("spd", 35);
  settings.fineTune = prefs.getChar("fine", 0);
  settings.numLeds = prefs.getUInt("nled", 20); 
  prefs.end();
}

void handleSideLogic(int turnGpio, CRGB* strip, unsigned long &startTime) {
  bool turnActive = digitalRead(turnGpio);
  bool brakeActive = digitalRead(GPIO_BRAKE_IN);
  unsigned long now = millis();

  if (turnActive) {
    if (startTime == 0) startTime = now;
    int progress = (now - startTime) / max((int)(settings.wipeSpeed + settings.fineTune), 1);
    fadeToBlackBy(strip, settings.numLeds, 110);
    for(int i = 0; i < min((int)progress, (int)settings.numLeds); i++) {
      strip[i] = settings.turnRainbow ? CHSV((now/10)+(i*15), 255, 255) : settings.turnColor;
    }
  } else {
    if (startTime != 0) {
      uint16_t duration = now - startTime;
      if (duration > 150 && (settings.wipeSpeed == 35 || isSyncing)) { 
        settings.wipeSpeed = duration / settings.numLeds; 
        isSyncing = false; saveConfig(); 
      }
      startTime = 0;
    }
    
    if (brakeActive) {
      if (!isBraking) { brakeStartTime = now; isBraking = true; }
      bool lockout = (now - lastBrakeRelease < 3000);
      if (lockout || settings.brakeMode == 0) fill_solid(strip, settings.numLeds, settings.brakeColor);
      else if (settings.brakeMode == 1) { // F1 Strobe
        if ((now - brakeStartTime) < 600 && ((now - brakeStartTime) / 60) % 2 == 0) fill_solid(strip, settings.numLeds, CRGB::Black);
        else fill_solid(strip, settings.numLeds, settings.brakeColor);
      }
      else if (settings.brakeMode == 2) { // 2-Tap
        if ((now - brakeStartTime) < 400 && ((now - brakeStartTime) / 100) % 2 == 0) fill_solid(strip, settings.numLeds, CRGB::Black);
        else fill_solid(strip, settings.numLeds, settings.brakeColor);
      }
    } else {
      if (isBraking) { lastBrakeRelease = now; isBraking = false; }
      if (digitalRead(GPIO_DRL_IN)) {
        if (settings.drlRainbow) fill_rainbow(strip, settings.numLeds, now/20, 255/settings.numLeds);
        else fill_solid(strip, settings.numLeds, settings.drlColor);
      } else fill_solid(strip, settings.numLeds, CRGB::Black);
    }
  }
}

void setup() {
  loadConfig();
  pinMode(GPIO_DRL_IN, INPUT); pinMode(GPIO_BRAKE_IN, INPUT);
  pinMode(GPIO_TURN_L_IN, INPUT); pinMode(GPIO_TURN_R_IN, INPUT);
  FastLED.addLeds<WS2812B, GPIO_DATA_1, GRB>(leds1, MAX_LEDS);
  FastLED.addLeds<WS2812B, GPIO_DATA_2, GRB>(leds2, MAX_LEDS);
  FastLED.setBrightness(settings.brightness);
  WiFi.softAP("AURA_CORE", "aura1234");
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *r){ r->send(200, "text/html", getHTML()); });
  server.on("/bmode", HTTP_GET, [](AsyncWebServerRequest *r){ if(r->hasParam("val")){settings.brakeMode=r->getParam("val")->value().toInt(); saveConfig();} r->send(200); });
  server.on("/bcolor", HTTP_GET, [](AsyncWebServerRequest *r){ if(r->hasParam("val")){settings.brakeColor=strtol(r->getParam("val")->value().c_str(),NULL,16); saveConfig();} r->send(200); });
  server.on("/setleds", HTTP_GET, [](AsyncWebServerRequest *r){ if(r->hasParam("val")){settings.numLeds=min((uint16_t)r->getParam("val")->value().toInt(), (uint16_t)MAX_LEDS); saveConfig();} r->send(200); });
  server.on("/bright", HTTP_GET, [](AsyncWebServerRequest *r){ if(r->hasParam("val")){settings.brightness=r->getParam("val")->value().toInt(); FastLED.setBrightness(settings.brightness); saveConfig();} r->send(200); });
  server.on("/color", HTTP_GET, [](AsyncWebServerRequest *r){ if(r->hasParam("val")){settings.drlColor=strtol(r->getParam("val")->value().c_str(),NULL,16); saveConfig();} r->send(200); });
  server.on("/tcolor", HTTP_GET, [](AsyncWebServerRequest *r){ if(r->hasParam("val")){settings.turnColor=strtol(r->getParam("val")->value().c_str(),NULL,16); saveConfig();} r->send(200); });
  server.on("/mode_d", HTTP_GET, [](AsyncWebServerRequest *r){ if(r->hasParam("val")){settings.drlRainbow=(r->getParam("val")->value()=="rainbow"); saveConfig();} r->send(200); });
  server.on("/mode_t", HTTP_GET, [](AsyncWebServerRequest *r){ if(r->hasParam("val")){settings.turnRainbow=(r->getParam("val")->value()=="rainbow"); saveConfig();} r->send(200); });
  server.on("/invert", HTTP_GET, [](AsyncWebServerRequest *r){ settings.invert = !settings.invert; saveConfig(); r->send(200); });
  server.on("/sync", HTTP_GET, [](AsyncWebServerRequest *r){ isSyncing = true; r->send(200); });
  server.on("/fine", HTTP_GET, [](AsyncWebServerRequest *r){ if(r->hasParam("val")){settings.fineTune = r->getParam("val")->value().toInt(); saveConfig();} r->send(200); });
  server.on("/showroom", HTTP_GET, [](AsyncWebServerRequest *r){ if(r->hasParam("state")){showroomActive=(r->getParam("state")->value()=="on");} r->send(200); });
  server.on("/reset", HTTP_GET, [](AsyncWebServerRequest *r){ prefs.begin("aura_core", false); prefs.clear(); prefs.end(); ESP.restart(); });
  server.begin();
}

void loop() {
  if (showroomActive) {
    fill_rainbow(leds1, settings.numLeds, millis()/20, 10);
    fill_rainbow(leds2, settings.numLeds, millis()/20, 10);
  } else {
    handleSideLogic(GPIO_TURN_L_IN, settings.invert?leds2:leds1, turnStartL);
    handleSideLogic(GPIO_TURN_R_IN, settings.invert?leds1:leds2, turnStartR);
  }
  FastLED.show();
}