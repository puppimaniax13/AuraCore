#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <FastLED.h>
#include <Preferences.h>
#include <ArduinoJson.h>   // Add "ArduinoJson" by Benoit Blanchon in Library Manager

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
  uint16_t wipeSpeed;
  int8_t   fineTune;
  uint16_t numLeds;
} settings;

unsigned long turnStartL = 0, turnStartR = 0;
unsigned long brakeStartTime = 0;
unsigned long lastBrakeRelease = 0;
bool isBraking      = false;
bool showroomActive = false;
bool isSyncing      = false;

AsyncWebServer server(80);

// ─── Helpers ──────────────────────────────────────────────────────────────────

String colorToHex(CRGB c) {
  char hex[7];
  sprintf(hex, "%02X%02X%02X", c.r, c.g, c.b);
  return String(hex);
}

CRGB hexToColor(const String& hex) {
  long val = strtol(hex.c_str(), NULL, 16);
  return CRGB((val >> 16) & 0xFF, (val >> 8) & 0xFF, val & 0xFF);
}

// ─── HTML (served over WiFi) ──────────────────────────────────────────────────
// NOTE: The full HTML is large. On ESP32-S3 this fits comfortably in flash.
// If flash is tight, move to PROGMEM or LittleFS.

String getHTML() {
  String h = "";

  // ── Head ──
  h += F("<!DOCTYPE html><html lang='en'><head>");
  h += F("<meta name='viewport' content='width=device-width,initial-scale=1.0,maximum-scale=1.0,user-scalable=no'>");
  h += F("<title>AuraCore v2.0</title>");
  // Google Fonts omitted — no internet on the car's SoftAP network.

  // ── CSS ──
  h += F("<style>");
  h += F(":root{--bg:#080a0c;--surface:#0f1215;--card:#13171c;--border:#1e252e;--border2:#2a3340;--accent:#ff6a00;--accent2:#ffaa44;--blue:#3a8fff;--green:#2dff8e;--red:#ff3a3a;--amber:#ffbf00;--text:#cdd6e0;--muted:#4a5a6a;--dim:#232d38;}");
  h += F("*{box-sizing:border-box;margin:0;padding:0;-webkit-tap-highlight-color:transparent;}");
  h += F("body{background:var(--bg);color:var(--text);font-family:'Rajdhani','Arial Narrow',system-ui,sans-serif;min-height:100vh;display:flex;justify-content:center;padding:0 0 max(40px,env(safe-area-inset-bottom)) 0;}");
  h += F(".app{width:100%;max-width:420px;padding:0 14px;}");

  // Header
  h += F(".header{padding:28px 0 18px;display:flex;align-items:center;justify-content:space-between;}");
  h += F(".logo-name{font-weight:700;font-size:1.6rem;letter-spacing:5px;color:#fff;line-height:1;}");
  h += F(".logo-name span{color:var(--accent);}");
  h += F(".logo-sub{font-family:'JetBrains Mono','Menlo','Consolas',monospace;font-size:0.6rem;color:var(--muted);letter-spacing:3px;margin-top:3px;}");
  h += F(".conn-badge{display:flex;align-items:center;gap:6px;background:var(--card);border:1px solid var(--border);border-radius:20px;padding:8px 14px;font-family:'JetBrains Mono','Menlo','Consolas',monospace;font-size:0.65rem;color:var(--green);cursor:pointer;}");
  h += F(".conn-dot{width:7px;height:7px;border-radius:50%;background:var(--green);box-shadow:0 0 8px var(--green);animation:pulse-dot 2s infinite;}");
  h += F("@keyframes pulse-dot{0%,100%{opacity:1;}50%{opacity:0.4;}}");
  h += F(".divider{height:1px;background:linear-gradient(90deg,transparent,var(--border2),transparent);margin:4px 0 18px;}");

  // Status bar
  h += F(".status-bar{display:grid;grid-template-columns:1fr 1fr 1fr 1fr;gap:8px;margin-bottom:18px;}");
  h += F(".sig{background:var(--card);border:1px solid var(--border);border-radius:10px;padding:10px 6px;text-align:center;transition:all 0.2s;position:relative;overflow:hidden;}");
  h += F(".sig::before{content:'';position:absolute;inset:0;opacity:0;transition:opacity 0.2s;}");
  h += F(".sig.active-sig{border-color:currentColor;}.sig.active-sig::before{opacity:0.07;background:currentColor;}");
  h += F(".sig-icon{font-size:1.1rem;line-height:1;margin-bottom:4px;}");
  h += F(".sig-label{font-family:'JetBrains Mono','Menlo','Consolas',monospace;font-size:0.62rem;color:var(--muted);text-transform:uppercase;letter-spacing:0.5px;}");
  h += F(".sig-state{font-family:'JetBrains Mono','Menlo','Consolas',monospace;font-size:0.65rem;font-weight:700;margin-top:2px;}");
  h += F(".sig.drl{color:var(--blue);}.sig.brk{color:var(--red);}.sig.tl{color:var(--amber);}.sig.tr{color:var(--amber);}");

  // Car / Card / Shared
  h += F(".car-section,.card{background:var(--card);border:1px solid var(--border);border-radius:18px;padding:18px;margin-bottom:14px;}");
  h += F(".section-label,.card-tag{font-family:'JetBrains Mono','Menlo','Consolas',monospace;font-size:0.65rem;color:var(--muted);letter-spacing:2px;text-transform:uppercase;}");
  h += F(".card-header{display:flex;justify-content:space-between;align-items:center;margin-bottom:16px;}");
  h += F(".card-tag{background:var(--dim);border-radius:6px;padding:3px 8px;}");
  h += F(".card-title{font-weight:700;font-size:0.85rem;letter-spacing:2px;text-transform:uppercase;}");

  // Range inputs
  h += F("input[type='range']{-webkit-appearance:none;appearance:none;width:100%;height:6px;border-radius:3px;background:var(--dim);outline:none;}");
  h += F("input[type='range']::-webkit-slider-thumb{-webkit-appearance:none;width:26px;height:26px;border-radius:50%;background:var(--accent);border:3px solid var(--bg);box-shadow:0 0 10px rgba(255,106,0,0.5);cursor:pointer;}");

  // Brightness
  h += F(".bright-row{display:flex;align-items:center;gap:14px;}");
  h += F(".bright-val{font-family:'JetBrains Mono',monospace;font-size:1.4rem;font-weight:700;color:#fff;min-width:54px;text-align:right;}");
  h += F(".bright-unit{font-size:0.7rem;color:var(--muted);font-weight:400;}");

  // Tabs
  h += F(".tabs{display:flex;gap:6px;flex-wrap:wrap;}");
  h += F(".tab{flex:1;min-width:0;padding:12px 4px;border-radius:10px;border:1px solid var(--border);background:var(--surface);color:var(--muted);font-family:'Rajdhani','Arial Narrow',system-ui,sans-serif;font-weight:700;font-size:0.78rem;letter-spacing:0.5px;text-align:center;cursor:pointer;transition:all 0.15s;user-select:none;}");
  h += F(".tab:active{transform:scale(0.96);}.tab.active-tab{background:var(--dim);border-color:var(--accent);color:var(--accent);}");

  // Zone pickers
  h += F(".zone-row{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-top:14px;}");
  h += F(".zone-picker{display:flex;flex-direction:column;gap:6px;}");
  h += F(".zone-lbl{font-family:'JetBrains Mono','Menlo','Consolas',monospace;font-size:0.65rem;color:var(--muted);letter-spacing:1.5px;text-transform:uppercase;}");
  h += F("input[type='color']{width:100%;height:52px;border:1px solid var(--border2);border-radius:10px;background:var(--surface);cursor:pointer;padding:4px;}");
  h += F("input[type='number']{width:100%;height:52px;border:1px solid var(--border2);border-radius:10px;background:var(--surface);color:#fff;font-family:'JetBrains Mono','Menlo','Consolas',monospace;font-size:1rem;text-align:center;outline:none;}");

  // Rainbow badge
  h += F(".rainbow-badge{padding:14px;border-radius:10px;background:linear-gradient(90deg,#f00,#ff8000,#ff0,#0f0,#00f,#8b00ff);text-align:center;font-weight:700;font-size:0.75rem;letter-spacing:2px;color:#fff;text-shadow:1px 1px 4px #000;margin-top:14px;}");

  // Speed row
  h += F(".speed-row{display:flex;align-items:center;gap:10px;margin-top:8px;}");
  h += F(".speed-icon{font-size:1rem;}");

  // Showroom
  h += F(".btn-showroom{width:100%;padding:18px;border-radius:14px;border:none;background:var(--accent);color:#fff;font-family:'Rajdhani',sans-serif;font-weight:700;font-size:1rem;letter-spacing:3px;cursor:pointer;transition:all 0.2s;margin-bottom:14px;position:relative;overflow:hidden;}");
  h += F(".btn-showroom:active{transform:scale(0.98);}.btn-showroom.active{background:var(--red);box-shadow:0 0 20px rgba(255,58,58,0.35);}");

  // Tuner
  h += F(".tuner-toggle{display:flex;justify-content:space-between;align-items:center;cursor:pointer;user-select:none;padding:2px 0;}");
  h += F(".tuner-title{font-weight:700;font-size:0.85rem;letter-spacing:2px;color:var(--accent2);}");
  h += F(".tuner-arrow{font-size:0.8rem;color:var(--muted);transition:transform 0.25s;}.tuner-arrow.open{transform:rotate(180deg);}");
  h += F(".tuner-body{display:none;padding-top:18px;}.tuner-body.open{display:block;}");
  h += F(".field-row{margin-bottom:16px;}");
  h += F(".field-lbl{font-family:'JetBrains Mono','Menlo','Consolas',monospace;font-size:0.65rem;color:var(--muted);letter-spacing:1.5px;text-transform:uppercase;margin-bottom:8px;display:flex;justify-content:space-between;}");
  h += F(".field-lbl span{color:var(--accent);}");
  h += F(".toggle-row{display:flex;justify-content:space-between;align-items:center;background:var(--surface);border:1px solid var(--border);border-radius:12px;padding:14px 16px;margin-bottom:14px;}");
  h += F(".toggle-lbl{font-weight:600;font-size:0.8rem;letter-spacing:1px;}");
  h += F(".pill-btn.inverted{background:rgba(255,58,58,0.15);border-color:var(--red);color:var(--red);}");
  h += F(".sync-row{margin-top:12px;text-align:right;}");
  h += F(".sync-btn{font-family:'JetBrains Mono','Menlo','Consolas',monospace;font-size:0.65rem;color:var(--muted);cursor:pointer;background:none;border:1px solid var(--border);border-radius:6px;padding:8px 14px;letter-spacing:1px;transition:all 0.15s;}");
  h += F(".sync-btn:active{border-color:var(--accent);color:var(--accent);}");
  h += F(".pill-btn{padding:10px 18px;border-radius:8px;border:1px solid var(--border2);background:var(--dim);color:var(--text);font-family:'Rajdhani','Arial Narrow',system-ui,sans-serif;font-weight:700;font-size:0.75rem;letter-spacing:1px;cursor:pointer;transition:all 0.15s;}");
  h += F(".btn-learn{width:100%;padding:15px;border-radius:12px;border:1px solid var(--amber);background:rgba(255,191,0,0.08);color:var(--amber);font-family:'Rajdhani','Arial Narrow',system-ui,sans-serif;font-weight:700;font-size:0.88rem;letter-spacing:2px;cursor:pointer;margin-bottom:10px;transition:all 0.15s;}");
  h += F(".btn-learn:disabled{opacity:0.5;cursor:not-allowed;}");
  h += F(".btn-reset{width:100%;padding:14px;border-radius:12px;border:1px solid #2a1515;background:#0e0808;color:#ff5555;font-family:'Rajdhani','Arial Narrow',system-ui,sans-serif;font-weight:700;font-size:0.8rem;letter-spacing:2px;cursor:pointer;margin-top:8px;}");
  h += F(".debug-panel{display:none;font-family:'JetBrains Mono','Menlo','Consolas',monospace;font-size:0.65rem;color:#2dff8e;background:#000;border:1px solid #2dff8e;border-radius:10px;padding:12px;margin-top:14px;line-height:1.8;}");
  h += F(".toast{position:fixed;bottom:max(72px,calc(env(safe-area-inset-bottom) + 32px));left:50%;transform:translateX(-50%) translateY(8px);background:var(--dim);border:1px solid var(--border2);color:var(--green);font-family:'JetBrains Mono',monospace;font-size:0.68rem;letter-spacing:2px;padding:10px 22px;border-radius:20px;opacity:0;transition:opacity 0.18s,transform 0.18s;pointer-events:none;z-index:2000;white-space:nowrap;}");
  h += F(".toast.show{opacity:1;transform:translateX(-50%) translateY(0);}");
  h += F("body::before{content:'';position:fixed;inset:0;background:repeating-linear-gradient(0deg,transparent,transparent 2px,rgba(0,0,0,0.03) 2px,rgba(0,0,0,0.03) 4px);pointer-events:none;z-index:999;}");
  h += F("</style></head><body><div class='app'>");

  // ── Header ──
  h += F("<div class='header'><div class='logo'>");
  h += F("<div class='logo-name'>AURA<span>CORE</span></div>");
  h += F("<div class='logo-sub'>LOGIC CONTROLLER · V2.0</div></div>");
  h += F("<div class='conn-badge' onclick='dCheck()'><div class='conn-dot'></div>ONLINE</div></div>");
  h += F("<div class='divider'></div>");

  // ── Status bar ──
  h += F("<div class='status-bar'>");
  h += F("<div class='sig drl' id='sigDRL'><div class='sig-icon'>💡</div><div class='sig-label'>DRL</div><div class='sig-state' id='sDRL'>OFF</div></div>");
  h += F("<div class='sig brk' id='sigBRK'><div class='sig-icon'>🔴</div><div class='sig-label'>BRAKE</div><div class='sig-state' id='sBRK'>OFF</div></div>");
  h += F("<div class='sig tl'  id='sigTL'> <div class='sig-icon'>◀</div>  <div class='sig-label'>LEFT</div> <div class='sig-state' id='sTL'>OFF</div></div>");
  h += F("<div class='sig tr'  id='sigTR'> <div class='sig-icon'>▶</div>  <div class='sig-label'>RIGHT</div><div class='sig-state' id='sTR'>OFF</div></div>");
  h += F("</div>");

  // ── Car diagram ──
  h += F("<div class='car-section'><div class='section-label' style='margin-bottom:14px;'>LED ZONE MAP</div>");
  h += F("<svg viewBox='0 0 300 160' xmlns='http://www.w3.org/2000/svg' style='width:100%;max-width:340px;display:block;margin:0 auto;'>");
  h += F("<defs><filter id='glow'><feGaussianBlur stdDeviation='2.5' result='blur'/><feMerge><feMergeNode in='blur'/><feMergeNode in='SourceGraphic'/></feMerge></filter>");
  h += F("<linearGradient id='bg' x1='0' y1='0' x2='0' y2='1'><stop offset='0%' stop-color='#1e252e'/><stop offset='100%' stop-color='#111820'/></linearGradient></defs>");
  h += F("<rect x='60' y='30' width='180' height='100' rx='16' fill='url(#bg)' stroke='#2a3340' stroke-width='1.5'/>");
  h += F("<rect x='90' y='18' width='120' height='28' rx='8' fill='#131a22' stroke='#2a3340' stroke-width='1'/>");
  h += F("<rect x='94' y='20' width='112' height='24' rx='6' fill='#0d1520' opacity='0.8'/>");
  h += F("<circle cx='100' cy='130' r='14' fill='#0a0e14' stroke='#2a3340' stroke-width='2'/><circle cx='100' cy='130' r='7' fill='#13171c' stroke='#1e252e' stroke-width='1'/>");
  h += F("<circle cx='200' cy='130' r='14' fill='#0a0e14' stroke='#2a3340' stroke-width='2'/><circle cx='200' cy='130' r='7' fill='#13171c' stroke='#1e252e' stroke-width='1'/>");
  // Left rear zone
  h += "<rect x='62' y='45' width='18' height='50' rx='5' fill='#" + colorToHex(settings.brakeColorL) + "22' stroke='#" + colorToHex(settings.brakeColorL) + "' stroke-width='1.5' filter='url(#glow)'/>";
  h += F("<text x='71' y='100' font-family='monospace' font-size='8' fill='#4a5a6a' text-anchor='middle'>L</text>");
  // Right rear zone
  h += "<rect x='220' y='45' width='18' height='50' rx='5' fill='#" + colorToHex(settings.brakeColorR) + "22' stroke='#" + colorToHex(settings.brakeColorR) + "' stroke-width='1.5' filter='url(#glow)'/>";
  h += F("<text x='229' y='100' font-family='monospace' font-size='8' fill='#4a5a6a' text-anchor='middle'>R</text>");
  h += F("<rect x='120' y='58' width='60' height='8' rx='3' fill='#0a1018' stroke='#1e252e' stroke-width='1'/>");
  h += F("<rect x='120' y='74' width='60' height='8' rx='3' fill='#0a1018' stroke='#1e252e' stroke-width='1'/>");
  h += F("<rect x='120' y='90' width='60' height='8' rx='3' fill='#0a1018' stroke='#1e252e' stroke-width='1'/>");
  h += F("<text x='150' y='150' font-family='monospace' font-size='7' fill='#3a4a5a' text-anchor='middle'>REAR VIEW</text>");
  h += F("</svg>");
  h += F("<div style='display:flex;gap:10px;margin-top:12px;justify-content:center;'>");
  h += F("<div style='display:flex;align-items:center;gap:5px;'><div style='width:10px;height:10px;border-radius:2px;background:#ff3a3a;box-shadow:0 0 6px #ff3a3a;'></div><span style='font-family:JetBrains Mono,monospace;font-size:0.55rem;color:#4a5a6a;'>BRAKE/DRL</span></div>");
  h += F("<div style='display:flex;align-items:center;gap:5px;'><div style='width:10px;height:10px;border-radius:2px;background:#ffbf00;box-shadow:0 0 6px #ffbf00;'></div><span style='font-family:JetBrains Mono,monospace;font-size:0.55rem;color:#4a5a6a;'>TURN SIGNAL</span></div>");
  h += F("</div></div>");

  // ── Brightness ──
  int bPct = round((settings.brightness / 255.0) * 100);
  h += F("<div class='card'><div class='card-header'><div class='card-title'>BRIGHTNESS</div><div class='card-tag' id='bPct'>");
  h += String(bPct); h += F("%</div></div><div class='bright-row'>");
  h += "<input type='range' min='0' max='255' value='" + String(settings.brightness) + "' oninput='uB(this.value)' onchange=\"req('/bright?val='+this.value)\" style='flex:1;'>";
  h += "<div class='bright-val' id='bVal'>" + String(settings.brightness) + "<span class='bright-unit'> / 255</span></div>";
  h += F("</div></div>");

  // ── Brake ──
  String bm[5] = {"active-tab","","","",""};
  bm[settings.brakeMode] = "active-tab";
  h += F("<div class='card'><div class='card-header'><div class='card-title'>BRAKE LIGHT</div><div class='card-tag'>REAR</div></div>");
  h += F("<div class='tabs'>");
  h += "<div class='tab " + bm[0] + "' onclick='sB(0)' id='b0'>SOLID</div>";
  h += "<div class='tab " + bm[1] + "' onclick='sB(1)' id='b1'>F1</div>";
  h += "<div class='tab " + bm[2] + "' onclick='sB(2)' id='b2'>2-TAP</div>";
  h += "<div class='tab " + bm[3] + "' onclick='sB(3)' id='b3'>PULSE</div>";
  h += "<div class='tab " + bm[4] + "' onclick='sB(4)' id='b4'>FADE</div>";
  h += F("</div><div class='zone-row'>");
  h += "<div class='zone-picker'><div class='zone-lbl'>LEFT SIDE</div><input type='color' id='bColorL' value='#" + colorToHex(settings.brakeColorL) + "' onchange=\"sendColor('/bcolor_l',this.value)\"></div>";
  h += "<div class='zone-picker'><div class='zone-lbl'>RIGHT SIDE</div><input type='color' id='bColorR' value='#" + colorToHex(settings.brakeColorR) + "' onchange=\"sendColor('/bcolor_r',this.value)\"></div>";
  h += F("</div><div class='sync-row'><button class='sync-btn' onclick='syncBrakeColors()'>&#8596; SYNC L/R</button></div></div>");

  // ── DRL ──
  String dm[4] = {"","","",""};
  dm[settings.drlMode] = "active-tab";
  bool dSolid = (settings.drlMode == 0);
  h += F("<div class='card'><div class='card-header'><div class='card-title'>DRL</div><div class='card-tag'>DAYTIME</div></div>");
  h += F("<div class='tabs'>");
  h += "<div class='tab " + dm[0] + "' onclick=\"sM('d','solid')\" id='tDS'>SOLID</div>";
  h += "<div class='tab " + dm[1] + "' onclick=\"sM('d','rainbow')\" id='tDR'>RAINBOW</div>";
  h += "<div class='tab " + dm[2] + "' onclick=\"sM('d','breathe')\" id='tDB'>BREATHE</div>";
  h += "<div class='tab " + dm[3] + "' onclick=\"sM('d','chase')\" id='tDC'>CHASE</div>";
  h += F("</div>");
  h += "<div id='dSC' style='display:" + String(dSolid ? "block" : "none") + ";'>";
  h += F("<div class='zone-row'>");
  h += "<div class='zone-picker'><div class='zone-lbl'>LEFT SIDE</div><input type='color' id='dColorL' value='#" + colorToHex(settings.drlColorL) + "' onchange=\"sendColor('/drl_l',this.value)\"></div>";
  h += "<div class='zone-picker'><div class='zone-lbl'>RIGHT SIDE</div><input type='color' id='dColorR' value='#" + colorToHex(settings.drlColorR) + "' onchange=\"sendColor('/drl_r',this.value)\"></div>";
  h += F("</div><div class='sync-row'><button class='sync-btn' onclick='syncDRLColors()'>&#8596; SYNC L/R</button></div></div>");
  h += "<div id='dRC' style='display:" + String(!dSolid ? "block" : "none") + ";'><div class='rainbow-badge'>MODE ACTIVE</div></div></div>";

  // ── Turn ──
  String tm[3] = {"","",""};
  tm[settings.turnMode] = "active-tab";
  bool tSolid = (settings.turnMode == 0);
  h += F("<div class='card'><div class='card-header'><div class='card-title'>TURN SIGNAL</div><div class='card-tag'>SEQUENTIAL</div></div>");
  h += F("<div class='tabs'>");
  h += "<div class='tab " + tm[0] + "' onclick=\"sM('t','solid')\" id='tTS'>SOLID</div>";
  h += "<div class='tab " + tm[1] + "' onclick=\"sM('t','rainbow')\" id='tTR'>SPECTRAL</div>";
  h += "<div class='tab " + tm[2] + "' onclick=\"sM('t','chase')\" id='tTC'>CHASE</div>";
  h += F("</div>");
  h += "<div id='tSC' style='display:" + String(tSolid ? "block" : "none") + ";'>";
  h += F("<div class='zone-row'>");
  h += "<div class='zone-picker'><div class='zone-lbl'>LEFT TURN</div><input type='color' value='#" + colorToHex(settings.turnColorL) + "' onchange=\"sendColor('/tcolor_l',this.value)\"></div>";
  h += "<div class='zone-picker'><div class='zone-lbl'>RIGHT TURN</div><input type='color' value='#" + colorToHex(settings.turnColorR) + "' onchange=\"sendColor('/tcolor_r',this.value)\"></div>";
  h += F("</div></div>");
  h += "<div id='tRC' style='display:" + String(!tSolid ? "block" : "none") + ";'><div class='rainbow-badge'>SPECTRAL WIPE ACTIVE</div></div>";
  // Wipe speed
  h += F("<div style='margin-top:16px;'><div class='field-lbl'>WIPE SPEED <span id='wipeVal'>");
  h += String(settings.wipeSpeed); h += F("ms</span></div>");
  h += F("<div class='speed-row'><span class='speed-icon'>🐇</span>");
  h += "<input type='range' min='10' max='80' value='" + String(settings.wipeSpeed) + "' oninput=\"document.getElementById('wipeVal').innerText=this.value+'ms'\" onchange=\"req('/wipe?val='+this.value)\" style='flex:1;'>";
  h += F("<span class='speed-icon'>🐢</span></div></div></div>");

  // ── Showroom ──
  h += F("<button class='btn-showroom' id='srBtn' onclick='tS()'>&#9654; ACTIVATE SHOWROOM</button>");

  // ── Tuner ──
  h += F("<div class='card'><div class='tuner-toggle' onclick='toggleTuner()'>");
  h += F("<div class='tuner-title'>&#9881; TUNER TOOLS</div><div class='tuner-arrow' id='tunerArrow'>&#9660;</div></div>");
  h += F("<div class='tuner-body' id='tunerBody'>");
  h += F("<div class='field-row'><div class='field-lbl'>LED COUNT — PER SIDE <span id='ledCountVal'>");
  h += String(settings.numLeds); h += F("</span></div>");
  h += "<input type='number' value='" + String(settings.numLeds) + "' min='1' max='150' inputmode='numeric' pattern='[0-9]*' autocomplete='off' oninput=\"document.getElementById('ledCountVal').innerText=this.value\" onchange=\"req('/setleds?val='+this.value)\"></div>";
  h += "<div class='toggle-row'><div class='toggle-lbl'>SWAP LEFT / RIGHT</div>";
  h += "<button class='pill-btn" + String(settings.invert ? " inverted" : "") + "' id='invBtn' onclick='toggleInvert()'>" + String(settings.invert ? "INVERTED" : "NORMAL") + "</button></div>";
  h += F("<div class='field-row'><div class='field-lbl'>TIMING FINE-TUNE <span id='fineVal'>");
  h += String(settings.fineTune); h += F("ms</span></div>");
  h += F("<div class='speed-row'><span style='font-family:JetBrains Mono,monospace;font-size:0.6rem;color:#4a5a6a;'>-15</span>");
  h += "<input type='range' min='-15' max='15' value='" + String(settings.fineTune) + "' oninput='uF(this.value)' onchange=\"req('/fine?val='+this.value)\" style='flex:1;'>";
  h += F("<span style='font-family:JetBrains Mono,monospace;font-size:0.65rem;color:#4a5a6a;'>+15</span></div></div>");
  h += F("<button class='btn-learn' id='learnBtn' onclick='startLearn()'>&#8634; RE-LEARN BLINK TIMING</button>");
  h += F("<div class='debug-panel' id='db'>WIPE_SPD: <span style='float:right;color:#fff' id='dbSpd'>35ms</span><br>OFFSET: <span style='float:right;color:#fff' id='dbOffset'>0ms</span><br>FREE_HEAP: <span style='float:right;color:#fff' id='dbHeap'>--</span></div>");
  h += F("<button class='btn-reset' onclick=\"if(confirm('Factory reset? All settings will be erased.'))req('/reset')\">&#9888; FACTORY RESET DEVICE</button>");
  h += F("</div></div></div>");
  h += F("<div class='toast' id='toast'>SAVED</div>");

  // ── Scripts ──
  h += F("<script>");
  // HTTP helper
  h += F("function req(u){var x=new XMLHttpRequest();x.open('GET',u,true);");
  h += F("x.onload=function(){if(u.includes('/invert')||u.includes('/reset')||u.includes('/setleds'))location.reload();else toast('SAVED');};x.send();}");
  // Toast
  h += F("var _tt;function toast(m){var t=document.getElementById('toast');t.innerText=m;t.classList.add('show');clearTimeout(_tt);_tt=setTimeout(function(){t.classList.remove('show');},1400);}");
  // UI helpers
  h += F("function uB(v){var p=Math.round((v/255)*100);document.getElementById('bPct').innerText=p+'%';document.getElementById('bVal').innerHTML=v+'<span class=\"bright-unit\"> / 255</span>';}");
  h += F("function uF(v){var s=(v>0?'+':'')+v+'ms';document.getElementById('fineVal').innerText=s;document.getElementById('dbOffset').innerText=s;}");
  h += F("function sB(m){['b0','b1','b2','b3','b4'].forEach(function(id,i){document.getElementById(id).className='tab'+(i===m?' active-tab':'');});req('/bmode?val='+m);}");
  h += F("function sM(t,m){");
  h += F("if(t==='d'){['tDS','tDR','tDB','tDC'].forEach(function(id){document.getElementById(id).className='tab';});");
  h += F("var dm={solid:'tDS',rainbow:'tDR',breathe:'tDB',chase:'tDC'};document.getElementById(dm[m]).className='tab active-tab';");
  h += F("document.getElementById('dSC').style.display=(m==='solid'?'block':'none');document.getElementById('dRC').style.display=(m!=='solid'?'block':'none');req('/mode_d?val='+m);}");
  h += F("else{['tTS','tTR','tTC'].forEach(function(id){document.getElementById(id).className='tab';});");
  h += F("var tm2={solid:'tTS',rainbow:'tTR',chase:'tTC'};document.getElementById(tm2[m]).className='tab active-tab';");
  h += F("document.getElementById('tSC').style.display=(m==='solid'?'block':'none');document.getElementById('tRC').style.display=(m!=='solid'?'block':'none');req('/mode_t?val='+m);}");
  h += F("}");
  h += F("function sendColor(ep,hex){req(ep+'?val='+hex.replace('#',''));}");
  h += F("function syncBrakeColors(){var v=document.getElementById('bColorL').value;document.getElementById('bColorR').value=v;req('/bcolor_l?val='+v.replace('#',''));req('/bcolor_r?val='+v.replace('#',''));}");
  h += F("function syncDRLColors(){var v=document.getElementById('dColorL').value;document.getElementById('dColorR').value=v;req('/drl_l?val='+v.replace('#',''));req('/drl_r?val='+v.replace('#',''));}");
  h += F("function toggleTuner(){var b=document.getElementById('tunerBody');var a=document.getElementById('tunerArrow');var o=b.classList.toggle('open');a.classList.toggle('open',o);}");
  h += F("var inv="); h += String(settings.invert ? "true" : "false"); h += F(";");
  h += F("function toggleInvert(){inv=!inv;var btn=document.getElementById('invBtn');btn.innerText=inv?'INVERTED':'NORMAL';btn.className='pill-btn'+(inv?' inverted':'');req('/invert');}");
  h += F("function tS(){var b=document.getElementById('srBtn');var a=b.classList.toggle('active');b.innerHTML=a?'&#9632; DEACTIVATE SHOWROOM':'&#9654; ACTIVATE SHOWROOM';req('/showroom?state='+(a?'on':'off'));}");
  h += F("function startLearn(){var b=document.getElementById('learnBtn');b.innerText='&#9203; WAITING FOR BLINKER...';b.disabled=true;req('/sync');setTimeout(function(){b.innerHTML='&#8634; RE-LEARN BLINK TIMING';b.disabled=false;},6000);}");
  h += F("var _dTap=0;function dCheck(){_dTap++;if(_dTap>=3)document.getElementById('db').style.display='block';setTimeout(function(){_dTap=0;},1000);}");
  // Status polling
  h += F("function setSig(s,on){var el=document.getElementById('sig'+s);var st=document.getElementById('s'+s);");
  h += F("if(on){el.classList.add('active-sig');st.innerText='ON';}else{el.classList.remove('active-sig');st.innerText='OFF';}}");
  h += F("function pollStatus(){var x=new XMLHttpRequest();x.open('GET','/status',true);");
  h += F("x.onload=function(){try{var d=JSON.parse(x.responseText);setSig('DRL',d.drl);setSig('BRK',d.brake);setSig('TL',d.turnL);setSig('TR',d.turnR);document.getElementById('dbHeap').innerText=d.heap+'B';}catch(e){}};x.send();}");
  h += F("setInterval(pollStatus,500);");
  h += F("</script></body></html>");

  return h;
}

// ─── Save / Load ──────────────────────────────────────────────────────────────

void saveConfig() {
  prefs.begin("aura_core", false);
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
  settings.brakeMode   = prefs.getUChar("brkM", 0);
  settings.wipeSpeed   = prefs.getUInt("spd",   35);
  settings.fineTune    = prefs.getChar("fine",  0);
  settings.numLeds     = prefs.getUInt("nled",  20);
  prefs.end();
}

// ─── LED Logic ────────────────────────────────────────────────────────────────

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

void handleSideLogic(int turnGpio, CRGB* strip, CRGB turnColor, CRGB drlColor, CRGB brakeColor, unsigned long& startTime) {
  bool turnActive  = digitalRead(turnGpio);
  bool brakeActive = digitalRead(GPIO_BRAKE_IN);
  unsigned long now = millis();

  if (turnActive) {
    if (startTime == 0) startTime = now;
    int progress = (now - startTime) / max((int)(settings.wipeSpeed + settings.fineTune), 1);
    fadeToBlackBy(strip, settings.numLeds, 110);
    for (int i = 0; i < min((int)progress, (int)settings.numLeds); i++) {
      if (settings.turnMode == 1) strip[i] = CHSV((now / 10) + (i * 15), 255, 255);
      else if (settings.turnMode == 2) { fill_solid(strip, settings.numLeds, CRGB::Black); strip[i] = turnColor; }
      else strip[i] = turnColor;
    }
  } else {
    if (startTime != 0) {
      uint16_t dur = now - startTime;
      if (dur > 150 && isSyncing) { settings.wipeSpeed = dur / settings.numLeds; isSyncing = false; saveConfig(); }
      startTime = 0;
    }
    if (brakeActive) {
      if (!isBraking) { brakeStartTime = now; isBraking = true; }
      bool lockout = (now - lastBrakeRelease < 3000);
      if (lockout) fill_solid(strip, settings.numLeds, brakeColor);
      else applyBrake(strip, brakeColor, settings.brakeMode, now);
    } else {
      if (isBraking) { lastBrakeRelease = now; isBraking = false; }
      if (digitalRead(GPIO_DRL_IN))
        applyDRL(strip, drlColor, settings.drlMode, now);
      else fill_solid(strip, settings.numLeds, CRGB::Black);
    }
  }
}

// ─── Setup ────────────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  loadConfig();

  pinMode(GPIO_DRL_IN,    INPUT);
  pinMode(GPIO_BRAKE_IN,  INPUT);
  pinMode(GPIO_TURN_L_IN, INPUT);
  pinMode(GPIO_TURN_R_IN, INPUT);

  FastLED.addLeds<WS2812B, GPIO_DATA_1, GRB>(leds1, MAX_LEDS);
  FastLED.addLeds<WS2812B, GPIO_DATA_2, GRB>(leds2, MAX_LEDS);
  FastLED.setBrightness(settings.brightness);

  WiFi.softAP("AURA_CORE", "aura1234");
  Serial.println("AP IP: " + WiFi.softAPIP().toString());

  // ── Routes ──
  server.on("/",         HTTP_GET, [](AsyncWebServerRequest* r){ r->send(200, "text/html", getHTML()); });

  // Status (JSON for status bar polling)
  server.on("/status", HTTP_GET, [](AsyncWebServerRequest* r) {
    StaticJsonDocument<128> doc;
    doc["drl"]   = digitalRead(GPIO_DRL_IN);
    doc["brake"] = digitalRead(GPIO_BRAKE_IN);
    doc["turnL"] = digitalRead(GPIO_TURN_L_IN);
    doc["turnR"] = digitalRead(GPIO_TURN_R_IN);
    doc["heap"]  = ESP.getFreeHeap();
    String out; serializeJson(doc, out);
    r->send(200, "application/json", out);
  });

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
  server.on("/sync",    HTTP_GET, [](AsyncWebServerRequest* r){ isSyncing=true; r->send(200); });
  server.on("/showroom",HTTP_GET, [](AsyncWebServerRequest* r){ if(r->hasParam("state")){ showroomActive=(r->getParam("state")->value()=="on"); } r->send(200); });
  server.on("/reset",   HTTP_GET, [](AsyncWebServerRequest* r){ prefs.begin("aura_core",false); prefs.clear(); prefs.end(); r->send(200); delay(500); ESP.restart(); });

  server.begin();
}

// ─── Loop ─────────────────────────────────────────────────────────────────────

void loop() {
  if (showroomActive) {
    fill_rainbow(leds1, settings.numLeds, millis() / 20, 10);
    fill_rainbow(leds2, settings.numLeds, millis() / 20, 10);
  } else {
    CRGB* leftStrip  = settings.invert ? leds2 : leds1;
    CRGB* rightStrip = settings.invert ? leds1 : leds2;
    handleSideLogic(GPIO_TURN_L_IN, leftStrip,  settings.turnColorL, settings.drlColorL, settings.brakeColorL, turnStartL);
    handleSideLogic(GPIO_TURN_R_IN, rightStrip, settings.turnColorR, settings.drlColorR, settings.brakeColorR, turnStartR);
  }
  FastLED.show();
}
