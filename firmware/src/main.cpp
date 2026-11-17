// =====================================================================
//  Survival Wiki - offline survival reference for the ESP32 CYD
//  Supports:
//   - Hosyond / LCDWiki 3.5" ESP32-32E (E32R35T) [320x480 ST7796]
//   - Sunton 2.8" ESP32-2432S028R CYD [240x320 ILI9341]
//  * Touch UI on the screen, reading articles + pictures from SD
//  * Wi-Fi hotspot with captive portal: phones get the full wiki in a browser
// =====================================================================
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include "thai_text.h"
#include <TJpg_Decoder.h>
#include <vector>
#include <algorithm>
#include "config.h"
#include "doc.h"
#include "web.h"
#include "mesh.h"
#include "gps.h"
#include "offline_maps.h"
#include "map_storage.h"
#include "triage.h"
#include "qrcodegen.h"
#include "kedmanee.h"
#include "field_hardware.h"
#include "field_tracks.h"

ThaiDisplay tft;
static SPIClass sdSPI(VSPI);
static Preferences prefs;
static Doc doc;
static WifiConfig wcfg;

// ---------------------------------------------------------------- data
struct Category { String id, name, blurb; uint16_t color; uint16_t count; };
struct Entry { String id, title, extra; };

static Category cats[MAX_CATEGORIES];
static int nCats = 0;
static int nArticles = 0;
static std::vector<Entry> list;   // current category's articles, or the library books

// ---------------------------------------------------------------- state
enum Screen { SC_HOME, SC_CATEGORY, SC_ARTICLE, SC_LIBRARY, SC_BOOK, SC_WIFI, SC_SETTINGS, SC_NOSD, SC_LIGHT, SC_MESH, SC_QUICK, SC_KEYS, SC_NEAR, SC_MAP, SC_RADAR, SC_TRIAGE, SC_WPS, SC_INFO, SC_POIS, SC_MAP_SETS, SC_WHERE, SC_LOST, SC_GOTO, SC_CLOCK, SC_TRIP, SC_ALARM, SC_FIELD, SC_READY, SC_TRACK, SC_HARDWARE, SC_LORA };
static Screen screen = SC_HOME;
static int curCat = 0, listPage = 0, scrollY = 0, homePage = 0, nearPage = 0;
static int bookIdx = 0, bookPart = 1, bookParts = 1;
static String articleTitle;

static uint8_t brightness = 200;
static uint8_t timeoutIdx = 2;
static const uint16_t TIMEOUTS_S[] = {0, 60, 180, 300, 600};
static const char *TIMEOUT_NAMES[] = {"ไม่ปิด", "1 นาที", "3 นาที", "5 นาที", "10 นาที"};
static bool invertColors = false;
static bool wifiOn = true;
static bool sleeping = false;
static bool gpsShare = false;        // send our GPS position to nearby boards (off until the user turns it on)
static bool nightAuto = false;       // dim the backlight when the room is dark (CYD light sensor)
bool gNightUI = false;                 // night palette active (see NC() in config.h)
static bool nightNow = false;        // currently dimmed by night mode
static uint8_t lightMode = 0;        // flashlight: 0 white, 1 red, 2 SOS strobe
static bool sdUp = false;
static bool meshEnabled = true;      // ESP-NOW text messaging between boards
static String kbBuf;                 // text being typed on the on-screen keyboard
static uint8_t kbTarget = 0;         // 0 = new message, 1 = this board's mesh name
static bool kbThai = true;
static uint8_t kbThaiPage = 0;
static bool kbShift = false;
static int mapZ = 13, mapMinZ = 8, mapMaxZ = 16;   // current / allowed zoom levels
static int32_t mapCx = 0, mapCy = 0;                // map centre, in world pixels at zoom mapZ
static bool mapReady = false;                       // /maps/center.txt was found
static bool mapFollow = false;
static OfflinePoi poiItems[4];
static int poiCount=0;
static String poiQuery;
static uint32_t poiOffset=0,poiNext=UINT32_MAX;
static std::vector<uint32_t> poiHistory;
// Only one page lives in RAM regardless of how many provinces are on SD.
static MapSetEntry mapSets[4];
static int mapSetCount = 0;
static uint32_t mapSetOffset = 0, mapSetNext = UINT32_MAX;
static String mapSetError;
static bool mapLocalData = false;
static bool mapOpenData = false;                    // new OpenFreeMap packages need visible attribution
static uint32_t ledOffMs = 0;        // green "message received" blink
static int goodboyIdx = -1;         // index of the hidden "goodboy" category, -1 if absent
static uint32_t lastTouchMs = 0, lastStatusMs = 0;
static void startMesh();             // (re)start mesh messaging on the right radio channel (defined below)
static void saveBackup();            // settings backup to the SD card (defined below)
static void restoreBackup();
static void redraw();
static int lastClients = -1;

// ---------------------------------------------------------------- buttons
enum BtnId : uint16_t {
  B_NONE = 0, B_BACK, B_UP, B_DOWN, B_PREV, B_NEXT, B_LIBRARY, B_WIFI, B_SETTINGS,
  B_WIFI_TOGGLE, B_BRIGHT_DN, B_BRIGHT_UP, B_TIMEOUT, B_INVERT, B_CALIB, B_RETRY,
  B_LIGHT, B_NIGHT, B_BACKUP_SAVE, B_BACKUP_LOAD,
  B_MESH, B_MESH_TOGGLE, B_MESH_QUICK, B_MESH_TYPE, B_MESH_NAME, B_MESH_NEAR, B_RADAR, B_GPS_SHARE, B_KB_DEL, B_KB_SHIFT, B_KB_SEND, B_MAP, B_MAP_IN, B_MAP_OUT, B_TRIAGE, B_MAP_ME, B_MAP_MARK, B_MAP_PTS, B_WP_DEL, B_WP_CLEAR, B_TRK_CLEAR,
  B_KB_LANG = 54, B_KB_PAGE, B_INFO, B_MAP_SEARCH, B_POI_QUERY,
  B_MAP_SETS,
  B_QUICK_BASE = 60,           // 60..69: quick messages
  B_TRI_BASE = 70,             // 70..74: triage answers
  B_WHERE = 300, B_LOST, B_GOTO, B_CLOCK, B_SEND_SPOT, B_SET_START, B_GOTO_START, B_LOST_GUIDE, B_GOTO_PREV, B_GOTO_NEXT, B_WP_GO, B_TZ_DN, B_TZ_UP, B_TZ_DST, B_TZ_AUTO, B_TZ_24H, B_ALARM_CYCLE, B_ALARM_OK, B_ALARM_SNOOZE, B_TRIP, B_TRIP_RESET,
  B_FIELD = 400, B_READY, B_TRACK, B_HARDWARE, B_LORA, B_REC_TOGGLE, B_REVERSE, B_STOP_REVERSE, B_SUGGEST, B_USE_SUGGEST, B_BATTERY, B_SOUND, B_SOUND_TEST, B_LORA_ENABLE, B_LORA_CONFIG, B_LORA_NORMAL, B_LORA_WRITE, B_LORA_SEND_POS,
  B_TILE = 100, B_ROW = 200,
  B_KEY_BASE = 1000            // 1000 + character: on-screen keyboard keys
};
enum Icon : uint8_t { I_NONE, I_UP, I_DOWN, I_LEFT, I_RIGHT };
struct Btn { int16_t x, y, w, h; uint16_t id; String label; uint8_t icon; bool enabled; };
static std::vector<Btn> btns;
static MapSetEntry suggestedMap;
static String fieldNotice;
static uint32_t lastBatteryRevision = UINT32_MAX;

static const int ROWS_PER_PAGE = CONTENT_H / ROW_H;
static int listRowsPerPage() { return screen==SC_LIBRARY?CONTENT_H/(SCR_W<300?54:64):ROWS_PER_PAGE; }
static_assert(B_TRK_CLEAR < B_QUICK_BASE, "button ids overlap the quick-message range");

static bool articleFromTriage = false;               // the open guide came from the triage assistant
static const TriNode *triCur = &TRIAGE[0];
static const TriNode *triStack[8];
static int triDepth = 0;

static bool articleFromLost = false;                 // the open guide came from the I'm lost screen
static int gotoIdx = 0;                              // waypoint the Go to arrow points at
static Screen gpsHubFrom = SC_HOME;
static Screen gotoFrom = SC_WHERE;                   // where Back from the Go to screen returns
static bool lostFromTriage = false, wpGoMode = false;
static bool tzAuto = false, tzDst = false, clock24 = true;   // clock settings
static int8_t tzHours = 7;
static double lastLat = 0, lastLon = 0;              // last good GPS position (for the clock and sun times)
static bool haveLast = false;
static char gsToast[120] = "";
static uint32_t gsToastMs = 0, sunCalcMs = 0;
static double tripDist = 0, tripLat = 0, tripLon = 0;   // trip stats, counted while the GPS has a fix
static uint32_t tripMoveS = 0;
static float tripMaxKmh = 0, tripClimb = 0, tripAltRef = 0, tripSpd = 0;
static bool tripHave = false, tripPhone = false;
static uint32_t tripFixMs = 0, tripPositionMs = 0;
static uint8_t tbIdx = 0;                            // sunset alarm: index into TB_MIN (0 = off)
static int32_t alarmDay = -1;
static uint32_t alarmSnoozeEp = 0, alarmSunset = 0, alarmMinute = UINT32_MAX;
static Screen alarmPrev = SC_HOME;
static void updateTrip();
static String alarmLabel();


// ---------------------------------------------------------------- helpers
static uint16_t hexColor(const String &h) {
  if (h.length() < 7 || h[0] != '#') return C_ACCENT;
  long v = strtol(h.c_str() + 1, nullptr, 16);
  return NC(tft.color565((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF));
}

static int splitTabs(const String &line, String *out, int maxParts) {
  int n = 0, start = 0;
  while (n < maxParts) {
    int t = line.indexOf('\t', start);
    if (t < 0) { out[n++] = line.substring(start); break; }
    out[n++] = line.substring(start, t);
    start = t + 1;
  }
  for (int i = 0; i < n; i++) out[i].trim();
  return n;
}

static String readLine(File &f) {
  String s = f.readStringUntil('\n');
  s.replace("\r", "");
  return s;
}

static String fitText(String s, int maxW) {
  return tft.fit(s,maxW);
}

static String asciiOnly(const String &in) {
  String s=in;s.replace("\r", "");return s;
}

static void setBacklight(uint8_t v) { ledcWrite(0, v); }

// ---------------------------------------------------------------- loading
static void loadConfig() {
  File f = SD.open(F_CONFIG, FILE_READ);
  if (!f) return;
  while (f.available()) {
    String l = readLine(f);
    l.trim();
    if (!l.length() || l[0] == '#') continue;
    int eq = l.indexOf('=');
    if (eq < 0) continue;
    String k = l.substring(0, eq), v = l.substring(eq + 1);
    k.trim(); v.trim();
    if (k == "ssid" && v.length()) {char b[33];utf8Copy(b,v.c_str(),sizeof(b));wcfg.ssid=b;}
    else if (k == "password") {char b[64];utf8Copy(b,v.c_str(),sizeof(b));wcfg.password=b;}
    else if (k == "channel") { int c = v.toInt(); if (c >= 1 && c <= 11) wcfg.channel = c; }
  }
  f.close();
}

static void loadCatalog() {
  nCats = 0;
  nArticles = 0;
  File f = SD.open(F_CATEGORIES, FILE_READ);
  if (f) {
    while (f.available() && nCats < MAX_CATEGORIES) {
      String l = readLine(f);
      if (!l.length() || l[0] == '#') continue;
      String p[4];
      int n = splitTabs(l, p, 4);
      if (n < 2) continue;
      cats[nCats] = {p[0], asciiOnly(p[1]), n > 3 ? asciiOnly(p[3]) : String(""), hexColor(n > 2 ? p[2] : ""), 0};
      nCats++;
    }
    f.close();
  }
  goodboyIdx = -1;
  for (int i = 0; i < nCats; i++) if (cats[i].id == "goodboy") goodboyIdx = i;
  f = SD.open(F_CATALOG, FILE_READ);
  if (f) {
    while (f.available()) {
      String l = readLine(f);
      if (!l.length() || l[0] == '#') continue;
      String p[3];
      if (splitTabs(l, p, 3) < 3) continue;
      for (int i = 0; i < nCats; i++) if (cats[i].id == p[1]) { cats[i].count++; nArticles++; break; }
    }
    f.close();
  }
}

static void loadCategoryList(int ci) {
  list.clear();
  File f = SD.open(F_CATALOG, FILE_READ);
  if (!f) return;
  while (f.available() && list.size() < MAX_ARTICLES) {
    String l = readLine(f);
    if (!l.length() || l[0] == '#') continue;
    String p[3];
    if (splitTabs(l, p, 3) < 3) continue;
    if (p[1] == cats[ci].id) list.push_back({p[0], asciiOnly(p[2]), ""});
  }
  f.close();
}

static void loadLibraryList() {
  list.clear();
  File f = SD.open(F_LIBRARY, FILE_READ);
  if (!f) return;
  while (f.available() && list.size() < MAX_BOOKS) {
    String l = readLine(f);
    if (!l.length() || l[0] == '#') continue;
    String p[4];
    if (splitTabs(l, p, 4) < 3) continue;
    list.push_back({p[0], asciiOnly(p[1]), p[2]});
  }
  f.close();
}

// ---------------------------------------------------------------- drawing
static void drawIcon(uint8_t icon, int cx, int cy, uint16_t col) {
  int half = (SCR_W < 300) ? 8 : 10;
  int hSide = (SCR_W < 300) ? 5 : 6;
  switch (icon) {
    case I_UP:    tft.fillTriangle(cx - half, cy + hSide, cx + half, cy + hSide, cx, cy - (hSide + 1), col); break;
    case I_DOWN:  tft.fillTriangle(cx - half, cy - hSide, cx + half, cy - hSide, cx, cy + (hSide + 1), col); break;
    case I_LEFT:  tft.fillTriangle(cx + hSide, cy - half, cx + hSide, cy + half, cx - (hSide + 1), cy, col); break;
    case I_RIGHT: tft.fillTriangle(cx - hSide, cy - half, cx - hSide, cy + half, cx + (hSide + 1), cy, col); break;
  }
}

static void drawButton(const Btn &b, bool pressed) {
  uint16_t bg = pressed ? C_PRESS : C_PANEL;
  tft.fillRoundRect(b.x + 2, b.y + 2, b.w - 4, b.h - 4, (SCR_W < 300 ? 5 : 6), bg);
  uint16_t fg = b.enabled ? C_TEXT : C_LINE;
  int cx = b.x + b.w / 2, cy = b.y + b.h / 2;
  if (b.icon != I_NONE) {
    if (b.label.length()) {
      if (SCR_W < 300) tft.setTextFont(2); else tft.setFreeFont(&FreeSansBold9pt7b);
      String label=fitText(b.label,b.w-30);
      int tw = tft.textWidth(label);
      int total = 18 + 6 + tw;
      int ix = cx - total / 2 + 9;
      drawIcon(b.icon, ix, cy, b.enabled ? C_ACCENT : C_LINE);
      tft.setTextDatum(ML_DATUM);
      tft.setTextColor(fg);
      tft.drawString(label, ix + (SCR_W < 300 ? 12 : 15), cy);
    } else {
      drawIcon(b.icon, cx, cy, b.enabled ? C_ACCENT : C_LINE);
    }
  } else {
    if (SCR_W < 300) tft.setTextFont(2); else tft.setFreeFont(&FreeSansBold9pt7b);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(fg);
    if(b.w<60||b.h<30)tft.setTextFont(1);
    tft.drawString(fitText(b.label,b.w-8), cx, cy);
  }
}

static void addBtn(int x, int y, int w, int h, uint16_t id, const String &label, uint8_t icon = I_NONE, bool en = true) {
  btns.push_back({(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h, id, label, icon, en});
  drawButton(btns.back(), false);
}

struct FootDef { uint16_t id; const char *label; uint8_t icon; bool en; };
static void drawFooter(const FootDef *defs, int n) {
  int y = SCR_H - FOOTER_H;
  tft.fillRect(0, y, SCR_W, FOOTER_H, C_HEADER);
  int w = SCR_W / n;
  for (int i = 0; i < n; i++) {
    int x = i * w;
    int ww = (i == n - 1) ? SCR_W - x : w;
    if (defs[i].id == B_NONE) {   // plain label (page / percent indicator)
      tft.setTextFont(2);
      tft.setTextDatum(MC_DATUM);
      tft.setTextColor(C_MUTED);
      tft.drawString(defs[i].label, x + ww / 2, y + FOOTER_H / 2);
    } else {
      addBtn(x, y + 2, ww, FOOTER_H - 4, defs[i].id, defs[i].label, defs[i].icon, defs[i].en);
    }
  }
}

static void drawWifiBadge() {
  int bw = (SCR_W < 300) ? 58 : 74;
  int x = SCR_W - bw, y = 0;
  tft.fillRect(x, y, bw, HEADER_H, C_HEADER);
  int cx = x + (SCR_W < 300 ? 12 : 16);
  int cy = (SCR_W < 300 ? 22 : 30);
  uint16_t col = wifiOn ? NC(0x07E9) : C_LINE;
  int rStart = (SCR_W < 300) ? 3 : 4;
  int rEnd = (SCR_W < 300) ? 11 : 14;
  int rStep = (SCR_W < 300) ? 4 : 5;
  for (int r = rStart; r <= rEnd; r += rStep) {
    tft.drawArc(cx, cy, r + 1, r - 1, 135, 225, col, C_HEADER, false);
  }
  tft.fillCircle(cx, cy, 2, col);
  tft.setTextFont(2);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(wifiOn ? C_TEXT : C_MUTED);
  tft.drawString(wifiOn ? String(webClientCount()) + " คน" : String("ปิด"), cx + (SCR_W < 300 ? 11 : 14), (SCR_W < 300 ? 18 : 24));
}

static void drawBatteryStatus() {
  FieldBattery battery = fieldBattery();
  tft.fillRect(0, HEADER_H, SCR_W, STATUS_H, C_HEADER);
  int x = SCR_W-57;
  uint16_t color = battery.valid && battery.percent <= 15 ? C_ACCENT : C_TEXT;
  tft.drawRect(x, HEADER_H+3, 17, 9, color);
  tft.fillRect(x+17, HEADER_H+5, 2, 5, color);
  if (battery.valid) tft.fillRect(x+2, HEADER_H+5, 13*battery.percent/100, 5, color);
  tft.TFT_eSPI::setTextFont(1);
  tft.TFT_eSPI::setTextColor(color, C_HEADER);
  tft.TFT_eSPI::setTextDatum(TL_DATUM);
  tft.TFT_eSPI::drawString(battery.valid ? String(battery.percent)+"%" : "--%", x+22, HEADER_H+3);
  if (fieldTrackRecording()) {
    tft.TFT_eSPI::setTextColor(NC(TFT_RED), C_HEADER);
    tft.TFT_eSPI::drawString("REC", 5, HEADER_H+3);
  }
  lastBatteryRevision = battery.revision;
}

static void drawHeader(const String &title, bool back) {
  tft.fillRect(0, 0, SCR_W, HEADER_H, C_HEADER);
  tft.drawFastHLine(0, HEADER_H - 1, SCR_W, C_ACCENT);
  int tx = (SCR_W < 300 ? 8 : 12);
  int bw = (SCR_W < 300) ? 58 : 74;
  if (back) {
    int backBtnW = (SCR_W < 300) ? 38 : 52;
    btns.push_back({0, 0, (int16_t)backBtnW, HEADER_H, B_BACK, "", I_LEFT, true});
    drawIcon(I_LEFT, backBtnW / 2, HEADER_H / 2, C_ACCENT);
    tx = backBtnW;
  }
  if (SCR_W < 300) tft.setFreeFont(&FreeSansBold9pt7b); else tft.setFreeFont(&FreeSansBold12pt7b);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(C_TEXT);
  int maxW = SCR_W - tx - (back ? 10 : (bw + 6));
  String t = fitText(title, maxW);
  if (SCR_W >= 300 && (tft.textWidth(t) > maxW || t.endsWith("..."))) {   // long titles: smaller font
    tft.setFreeFont(&FreeSansBold9pt7b);
    t = fitText(title, maxW);
  }
  tft.drawString(t, tx, HEADER_H / 2);
  if (!back) drawWifiBadge();
  drawBatteryStatus();
}

static void drawCatIcon(const String &id, int cx, int cy) {
  uint16_t w = NC(TFT_WHITE);
  if (id == "water") {
    tft.fillCircle(cx, cy + 4, 8, w);
    tft.fillTriangle(cx - 8, cy + 2, cx + 8, cy + 2, cx, cy - 12, w);
  } else if (id == "fire") {
    tft.fillTriangle(cx - 9, cy + 9, cx + 9, cy + 9, cx, cy - 12, w);
    tft.fillTriangle(cx - 9, cy + 9, cx + 1, cy + 9, cx - 6, cy - 4, w);
    tft.fillTriangle(cx - 1, cy + 9, cx + 9, cy + 9, cx + 7, cy - 6, w);
  } else if (id == "shelter") {
    tft.fillTriangle(cx - 13, cy + 9, cx + 13, cy + 9, cx, cy - 11, w);
    tft.fillTriangle(cx - 3, cy + 9, cx + 3, cy + 9, cx, cy, C_BG);
  } else if (id == "firstaid") {
    tft.fillRect(cx - 3, cy - 11, 7, 22, w);
    tft.fillRect(cx - 11, cy - 3, 22, 7, w);
  } else if (id == "food") {
    tft.fillEllipse(cx - 2, cy, 11, 6, w);
    tft.fillTriangle(cx + 7, cy, cx + 14, cy - 7, cx + 14, cy + 7, w);
  } else if (id == "navigation") {
    tft.drawCircle(cx, cy, 11, w);
    tft.fillTriangle(cx - 4, cy, cx + 4, cy, cx, cy - 9, NC(TFT_RED));
    tft.fillTriangle(cx - 4, cy, cx + 4, cy, cx, cy + 9, w);
  } else if (id == "signals") {
    tft.fillCircle(cx, cy, 3, w);
    for (int r = 7; r <= 13; r += 6) tft.drawCircle(cx, cy, r, w);
  } else if (id == "weather") {          // cloud with a lightning bolt
    tft.fillCircle(cx - 6, cy - 3, 6, w);
    tft.fillCircle(cx + 3, cy - 6, 7, w);
    tft.fillCircle(cx + 9, cy - 1, 5, w);
    tft.fillRect(cx - 9, cy - 3, 22, 7, w);
    tft.fillTriangle(cx + 1, cy + 1, cx - 5, cy + 11, cx + 1, cy + 8, NC(TFT_YELLOW));
    tft.fillTriangle(cx - 2, cy + 6, cx + 5, cy + 6, cx - 1, cy + 14, NC(TFT_YELLOW));
  } else if (id == "vehicles") {          // car
    tft.fillRoundRect(cx - 12, cy - 1, 25, 9, 3, w);
    tft.fillTriangle(cx - 8, cy - 1, cx - 4, cy - 8, cx + 4, cy - 8, w);
    tft.fillTriangle(cx - 8, cy - 1, cx + 4, cy - 8, cx + 8, cy - 1, w);
    tft.fillCircle(cx - 7, cy + 8, 3, C_BG); tft.drawCircle(cx - 7, cy + 8, 3, w);
    tft.fillCircle(cx + 7, cy + 8, 3, C_BG); tft.drawCircle(cx + 7, cy + 8, 3, w);
  } else if (id == "radio") {             // handheld radio with an antenna
    tft.fillRoundRect(cx - 8, cy - 4, 17, 17, 3, w);
    tft.drawLine(cx + 4, cy - 4, cx + 9, cy - 13, w); tft.drawLine(cx + 5, cy - 4, cx + 10, cy - 13, w);
    tft.fillCircle(cx, cy + 2, 3, C_BG);
    tft.fillRect(cx - 5, cy + 8, 11, 2, C_BG);
  } else if (id == "family") {            // adult and child
    tft.fillCircle(cx - 5, cy - 8, 4, w);
    tft.fillRoundRect(cx - 10, cy - 2, 11, 14, 3, w);
    tft.fillCircle(cx + 8, cy - 1, 3, w);
    tft.fillRoundRect(cx + 3, cy + 3, 10, 9, 3, w);
  } else if (id == "home") {              // house with a door
    tft.fillTriangle(cx - 13, cy - 1, cx + 13, cy - 1, cx, cy - 12, w);
    tft.fillRect(cx - 9, cy - 1, 19, 12, w);
    tft.fillRect(cx - 2, cy + 4, 5, 7, C_BG);
  } else if (id == "basics") {
    tft.drawCircle(cx, cy, 11, w);
    tft.fillRect(cx - 1, cy - 7, 3, 9, w);
    tft.fillRect(cx - 1, cy + 4, 3, 3, w);
  } else {
    tft.drawCircle(cx, cy, 10, w);
  }
}

static void drawTriageTile(int x, int y, int tw, int th) {
  btns.push_back({(int16_t)x, (int16_t)y, (int16_t)tw, (int16_t)th, (uint16_t)B_TRIAGE, "", I_NONE, true});
  uint16_t red = NC(tft.color565(190, 40, 40));
  tft.fillRoundRect(x, y, tw, th, (SCR_W < 300 ? 8 : 10), red);
  int cx = SCR_W < 300 ? x + 18 : x + 24, cy = SCR_W < 300 ? y + th / 2 : y + 26;
  int a = SCR_W < 300 ? 9 : 12, b = SCR_W < 300 ? 3 : 4;
  tft.fillRect(cx - a, cy - b, 2 * a + 1, 2 * b + 1, NC(TFT_WHITE));
  tft.fillRect(cx - b, cy - a, 2 * b + 1, 2 * a + 1, NC(TFT_WHITE));
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(NC(TFT_WHITE));
  if (SCR_W < 300) {
    tft.setTextFont(2);
    tft.drawString("ประเมินเหตุ", x + 34, y + 3);
    tft.setTextFont(1);
    tft.drawString("เกิดอะไรขึ้น?", x + 35, y + 31);
  } else {
    tft.setFreeFont(&FreeSansBold9pt7b);
    tft.drawString("ประเมินเหตุ", x + 10, y + th - 40);
    tft.setTextFont(2);
    tft.drawString("เกิดอะไรขึ้น?", x + 10, y + th - 20);
  }
}

static void drawHome() {
  btns.clear();
  tft.fillScreen(C_BG);
  drawHeader("คู่มือเอาตัวรอด", false);
  const int cols = 2, rows = SCR_W<300?3:4, gap = TILE_GAP, perPage = cols * rows;
  int vis[MAX_CATEGORIES + 3], nVis = 0;
  vis[nVis++] = -2;                                    // the Triage tile comes first
  vis[nVis++] = -4;
  vis[nVis++] = -3;                                    // GPS tools also show while waiting for a fix
  for (int i = 0; i < nCats; i++) if (i != goodboyIdx) vis[nVis++] = i;   // goodboy is the hidden easter egg
  int pages = max(1, (nVis + perPage - 1) / perPage);
  if (homePage >= pages) homePage = pages - 1;
  const int strip = pages > 1 ? (SCR_W < 300 ? 26 : 34) : 0;   // page switcher under the tiles
  int tw = (SCR_W - gap * (cols + 1)) / cols;
  int th = (CONTENT_H - strip - gap * (rows + 1)) / rows;
  if (strip) {
    int sy = SCR_H - FOOTER_H - strip;
    char pg[12]; snprintf(pg, sizeof(pg), "%d / %d", homePage + 1, pages);
    addBtn(gap, sy, SCR_W / 3 - gap, strip - 2, B_PREV, "", I_LEFT, homePage > 0);
    addBtn(SCR_W - SCR_W / 3, sy, SCR_W / 3 - gap, strip - 2, B_NEXT, "", I_RIGHT, homePage < pages - 1);
    tft.setTextFont(2); tft.setTextDatum(MC_DATUM); tft.setTextColor(C_MUTED);
    tft.drawString(pg, SCR_W / 2, sy + strip / 2 - 1);
  }
  for (int k = homePage * perPage; k < nVis && k < (homePage + 1) * perPage; k++) {
    int i = vis[k], slot = k - homePage * perPage;
    int x = gap + (slot % cols) * (tw + gap);
    int y = CONTENT_Y + gap + (slot / cols) * (th + gap);
    if (i == -2) { drawTriageTile(x, y, tw, th); continue; }
    if (i == -4) { addBtn(x,y,tw,th,B_FIELD,"ภาคสนาม");continue; }
    if (i == -3) { addBtn(x, y, tw, th, B_WHERE, "GPS"); continue; }
    btns.push_back({(int16_t)x, (int16_t)y, (int16_t)tw, (int16_t)th, (uint16_t)(B_TILE + i), "", I_NONE, true});
    tft.fillRoundRect(x, y, tw, th, (SCR_W < 300 ? 8 : 10), cats[i].color);
    if (cats[i].id == "thailand") {
      tft.setTextFont(1);tft.setTextDatum(MC_DATUM);tft.setTextColor(TFT_WHITE);
      tft.drawString("ใช้งานใน",x+tw/2,y+th/2-12);
      tft.drawString("ประเทศไทย",x+tw/2,y+th/2+12);
      continue;
    }
    if (SCR_W < 300) {
      // 2.8" compact split: icon on left, title + count on right
      drawCatIcon(cats[i].id, x + 18, y + th / 2);
      tft.setTextFont(2);
      tft.setTextDatum(TL_DATUM);
      tft.setTextColor(TFT_WHITE);
      tft.drawString(fitText(cats[i].name, tw - 38), x + 34, y + 3);
      tft.setTextFont(1);
      tft.drawString(String(cats[i].count) + (cats[i].count == 1 ? " เรื่อง" : " เรื่อง"), x + 35, y + 31);
    } else {
      // 3.5" full vertical stack
      drawCatIcon(cats[i].id, x + 24, y + 26);
      tft.setFreeFont(&FreeSansBold9pt7b);
      tft.setTextDatum(TL_DATUM);
      tft.setTextColor(TFT_WHITE);
      tft.drawString(fitText(cats[i].name, tw - 16), x + 10, y + th - 40);
      tft.setTextFont(2);
      tft.drawString(String(cats[i].count) + (cats[i].count == 1 ? " เรื่อง" : " เรื่อง"), x + 10, y + th - 20);
    }
  }
  if (nCats == 0) {
    tft.setTextFont(2);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_MUTED);
    tft.drawString("ไม่พบบทความในการ์ด SD", SCR_W / 2, CONTENT_Y + (SCR_W < 300 ? 90 : 150));
    tft.drawString("คัดลอกไฟล์ใน sdcard ลงการ์ด", SCR_W / 2, CONTENT_Y + (SCR_W < 300 ? 112 : 172));
  }
  static char meshLabel[32];
  snprintf(meshLabel, sizeof(meshLabel), meshUnread() > 0 ? "เมช*" : "เมช");
  FootDef f[] = {{B_LIBRARY, "คู่มือ", I_NONE, true}, {B_WIFI, "Wi-Fi", I_NONE, true},
                 {B_LIGHT, "ไฟ", I_NONE, true}, {B_MESH, meshLabel, I_NONE, true},
                 {B_MAP, "แผนที่", I_NONE, true}, {B_SETTINGS, "ตั้งค่า", I_NONE, true}};
  drawFooter(f, 6);
}

static void drawList(const String &title, bool books) {
  btns.clear();
  tft.fillScreen(C_BG);
  drawHeader(title, true);
  int rowHeight=books?(SCR_W<300?54:64):ROW_H;
  int rows=CONTENT_H/rowHeight;
  int pages = max(1, (int)((list.size() + rows - 1) / rows));
  if (listPage >= pages) listPage = pages - 1;
  int first = listPage * rows;
  for (int r = 0; r < rows && first + r < (int)list.size(); r++) {
    int y = CONTENT_Y + r * rowHeight;
    const Entry &e = list[first + r];
    btns.push_back({0, (int16_t)y, SCR_W, (int16_t)rowHeight, (uint16_t)(B_ROW + first + r), "", I_NONE, true});
    if (SCR_W < 300) tft.setTextFont(2); else tft.setFreeFont(&FreeSans9pt7b);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(C_TEXT);
    if (books) {
      tft.drawString(fitText(e.title, SCR_W - (SCR_W < 300 ? 40 : 50)), (SCR_W < 300 ? 8 : 12), y + (SCR_W < 300 ? 15 : 17));
      tft.setTextFont(SCR_W < 300 ? 1 : 2);
      tft.setTextColor(C_MUTED);
      tft.drawString(e.extra + " ตอน", (SCR_W < 300 ? 8 : 12), y + (SCR_W < 300 ? 40 : 47));
    } else {
      tft.drawString(fitText(e.title, SCR_W - (SCR_W < 300 ? 36 : 50)), (SCR_W < 300 ? 8 : 12), y + ROW_H / 2);
    }
    drawIcon(I_RIGHT, SCR_W - (SCR_W < 300 ? 14 : 20), y + rowHeight / 2, C_LINE);
    tft.drawFastHLine(8, y + rowHeight - 1, SCR_W - 16, C_LINE);
  }
  if (list.empty()) {
    tft.setTextFont(2);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_MUTED);
    tft.drawString(books ? "ยังไม่มีคู่มือใน /library" : "ยังไม่มีข้อมูล", SCR_W / 2, CONTENT_Y + (SCR_W < 300 ? 80 : 120));
  }
  String pg = String(listPage + 1) + " / " + pages;
  FootDef f[] = {{B_BACK, "กลับ", I_LEFT, true}, {B_UP, "", I_UP, listPage > 0}, {B_NONE, pg.c_str(), I_NONE, true},
                 {B_DOWN, "", I_DOWN, listPage < pages - 1}};
  drawFooter(f, 4);
}

static void drawDocView() {
  tft.setViewport(0, CONTENT_Y, SCR_W, CONTENT_H);
  doc.draw(scrollY);
  tft.resetViewport();
}

static void drawDocFooter() {
  btns.erase(std::remove_if(btns.begin(), btns.end(), [](const Btn &b) { return b.id != B_BACK || b.y != 0; }), btns.end());
  int maxScroll = max(0, doc.height() - CONTENT_H);
  int pct = maxScroll ? (scrollY * 100 / maxScroll) : 100;
  String p = String(pct) + "%";
  if (screen == SC_BOOK) {
    String part = String(bookPart) + "/" + bookParts;
    FootDef f[] = {{B_BACK, "กลับ", I_LEFT, true}, {B_PREV, "", I_LEFT, bookPart > 1}, {B_UP, "", I_UP, scrollY > 0 || bookPart > 1},
                   {B_NONE, part.c_str(), I_NONE, true}, {B_DOWN, "", I_DOWN, scrollY < maxScroll || bookPart < bookParts},
                   {B_NEXT, "", I_RIGHT, bookPart < bookParts}};
    drawFooter(f, 6);
  } else {
    FootDef f[] = {{B_BACK, "กลับ", I_LEFT, true}, {B_UP, "", I_UP, scrollY > 0}, {B_NONE, p.c_str(), I_NONE, true},
                   {B_DOWN, "", I_DOWN, scrollY < maxScroll}};
    drawFooter(f, 4);
  }
}

static void showLoading(const char *msg) {
  tft.fillRect(0, CONTENT_Y, SCR_W, CONTENT_H, C_BG);
  tft.setTextFont(2);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_MUTED);
  tft.drawString(msg, SCR_W / 2, CONTENT_Y + CONTENT_H / 2);
}

static void openArticle(const Entry &e) {
  screen = SC_ARTICLE;
  scrollY = 0;
  articleTitle = e.title;
  btns.clear();
  drawHeader(e.title, true);
  showLoading("กำลังโหลด...");
  String path = String(D_ARTICLES) + e.id + ".md";
  if (!doc.load(path.c_str(), true)) {
    showLoading("เปิดบทความนี้ไม่ได้");
  } else {
    drawDocView();
  }
  drawDocFooter();
}

static void openBookPart(bool atEnd) {
  screen = SC_BOOK;
  btns.clear();
  drawHeader(list[bookIdx].title, true);
  showLoading("กำลังโหลด...");
  char name[16];
  snprintf(name, sizeof(name), "/%04d.txt", bookPart);
  String path = String(D_LIBRARY) + list[bookIdx].id + name;
  if (!doc.load(path.c_str(), false)) {
    showLoading("เปิดคู่มือตอนนี้ไม่ได้");
    scrollY = 0;
  } else {
    scrollY = atEnd ? max(0, doc.height() - CONTENT_H) : 0;
    drawDocView();
  }
  drawDocFooter();
}

static void drawQR(const String &text, int x, int y, int maxPx) {
  static uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(10)];
  static uint8_t tmpb[qrcodegen_BUFFER_LEN_FOR_VERSION(10)];
  if (!qrcodegen_encodeText(text.c_str(), tmpb, qr, qrcodegen_Ecc_MEDIUM, 1, 10, qrcodegen_Mask_AUTO, true)) return;
  int size = qrcodegen_getSize(qr);
  int scale = maxPx / (size + 4);
  int px = (size + 4) * scale;
  int ox = x + (maxPx - px) / 2, oy = y;
  tft.fillRect(ox, oy, px, px, TFT_WHITE);
  for (int r = 0; r < size; r++)
    for (int c = 0; c < size; c++)
      if (qrcodegen_getModule(qr, c, r)) tft.fillRect(ox + (c + 2) * scale, oy + (r + 2) * scale, scale, scale, TFT_BLACK);
}

static void drawWifi() {
  btns.clear();
  tft.fillScreen(C_BG);
  drawHeader("เชื่อมต่อโทรศัพท์", true);
  int y = CONTENT_Y + (SCR_W < 300 ? 6 : 10);
  if (wifiOn) {
    int qrMax = (SCR_W < 300) ? 100 : 180;
    tft.setTextFont(2);
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(C_MUTED);
    tft.drawString("สแกนเพื่อเชื่อมต่อโทรศัพท์", SCR_W / 2, y);
    drawQR(webQRText(wcfg), (SCR_W - qrMax) / 2, y + 28, qrMax);
    y += qrMax + 32;
    tft.setTextDatum(TL_DATUM);
    if (SCR_W < 300) tft.setTextFont(2); else tft.setFreeFont(&FreeSans9pt7b);
    int col1W = (SCR_W < 300) ? 76 : 112;
    int lineGap = 27;
    tft.setTextColor(C_MUTED);  tft.drawString(fitText("เครือข่าย",col1W-18), 12, y);
    tft.setTextColor(C_TEXT);   tft.drawString(fitText(wcfg.ssid, SCR_W - col1W - 8), col1W, y);
    y += lineGap;
    tft.setTextColor(C_MUTED);  tft.drawString("รหัสผ่าน", 12, y);
    tft.setTextColor(C_TEXT);   tft.drawString(fitText(wcfg.password.length() >= 8 ? wcfg.password : String("ไม่มีรหัสผ่าน"),SCR_W-col1W-8), col1W, y);
    y += lineGap;
    tft.setTextColor(C_MUTED);  tft.drawString("ที่อยู่เว็บ", 12, y);
    tft.setTextColor(C_ACCENT); tft.drawString("http://" + webIP(), col1W, y);
    y += lineGap;
    tft.setTextColor(C_MUTED);  tft.drawString("โทรศัพท์", 12, y);
    tft.setTextColor(C_TEXT);   tft.drawString(String(webClientCount()) + " เครื่อง", col1W, y);
  } else {
    if (SCR_W < 300) tft.setTextFont(2); else tft.setFreeFont(&FreeSans9pt7b);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_MUTED);
    tft.drawString("ฮอตสปอต Wi-Fi ปิดอยู่", SCR_W / 2, CONTENT_Y + (SCR_W < 300 ? 90 : 140));
    tft.setTextFont(2);
    tft.drawString("เปิดเพื่ออ่านคู่มือผ่านโทรศัพท์", SCR_W / 2, CONTENT_Y + (SCR_W < 300 ? 116 : 166));
  }
  FootDef f[] = {{B_BACK, "กลับ", I_LEFT, true}, {B_WIFI_TOGGLE, wifiOn ? "ปิด Wi-Fi" : "เปิด Wi-Fi", I_NONE, true}};
  drawFooter(f, 2);
  lastClients = webClientCount();
}

static int settingRowH() { return (SCR_W < 300) ? 32 : 48; }

static void drawSettingRow(int y, const char *name, const String &value) {
  int rh = settingRowH();
  if (SCR_W < 300) tft.setTextFont(2); else tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(C_TEXT);
  tft.drawString(fitText(name,SCR_W-((SCR_W<300)?98:116)-(value.length()?36:0)), 12, y + rh / 2);
  tft.setTextColor(C_ACCENT);
  tft.setTextDatum(MR_DATUM);
  int btnW = (SCR_W < 300) ? 80 : 98;
  if (value.length()) tft.drawString(value, SCR_W - btnW - 14, y + rh / 2);
  tft.drawFastHLine(8, y + rh - 1, SCR_W - 16, C_LINE);
}

static void drawSettings() {
  btns.clear();
  tft.fillScreen(C_BG);
  drawHeader("ตั้งค่า", true); addBtn(SCR_W-70,2,68,HEADER_H-4,B_WHERE,"GPS");
  int y = CONTENT_Y + 2;
  int rowStep = settingRowH();
  int btnH = rowStep - 4;
  int btnW = (SCR_W < 300) ? 80 : 98;
  int halfBtnW = (btnW - 6) / 2;

  drawSettingRow(y, "ความสว่าง", String(brightness * 100 / 255) + "%");
  addBtn(SCR_W - btnW - 6, y + 2, halfBtnW, btnH, B_BRIGHT_DN, "-");
  addBtn(SCR_W - halfBtnW - 4, y + 2, halfBtnW, btnH, B_BRIGHT_UP, "+");
  y += rowStep;

  drawSettingRow(y, "พักจอหลัง", "");
  addBtn(SCR_W - btnW - 6, y + 2, btnW, btnH, B_TIMEOUT, TIMEOUT_NAMES[timeoutIdx]);
  y += rowStep;

  drawSettingRow(y, "กลับสีจอ", "");
  addBtn(SCR_W - btnW - 6, y + 2, btnW, btnH, B_INVERT, invertColors ? "เปิด" : "ปิด");
  y += rowStep;

#if PIN_LDR >= 0
  drawSettingRow(y, "หรี่จออัตโนมัติ", "");
  addBtn(SCR_W - btnW - 6, y + 2, btnW, btnH, B_NIGHT, nightAuto ? "เปิด" : "ปิด");
  y += rowStep;
#endif

  drawSettingRow(y, "เทียบสัมผัส", "");
  addBtn(SCR_W - btnW - 6, y + 2, btnW, btnH, B_CALIB, "ทำใหม่");
  y += rowStep;

  drawSettingRow(y, "สำรองลง SD", "");
  addBtn(SCR_W - btnW - 6, y + 2, halfBtnW, btnH, B_BACKUP_SAVE, "บันทึก");
  addBtn(SCR_W - halfBtnW - 4, y + 2, halfBtnW, btnH, B_BACKUP_LOAD, "โหลด");
  y += rowStep;

  tft.setTextFont(SCR_W < 300 ? 1 : 2);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(C_MUTED);
  uint32_t totalMB = SD.totalBytes() / (1024 * 1024), usedMB = SD.usedBytes() / (1024 * 1024);
  int lineH = tft.lineHeight();
  int shownCats = nCats - (goodboyIdx >= 0 ? 1 : 0);
  tft.drawString("คู่มือเอาตัวรอด " FW_VERSION, 12, y);
  tft.drawString(String(nArticles) + " เรื่อง ใน " + shownCats + " หมวด", 12, y + lineH);
  if(y+lineH*3<=SCR_H-FOOTER_H)tft.drawString("การ์ด SD: " + String(usedMB) + " MB จาก " + totalMB + " MB", 12, y + lineH * 2);
  if (gpsPresent() && y + lineH * 4 <= SCR_H - FOOTER_H) {
    GpsFix g = gpsGet();
    String gl = g.sentences == 0 ? String("GPS: ไม่มีข้อมูล (ตรวจสาย)")
              : g.valid ? "GPS: " + String(g.lat, 5) + ", " + String(g.lon, 5) + (g.phone ? String(" (โทรศัพท์)") : " (" + String(g.sats) + " ดาวเทียม)")
              : "GPS: กำลังค้นหา " + String(g.sats) + " ดาวเทียม";
    tft.drawString(gl, 12, y + lineH * 3);
  }
  FootDef f[] = {{B_BACK, "กลับ", I_LEFT, true},{B_INFO,"ข้อมูล",I_NONE,true}};
  drawFooter(f, 2);
}
static void drawInfo() {
  btns.clear();tft.fillScreen(C_BG);drawHeader("ข้อมูลระบบ",true);
  tft.setTextFont(2);tft.setTextDatum(TL_DATUM);tft.setTextColor(C_TEXT);
  String info[]={String("รุ่น ")+FW_VERSION,BOARD_NAME,
    String(nArticles)+" เรื่อง / "+String(nCats-(goodboyIdx>=0?1:0))+" หมวด",
    String("SD ")+String(SD.usedBytes()/(1024*1024))+" / "+String(SD.totalBytes()/(1024*1024))+" MB",
    String("GPS: ")+(gpsGet().valid?String(gpsGet().lat,5)+", "+String(gpsGet().lon,5):String(gpsGet().sats)+" ดาวเทียม"),
    String("RAM ว่าง ")+String(ESP.getFreeHeap()/1024)+" KB"};
  for(int i=0;i<6;i++)tft.drawString(info[i],8,CONTENT_Y+4+i*32);
  FootDef f[]={{B_BACK,"กลับ",I_LEFT,true}};drawFooter(f,1);
}
static void drawNoSD() {
  btns.clear();
  tft.fillScreen(C_BG);
  drawHeader("คู่มือเอาตัวรอด", false);
  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_ACCENT);
  tft.drawString("ไม่พบการ์ด SD", SCR_W / 2, CONTENT_Y + (SCR_W < 300 ? 70 : 110));
  tft.setTextFont(2);
  tft.setTextColor(C_MUTED);
  tft.drawString("ใส่การ์ด microSD แบบ FAT32", SCR_W / 2, CONTENT_Y + (SCR_W < 300 ? 100 : 150));
  tft.drawString("พร้อมไฟล์คู่มือ แล้วแตะ ลองใหม่", SCR_W / 2, CONTENT_Y + (SCR_W < 300 ? 130 : 180));
  // The flashlight and mesh messaging don't need the card, so keep them reachable.
  FootDef f[] = {{B_RETRY, "ลองใหม่", I_NONE, true}, {B_LIGHT, "ไฟ", I_NONE, true}, {B_MESH, "เมช", I_NONE, true}};
  drawFooter(f, 3);
}

// ---------------------------------------------------------------- mesh messaging screens
// Greedy word wrap of s into at most `lines` lines no wider than maxW pixels (current font).
static int wrapLines(const String &s, int maxW, String *out, int lines) {
  int n=0,pos=0,len=s.length();const char *b=s.c_str();
  while(pos<len&&n<lines) {
    while(pos<len&&b[pos]==' ')pos++;
    if(pos>=len)break;
    int p=pos,fit=pos,br=-1;
    while(p<len) {
      int e=utf8ClusterEnd(b,len,p);
      if(tft.textWidth(s.substring(pos,e))>maxW)break;
      fit=e;int q=p;uint32_t cp=utf8Decode(b,len,q);
      if(cp==' '||cp==0x200B||cp=='-')br=e;
      p=e;
    }
    if(fit<len&&br>pos)fit=br;
    if(fit==pos)fit=utf8ClusterEnd(b,len,pos);
    out[n]=s.substring(pos,fit);out[n].trim();n++;pos=fit;
  }
  if(pos<len&&n)out[n-1]=fitText(out[n-1]+s.substring(pos),maxW);
  return n;
}

static String ageText(uint32_t ms) {
  uint32_t s = (millis() - ms) / 1000;
  if (s < 60) return "เมื่อครู่";
  if (s < 3600) return String(s / 60) + "น.";
  return String(s / 3600) + "ชม.";
}

static void drawMesh() {
  btns.clear();
  tft.fillScreen(C_BG);
  drawHeader("เมช", true);
  int y = CONTENT_Y + 3;
  tft.setTextFont(2);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(meshActive() ? C_ACCENT : C_MUTED);
  String st = String(meshName()) + (meshActive() ? "  " + String(meshPeersHeard()) + " เครื่อง" : "  ปิด");
  tft.drawString(fitText(st, SCR_W - 76), 8, y + 11);
  addBtn(SCR_W - 66, y, 60, 22, B_MESH_TOGGLE, meshActive() ? "ปิด" : "เปิด");
  int top = y + 28, bottom = SCR_H - FOOTER_H - 2;
  int rh = (SCR_W < 300) ? 58 : 62;
  int rows = (bottom - top) / rh;
  int n = meshCount();
  if (!meshActive()) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_MUTED);
    tft.drawString("เมชปิดอยู่ แตะ เปิด เพื่อคุย", SCR_W / 2, top + 40);
    tft.drawString("กับบอร์ดใกล้เคียง", SCR_W / 2, top + 70);
  } else if (n == 0) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_MUTED);
    tft.drawString("ยังไม่มีข้อความ", SCR_W / 2, top + 40);
    tft.drawString("แตะ ด่วน หรือ พิมพ์ เพื่อส่ง", SCR_W / 2, top + 70);
  }
  int first = max(0, n - rows);
  int ry = top;
  for (int i = first; i < n; i++) {
    const MeshMsg &m = meshAt(i);
    bool me = m.mine && strcmp(m.from, meshName()) == 0;       // sent from a phone: show the phone's name
    String line = (me ? String("ฉัน") : String(m.from)) + ": " + m.text;
    String ln[2];
    int cnt = wrapLines(line, SCR_W - 72, ln, 2);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(m.mine ? C_ACCENT : C_TEXT);
    for (int k = 0; k < cnt; k++) tft.drawString(ln[k], 8, ry + 1 + k * 27);
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(C_MUTED);
    tft.drawString(ageText(m.ms), SCR_W - 6, ry + 2);
    if (m.mine) {                                              // like "Read": how many boards said they got it
      tft.setTextFont(1);
      tft.setTextColor(m.ackN ? NC(0x07E0) : C_MUTED);
      tft.drawString(m.ackN ? "รับแล้ว " + String(m.ackN) : String("ส่งแล้ว"), SCR_W - 6, ry + 30);
      tft.setTextFont(2);
    }
    tft.drawFastHLine(8, ry + rh - 2, SCR_W - 16, C_LINE);
    ry += rh;
  }
  FootDef f[] = {{B_BACK, "กลับ", I_LEFT, true}, {B_MESH_QUICK, "ด่วน", I_NONE, meshActive()},
                 {B_MESH_TYPE, "พิมพ์", I_NONE, meshActive()}, {B_MESH_NAME, "ชื่อ", I_NONE, true},
                 {B_MESH_NEAR, "ใกล้", I_NONE, meshActive()}};   // (radar button lives on the Near screen)
  drawFooter(f, 5);
}

// Nearby boards with signal bars and a rough distance. Signal strength is only a guess at range.
static String distText(int rssi) {
  float d = meshDistanceM(rssi);
  if (d < 0) return "ไม่ทราบ";                              // no signal reading (hotspot on, or only heard via a relay)
  if (d < 10) return "~" + String((int)(d + 0.5f)) + " ม.";
  if (d < 100) return "~" + String((int)(d / 5 + 0.5f) * 5) + " ม.";
  return "~" + String((int)(d / 10 + 0.5f) * 10) + " ม.";
}

static void drawNear() {
  btns.clear();
  tft.fillScreen(C_BG);
  drawHeader("บอร์ดใกล้เคียง", true);
  MeshPeer pl[8];
  int n = meshPeerList(pl, 8);
  int rh = (SCR_W < 300) ? 54 : 58;
  int y = CONTENT_Y + 6;
  int perPage=max(1,(CONTENT_H-38)/rh);
  int pages=max(1,(n+perPage-1)/perPage);
  nearPage=constrain(nearPage,0,pages-1);
  tft.setTextFont(2);
  if (n == 0) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_MUTED);
    tft.drawString("ยังไม่พบบอร์ดอื่น", SCR_W / 2, y + 50);
    tft.drawString("บอร์ดส่งสัญญาณทุก 30 วินาที", SCR_W / 2, y + 82);
  }
  for (int i = nearPage*perPage; i < min(n,(nearPage+1)*perPage); i++) {
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(C_TEXT);
    tft.drawString(fitText(pl[i].name, SCR_W / 2 - 40), 10, y + 14);
    tft.setTextColor(C_MUTED);
    tft.setTextFont(1);
    tft.drawString(fitText(String(pl[i].rssi ? String((int)pl[i].rssi) + " dBm" : String("-")) + "  " +
                   (pl[i].ageS < 5 ? String("เมื่อครู่") : String(pl[i].ageS) + " วินาทีที่แล้ว"),SCR_W-20), 10, y + 38);
    tft.setTextFont(2);
    // signal bars
    int bars = meshBars(pl[i].rssi);
    int bx = SCR_W / 2 + 6;
    for (int b = 0; b < 4; b++) {
      int bh = 6 + b * 5;
      uint16_t col = (b < bars) ? (bars >= 3 ? NC(0x07E0) : (bars == 2 ? NC(0xFFE0) : C_ACCENT)) : C_LINE;
      tft.fillRect(bx + b * 8, y + 26 - bh, 6, bh, col);
    }
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(C_ACCENT);
    tft.drawString(distText(pl[i].rssi), SCR_W - 10, y + 20);
    tft.drawFastHLine(8, y + rh - 3, SCR_W - 16, C_LINE);
    y += rh;
  }
  tft.setTextFont(1);
  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(C_MUTED);
  // The signal listener can't run while the hotspot is on (phones couldn't join), so say why there is no distance.
  tft.drawString(webRunning() ? "ปิด Wi-Fi เพื่อดูระยะทาง" : "ระยะประมาณจากความแรงสัญญาณ",
                 SCR_W / 2, SCR_H - FOOTER_H - 4);
  FootDef f[] = {{B_BACK, "กลับ", I_LEFT, true}, {B_PREV, "", I_LEFT, nearPage>0},
    {B_NEXT,"",I_RIGHT,nearPage+1<pages}, {B_RADAR, "เรดาร์", I_NONE, gpsPresent()}};
  drawFooter(f, 4);
}

// ---------------------------------------------------------------- radar (nearby boards that share a GPS position)
static double geoDistM(double la1, double lo1, double la2, double lo2) {
  const double R = 6371000.0, d2r = 0.017453292519943295;
  double dla = (la2 - la1) * d2r, dlo = (lo2 - lo1) * d2r;
  double a = sin(dla / 2) * sin(dla / 2) + cos(la1 * d2r) * cos(la2 * d2r) * sin(dlo / 2) * sin(dlo / 2);
  return 2 * R * atan2(sqrt(a), sqrt(1 - a));
}
static double geoBearing(double la1, double lo1, double la2, double lo2) {   // degrees clockwise from north
  const double d2r = 0.017453292519943295;
  double dlo = (lo2 - lo1) * d2r;
  double y = sin(dlo) * cos(la2 * d2r);
  double x = cos(la1 * d2r) * sin(la2 * d2r) - sin(la1 * d2r) * cos(la2 * d2r) * cos(dlo);
  double b = atan2(y, x) / d2r;
  return b < 0 ? b + 360 : b;
}
static String compass8(double b) {
  static const char *N[] = {"เหนือ", "อีสาน", "ออก", "อาคเนย์", "ใต้", "หรดี", "ตก", "พายัพ"};
  return N[(int)((b + 22.5) / 45) % 8];
}
static String metersText(double m) { return m < 1000 ? String((int)m) + " ม." : String(m / 1000.0, m < 10000 ? 1 : 0) + " กม."; }

static void drawRadar() {
  btns.clear();
  tft.fillScreen(C_BG);
  drawHeader("เรดาร์", true);
  GpsFix g = gpsGet();
  MeshPeer pl[8];
  int n = meshPeerList(pl, 8);
  int listH = 96;
  int R = min(SCR_W / 2 - 12, (CONTENT_H - listH - 14) / 2);
  int cx = SCR_W / 2, cy = CONTENT_Y + 8 + R;
  tft.drawCircle(cx, cy, R, C_LINE); tft.drawCircle(cx, cy, R / 2, C_LINE);
  tft.drawFastHLine(cx - R, cy, 2 * R + 1, C_LINE); tft.drawFastVLine(cx, cy - R, 2 * R + 1, C_LINE);
  tft.setTextFont(1); tft.setTextDatum(TC_DATUM); tft.setTextColor(C_MUTED);
  tft.drawString("เหนือ", cx, cy - R + 2);
  if (!g.valid) {
    tft.setTextFont(2); tft.setTextDatum(MC_DATUM); tft.setTextColor(C_ACCENT);
    tft.drawString(PIN_GPS_RX < 0 ? "รอพิกัดจากโทรศัพท์" : (g.sentences == 0 ? "ไม่มีข้อมูล GPS" : "รอ GPS ระบุตำแหน่ง"), cx, cy - 18);
    tft.setTextColor(C_MUTED);
    tft.drawString(PIN_GPS_RX < 0 ? "เปิดหน้าแผนที่ในโทรศัพท์ไว้" : (g.sentences == 0 ? "ตรวจสอบสายเชื่อมต่อ" : String(g.sats) + " ดาวเทียม ควรออกกลางแจ้ง"), cx, cy + 12);
  } else {
    double maxD = 0;
    double dist[8], brg[8];
    for (int i = 0; i < n; i++) {
      if (!pl[i].hasPos) continue;
      dist[i] = geoDistM(g.lat, g.lon, pl[i].lat, pl[i].lon); brg[i] = geoBearing(g.lat, g.lon, pl[i].lat, pl[i].lon);
      if (dist[i] > maxD) maxD = dist[i];
    }
    static const int STEPS[] = {100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000, 100000};
    int ring = STEPS[9];
    for (int s : STEPS) if (maxD <= s) { ring = s; break; }
    tft.fillCircle(cx, cy, 4, NC(0x07E0));                         // you
    tft.setTextFont(1); tft.setTextDatum(TL_DATUM);
    for (int i = 0; i < n; i++) {
      if (!pl[i].hasPos) continue;
      double r = min(dist[i], (double)ring) / ring * R, a = brg[i] * 0.017453292519943295;
      int px = cx + (int)(r * sin(a)), py = cy - (int)(r * cos(a));
      tft.fillCircle(px, py, 4, C_ACCENT);
      tft.setTextColor(C_TEXT);
      tft.drawString(fitText(pl[i].name, 48), constrain(px + 6, 2, SCR_W - 50), constrain(py - 4, CONTENT_Y + 2, cy + R));
    }
    tft.setTextDatum(BR_DATUM); tft.setTextColor(C_MUTED);
    tft.drawString("รัศมี " + metersText(ring), SCR_W - 4, cy + R);
    // nearest sharing boards, written out
    int shown = 0, ly = cy + R + 8;
    tft.setTextFont(2); tft.setTextDatum(TL_DATUM);
    for (int pass = 0; pass < 3; pass++) {
      int best = -1;
      for (int i = 0; i < n; i++) if (pl[i].hasPos && dist[i] >= 0 && (best < 0 || dist[i] < dist[best])) best = i;
      if (best < 0) break;
      tft.setTextColor(C_TEXT);
      tft.drawString(fitText(pl[best].name, SCR_W / 2 - 10) + "  " + metersText(dist[best]) + " " + compass8(brg[best]), 8, ly);
      dist[best] = -1; ly += 27; shown++;
    }
    if (!shown) { tft.setTextColor(C_MUTED); tft.drawString(n ? "ยังไม่มีใครแชร์พิกัด" : "ยังไม่พบบอร์ดอื่น", 8, ly); }
  }
  FootDef f[] = {{B_BACK, "กลับ", I_LEFT, true}, {B_GPS_SHARE, gpsShare ? "แชร์:เปิด" : "แชร์:ปิด", I_NONE, true}};
  drawFooter(f, 2);
}

static void drawQuick() {
  static const char *QUICK[] = {"ปลอดภัย", "ขอความช่วยเหลือ", "กำลังออกเดินทาง", "อยู่ที่พัก", "พบน้ำแล้ว", "มาหาฉัน"};
  btns.clear();
  tft.fillScreen(C_BG);
  drawHeader("ข้อความด่วน", true);
  int y = CONTENT_Y + 4;
  for (int i = 0; i < 6; i++) {
    addBtn(8, y, SCR_W - 16, ROW_H - 4, B_QUICK_BASE + i, QUICK[i]);
    y += ROW_H;
  }
  FootDef f[] = {{B_BACK, "กลับ", I_LEFT, true}};
  drawFooter(f, 1);
}

#define KB_MAX_MSG  (MESH_TEXT_LEN - 1)
static int kbMax() { return kbTarget == 3 ? 160 : kbTarget == 2 ? 96 : kbTarget == 0 ? KB_MAX_MSG : MESH_NAME_LEN - 1; }

static void drawKbText() {
  int bx = 6, by = CONTENT_Y + 4, bw = SCR_W - 12, bh = 84;
  tft.fillRoundRect(bx, by, bw, bh, 6, C_PANEL);
  tft.drawRoundRect(bx, by, bw, bh, 6, C_LINE);
  tft.setTextFont(2);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(C_TEXT);
  String ln[6];
  int cnt = wrapLines(kbBuf + "_", bw - 12, ln, 6);
  int first=max(0,cnt-2); // keep the cursor/latest input visible for long messages
  for (int k = first; k < cnt; k++) tft.drawString(ln[k], bx + 6, by + 1 + (k-first) * 28);
  tft.setTextDatum(BR_DATUM);
  tft.setTextColor(C_MUTED);
  tft.setTextFont(1);
  tft.drawString(String(kbBuf.length()) + "/" + kbMax() + " ไบต์", bx + bw - 4, by + bh - 2);
}

static void drawKeys() {
  static const char *ROWS[] = {"1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm"};
  btns.clear();
  tft.fillScreen(C_BG);
  drawHeader(kbTarget == 3 ? "ข้อความ LoRa" : kbTarget == 2 ? "ค้นหาสถานที่" : kbTarget == 0 ? "ข้อความใหม่" : "ชื่อของคุณ", true);
  drawKbText();
  int kw = SCR_W / 10;
  int top = CONTENT_Y + 94;
  int kh = (SCR_H - FOOTER_H - 4 - top) / 5;
  if(kbThai) {
    for (int row = 0; row < 4; row++) {
      const char *keys = KEDMANEE[kbShift ? 1 : 0][row];
      int byteCount = strlen(keys), pos = 0, columns = 0;
      while (pos < byteCount) { utf8Decode(keys, byteCount, pos); columns++; }
      int keyWidth = SCR_W / 13;
      int rowWidth = keyWidth * columns;
      int startX = row == 0 || row == 1 ? 0 : (SCR_W - rowWidth)/2;
      pos = 0;
      for (int col = 0; col < columns; col++) {
        int start = pos;
        uint32_t cp = utf8Decode(keys, byteCount, pos);
        String label = String(keys+start).substring(0, pos-start);
        if (thaiMark(cp)) label = String("ก") + label;
        int width = col == columns-1 && row == 1 ? SCR_W-startX-col*keyWidth : keyWidth;
        addBtn(startX+col*keyWidth, top+row*kh, width, kh, B_KEY_BASE+cp, label);
      }
    }
  } else {
    for(int r=0;r<4;r++) {
      int len=strlen(ROWS[r]),x0=(SCR_W-len*kw)/2;
      if(r==3)x0=kw/2;
      for(int c=0;c<len;c++) {
        char ch=ROWS[r][c];String label=String(kbShift&&isalpha(ch)?(char)toupper(ch):ch);
        addBtn(x0+c*kw,top+r*kh,kw,kh,B_KEY_BASE+ch,label);
      }
      if(r==3)addBtn(x0+len*kw,top+r*kh,SCR_W-(x0+len*kw),kh,B_KB_DEL,"<-");
    }
  }
  int y=top+4*kh;
  addBtn(0,y,kw*2,kh,B_KB_SHIFT,kbShift?"Shift*":"Shift");
  addBtn(kw*2,y,kw*4,kh,B_KEY_BASE+' ',"เว้นวรรค");
  addBtn(kw*6,y,kw*2,kh,B_KB_LANG,kbThai?"EN":"ไทย");
  addBtn(kw*8,y,SCR_W-kw*8,kh,B_KB_DEL,"ลบ");
  FootDef f[] = {{B_BACK, "ยกเลิก", I_NONE, true}, {B_KB_SEND, kbTarget == 2 ? "ค้นหา" : kbTarget == 0 ? "ส่ง" : "บันทึก", I_NONE, true}};
  drawFooter(f, 2);
}

// ---------------------------------------------------------------- offline map viewer
// Tiles live on the SD card as /maps/{zoom}/{x}/{y}.jpg (256x256, standard "slippy map" layout),
// made by scripts/get-map-tiles.ps1. /maps/center.txt holds: lat lon startZoom minZoom maxZoom.
static double mapWorld(int z) { return 256.0 * (double)(1UL << z); }

static void mapLatLonToPx(double lat, double lon, int z, int32_t &px, int32_t &py) {
  if(!isfinite(lat)||!isfinite(lon)){px=py=0;return;}
  lat=constrain(lat,-85.05112878,85.05112878);lon=constrain(lon,-180.0,180.0);
  double n = mapWorld(z), r = lat * M_PI / 180.0;
  px = (int32_t)constrain((lon + 180.0) / 360.0 * n,0.0,n-1);
  py = (int32_t)constrain((1.0 - log(tan(r) + 1.0 / cos(r)) / M_PI) / 2.0 * n,0.0,n-1);
}

static void mapPxToLatLon(int32_t px, int32_t py, int z, double &lat, double &lon) {
  double n = mapWorld(z);
  lon = px / n * 360.0 - 180.0;
  lat = atan(sinh(M_PI * (1.0 - 2.0 * py / n))) * 180.0 / M_PI;
}

static bool mapLoadCenter() {
  mapReady = false;
  mapOpenData = false;
  mapLocalData = false;
  if (!sdUp) return false;
  File f = SD.open(mapStoragePath("center.txt"), FILE_READ);
  if (!f) return false;
  String l = readLine(f);
  f.close();
  double lat, lon;
  int z, zmin, zmax;
  if (sscanf(l.c_str(), "%lf %lf %d %d %d", &lat, &lon, &z, &zmin, &zmax) != 5) return false;
  if(!isfinite(lat)||!isfinite(lon)||fabs(lat)>85.05112878||fabs(lon)>180)return false;
  mapMinZ = constrain(zmin, 0, 18);
  mapMaxZ = constrain(zmax, mapMinZ, 18);
  mapZ = constrain(z, mapMinZ, mapMaxZ);
  mapLatLonToPx(lat, lon, mapZ, mapCx, mapCy);
  // The downloader writes a small source.json manifest. Read the provider
  // once per map load; do not allocate a JSON parser or read SD on every draw.
  File source = SD.open(mapStoragePath("source.json"), FILE_READ);
  if (source && !source.isDirectory() && source.size() <= 4096) {
    String metadata = source.readString();
    int p = metadata.indexOf("\"provider\"");
    if (p >= 0) {
      p += 10;
      while (p < (int)metadata.length() && isspace((unsigned char)metadata[p])) p++;
      if (p < (int)metadata.length() && metadata[p++] == ':') {
        while (p < (int)metadata.length() && isspace((unsigned char)metadata[p])) p++;
        mapLocalData = metadata.substring(p, p + 11) == "\"local-osm\"";
        mapOpenData = mapLocalData || metadata.substring(p, p + 13) == "\"openfreemap\"";
      }
    }
  }
  if (source) source.close();
  mapReady = true;
  return true;
}

// ---- waypoints (SD: /maps/waypoints.txt) and GPS track (SD: /maps/track.csv) ----
struct Waypoint { char name[12]; double lat, lon; };
static Waypoint wps[16];
static int nWps = 0;
static bool wpDelMode = false;
static uint32_t wpClearMs = 0, trkClearMs = 0;      // first tap arms "Clear", second tap within 3 s does it
static const int TRK_MAX = 200;
struct TrkPt { float lat, lon; };
static TrkPt trk[TRK_MAX];
static int trkN = 0;                                // points held (oldest first, shifted out when full)
static bool trkLoaded = false;
static char mapToast[96] = "";
static uint32_t mapToastMs = 0;

static int wpMax() { return min<int>(16, ROWS_PER_PAGE); }

static void wpLoad() {
  nWps = 0;
  if (!sdUp) return;
  String path = mapStoragePath("waypoints.txt");
  File f = SD.open(path, FILE_READ);
  if (!f) f = SD.open(path + ".bak", FILE_READ);
  if (!f) return;
  while (f.available() && nWps < wpMax()) {
    String l = readLine(f);
    String p[3];
    if (!l.length() || splitTabs(l, p, 3) < 3) continue;
    Waypoint &w = wps[nWps];
    utf8Copy(w.name,p[0].c_str(),sizeof(w.name));
    w.lat = p[1].toDouble(); w.lon = p[2].toDouble();
    nWps++;
  }
  f.close();
}

static bool wpSave() {
  if (!sdUp) return false;
  String path = mapStoragePath("waypoints.txt");
  String temp = path + ".tmp";
  String backup = path + ".bak";
  File f = SD.open(temp, FILE_WRITE);
  if (!f) return false;
  bool complete = true;
  for (int i = 0; i < nWps; i++) {
    char row[64];
    int n = snprintf(row, sizeof(row), "%s\t%.6f\t%.6f\n", wps[i].name, wps[i].lat, wps[i].lon);
    if (n <= 0 || n >= (int)sizeof(row) || f.write((const uint8_t *)row, n) != (size_t)n) {
      complete = false;
      break;
    }
  }
  f.close();
  if (!complete) { SD.remove(temp); return false; }
  if (SD.exists(path)) {
    if (SD.exists(backup) && !SD.remove(backup)) return false;
    if (!SD.rename(path, backup)) return false;
  }
  if (!SD.rename(temp, path)) {
    if (SD.exists(backup)) SD.rename(backup, path);
    return false;
  }
  if (SD.exists(backup)) SD.remove(backup);
  return true;
}

static void trkLoad() {
  trkLoaded = true;
  trkN = 0;
  if (!sdUp) return;
  File f = SD.open(mapStoragePath("track.csv"), FILE_READ);
  if (!f) return;
  while (f.available()) {
    String l = readLine(f);
    float a, b;
    if (sscanf(l.c_str(), "%f,%f", &a, &b) != 2) continue;
    if (trkN == TRK_MAX) { memmove(trk, trk + 1, sizeof(TrkPt) * (TRK_MAX - 1)); trkN--; }
    trk[trkN++] = {a, b};
  }
  f.close();
}

static void trkClear() {
  trkN = 0;
  if (sdUp) SD.remove(mapStoragePath("track.csv"));
}

// Called from loop(): drop a breadcrumb every 15 m of travel while the GPS has a fix.
static void trkLoop(uint32_t now) {
  static uint32_t next = 0;
  if ((int32_t)(now - next) < 0) return;
  next = now + 5000;
  GpsFix g = gpsGet();
  if (!g.valid || !mapReady) return;
  if (!trkLoaded) trkLoad();
  if (trkN && geoDistM(trk[trkN - 1].lat, trk[trkN - 1].lon, g.lat, g.lon) < 15.0) return;
  if (trkN == TRK_MAX) { memmove(trk, trk + 1, sizeof(TrkPt) * (TRK_MAX - 1)); trkN--; }
  trk[trkN++] = {(float)g.lat, (float)g.lon};
  if (sdUp) {
    File f = SD.open(mapStoragePath("track.csv"), FILE_APPEND);
    if (f) {
      if (f.size() < 150000) f.printf("%.6f,%.6f\n", g.lat, g.lon);
      f.close();
    }
  }
}

static bool mapToScreen(double lat, double lon, int &sx, int &sy) {
  int32_t px, py;
  mapLatLonToPx(lat, lon, mapZ, px, py);
  sx = px - (mapCx - SCR_W / 2);
  sy = CONTENT_Y + py - (mapCy - CONTENT_H / 2);
  return sx >= -6 && sx < SCR_W + 6 && sy >= CONTENT_Y - 6 && sy < CONTENT_Y + CONTENT_H + 6;
}

static void mapDrawLabel(const char *s, int x, int y, uint16_t col) {
  tft.setTextFont(1);
  tft.setTextDatum(TL_DATUM);
  String label=fitText(s,SCR_W-4);
  int w = tft.textWidth(label) + 4;
  int h=tft.lineHeight()+2;
  x = constrain(x, 0, SCR_W - w);
  y = constrain(y, CONTENT_Y, CONTENT_Y + CONTENT_H - h);
  tft.fillRect(x, y, w, h, C_BG);
  tft.setTextColor(col);
  tft.drawString(label, x + 2, y + 1);
}

// Clip route and track segments to the map rectangle so long segments cannot
// draw over the header/footer. North remains up; bearing is direction of travel.
static void mapLine(int x0,int y0,int x1,int y1,uint16_t color) {
  double t0=0,t1=1,dx=x1-x0,dy=y1-y0;
  double pp[4]={-dx,dx,-dy,dy},qq[4]={(double)x0,(double)(SCR_W-1-x0),(double)(y0-CONTENT_Y),(double)(CONTENT_Y+CONTENT_H-1-y0)};
  for(int i=0;i<4;i++){if(pp[i]==0){if(qq[i]<0)return;}else{double r=qq[i]/pp[i];if(pp[i]<0)t0=max(t0,r);else t1=min(t1,r);if(t0>t1)return;}}
  tft.drawLine(x0+t0*dx,y0+t0*dy,x0+t1*dx,y0+t1*dy,color);
}
static void mapDrawOverlays() {
  int sx, sy, px = 0, py = 0;
  bool prev = false;
  uint16_t tcol = NC(TFT_CYAN);
  for (int i = 0; i < trkN; i++) {                      // breadcrumb trail
    bool on = mapToScreen(trk[i].lat, trk[i].lon, sx, sy);
    if (prev && (on || (px >= 0 && px < SCR_W)) && abs(sx - px) < 400 && abs(sy - py) < 400) {
      mapLine(px, py, sx, sy, tcol);
    } else if (on) {
      tft.drawPixel(sx, sy, tcol);
    }
    px = sx; py = sy; prev = true;
  }
  int routeCount=0;const OfflineRoutePoint *route=offlineRoute(routeCount);
  prev=false;
  for(int i=0;i<routeCount;i++){
    mapToScreen(route[i].lat,route[i].lon,sx,sy);
    if(prev&&!route[i].start)mapLine(px,py,sx,sy,NC(TFT_ORANGE));
    px=sx;py=sy;prev=true;
  }
  int reverseCount=0;
  const OfflineRoutePoint *reverse=fieldTrackBacktrack(reverseCount);
  prev=false;
  for(int i=0;i<reverseCount;i++){
    mapToScreen(reverse[i].lat,reverse[i].lon,sx,sy);
    if(prev&&!reverse[i].start)mapLine(px,py,sx,sy,NC(TFT_GREEN));
    px=sx;py=sy;prev=true;
  }
  OfflineDestination dest=offlineDestination();
  if(dest.valid&&mapToScreen(dest.lat,dest.lon,sx,sy)){
    tft.drawCircle(sx,sy,8,NC(TFT_MAGENTA));
    mapDrawLabel(dest.name,sx+9,sy,NC(TFT_MAGENTA));
  }
  for (int i = 0; i < nWps; i++) {                      // waypoints
    if (!mapToScreen(wps[i].lat, wps[i].lon, sx, sy)) continue;
    uint16_t c = NC(TFT_MAGENTA);
    tft.fillTriangle(sx, sy - 6, sx - 5, sy, sx + 5, sy, c);
    tft.fillTriangle(sx, sy + 6, sx - 5, sy, sx + 5, sy, c);
    mapDrawLabel(wps[i].name, sx + 7, sy - 5, NC(TFT_WHITE));
  }
  MeshPeer pl[8];
  int n = meshPeerList(pl, 8);
  for (int i = 0; i < n; i++) {                         // boards that share their position
    if (!pl[i].hasPos || pl[i].posAgeS > 300) continue;
    if (!mapToScreen(pl[i].lat, pl[i].lon, sx, sy)) continue;
    tft.fillCircle(sx, sy, 5, NC(TFT_YELLOW));
    tft.drawCircle(sx, sy, 5, NC(TFT_BLACK));
    mapDrawLabel(pl[i].name, sx + 7, sy - 5, NC(TFT_YELLOW));
  }
  GpsFix g = gpsGet();
  if (g.valid && mapToScreen(g.lat, g.lon, sx, sy)) {   // you
    tft.fillCircle(sx, sy, 5, NC(0x07E0));
    tft.drawCircle(sx, sy, 6, NC(TFT_WHITE));
  }
  if (mapToast[0] && millis() - mapToastMs < 2500) mapDrawLabel(mapToast, 4, CONTENT_Y + 3, NC(TFT_WHITE));
}

static int mapClipX0, mapClipY0, mapClipX1, mapClipY1;
static bool mapJpgOut(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bmp) {
  int x0 = max<int>(x, mapClipX0), y0 = max<int>(y, mapClipY0);
  int x1 = min<int>(x + w, mapClipX1), y1 = min<int>(y + h, mapClipY1);
  if (x1 <= x0 || y1 <= y0) return true;                    // block is outside the map area
  if (gNightUI) for (int i = 0; i < w * h; i++) bmp[i] = NC(bmp[i]);   // keep tiles red at night
  for (int yy = y0; yy < y1; yy++) tft.pushImage(x0, yy, x1 - x0, 1, bmp + (yy - y) * w + (x0 - x));
  return true;
}

static void mapDrawTiles() {
  mapClipX0 = 0; mapClipX1 = SCR_W;
  mapClipY0 = CONTENT_Y; mapClipY1 = CONTENT_Y + CONTENT_H;
  tft.fillRect(0, CONTENT_Y, SCR_W, CONTENT_H, C_PANEL);
  int32_t left = mapCx - SCR_W / 2, top = mapCy - CONTENT_H / 2;
  int tiles = 1 << mapZ;
  int missingTiles=0;
  TJpgDec.setJpgScale(1);
  TJpgDec.setCallback(mapJpgOut);
  for (int ty = (int)floor(top / 256.0); ty <= (int)floor((top + CONTENT_H - 1) / 256.0); ty++) {
    for (int tx = (int)floor(left / 256.0); tx <= (int)floor((left + SCR_W - 1) / 256.0); tx++) {
      if (tx < 0 || ty < 0 || tx >= tiles || ty >= tiles) continue;
      char suffix[40];
      snprintf(suffix, sizeof(suffix), "%d/%d/%d.jpg", mapZ, tx, ty);
      String path = mapStoragePath(suffix);
      if(!SD.exists(path))missingTiles++;
      else TJpgDec.drawFsJpg(tx * 256 - left, CONTENT_Y + ty * 256 - top, path.c_str(), SD);   // a missing tile just stays grey
    }
  }
  mapDrawOverlays();
  // crosshair at the centre and the centre's coordinates
  int cx = SCR_W / 2, cy = CONTENT_Y + CONTENT_H / 2;
  if(!mapFollow){
    tft.drawCircle(cx, cy, 7, NC(TFT_RED));
    tft.drawFastHLine(cx - 11, cy, 23, NC(TFT_RED));
    tft.drawFastVLine(cx, cy - 11, 23, NC(TFT_RED));
  }
  double lat, lon;
  mapPxToLatLon(mapCx, mapCy, mapZ, lat, lon);
  char buf[40];
  snprintf(buf, sizeof(buf), "%.4f, %.4f  z%d", lat, lon, mapZ);
  tft.setTextFont(1);
  tft.setTextDatum(BL_DATUM);
  int creditH = mapOpenData ? (mapLocalData ? 1 : 2) * tft.lineHeight() : 0;
  int bottom = CONTENT_Y + CONTENT_H;
  int tw = tft.textWidth(buf) + 6;
  tft.fillRect(0, bottom - creditH - tft.lineHeight()-4, tw, tft.lineHeight()+4, C_BG);
  tft.setTextColor(C_TEXT);
  tft.drawString(buf, 3, bottom - creditH - 2);
  if (mapOpenData) {
    // Fixed, readable credits remain visible while panning and zooming.
    tft.fillRect(0, bottom - creditH, SCR_W, creditH, C_BG);
    tft.setTextDatum(TL_DATUM);
    tft.drawString("© OpenStreetMap contributors", 3, bottom - creditH);
    if(!mapLocalData)tft.drawString("© OpenMapTiles | OpenFreeMap", 3, bottom - tft.lineHeight());
  }
  // Compact GPS strip, north marker and a scale computed at the map latitude.
  GpsFix g=gpsGet();String gs;
  if(g.valid)gs=(g.phone?String("โทรศัพท์"):String("GPS ")+String(g.sats)+" ดาว")+" "+String(g.speedKmh,0)+" กม./ชม.";
  else gs=g.sentences?String("GPS รอตำแหน่ง ")+String(g.sats)+" ดาว":String("GPS ไม่มีข้อมูล");
  tft.setTextDatum(TL_DATUM);tft.fillRect(0,CONTENT_Y,SCR_W,tft.lineHeight()+4,C_BG);
  tft.setTextColor(g.valid?NC(TFT_GREEN):C_ACCENT);tft.drawString(fitText(gs,SCR_W-30),3,CONTENT_Y+1);
  tft.setTextColor(C_TEXT);tft.drawString("N",SCR_W-19,CONTENT_Y+1);
  double mpp=156543.033928*cos(lat*M_PI/180.0)/pow(2.0,mapZ);
  double desired=mpp*65.0,base=pow(10.0,floor(log10(max(1.0,desired))));
  double meters=(desired/base>=5?5:desired/base>=2?2:1)*base;int length=max(2,(int)(meters/mpp));
  int sy=CONTENT_Y+tft.lineHeight()+10;
  tft.drawFastHLine(5,sy,length,C_TEXT);tft.drawFastVLine(5,sy-3,6,C_TEXT);tft.drawFastVLine(5+length,sy-3,6,C_TEXT);
  String scale=meters>=1000?String(meters/1000,meters<10000?1:0)+" กม.":String((int)meters)+" ม.";
  mapDrawLabel(scale.c_str(),8+length,sy-6,C_TEXT);
  if(missingTiles)mapDrawLabel("พื้นที่นี้ยังขาดแผนที่",4,bottom-creditH-3*tft.lineHeight()-8,C_ACCENT);
  OfflineDestination dest=offlineDestination();
  if(dest.valid&&g.valid){
    double d=geoDistM(g.lat,g.lon,dest.lat,dest.lon),b=geoBearing(g.lat,g.lon,dest.lat,dest.lon);
    String text=(d<1000?String((int)d)+" ม.":String(d/1000,1)+" กม.")+" "+String((int)b)+"° "+String(dest.name);
    mapDrawLabel(text.c_str(),3,bottom-creditH-2*tft.lineHeight()-6,NC(TFT_MAGENTA));
  }
}

static void drawMap() {
  btns.clear();
  tft.fillScreen(C_BG);
  drawHeader("", true);
  tft.setTextFont(1); tft.setTextDatum(ML_DATUM); tft.setTextColor(C_TEXT);
  int nameX=SCR_W<300?38:52;
  tft.drawString(fitText(mapStorageName(), SCR_W-80-nameX-6), nameX, HEADER_H/2);
  // Keep province selection out of the six existing map controls.
  addBtn(SCR_W-80, 2, 78, HEADER_H-4, B_MAP_SETS, "จังหวัด");
  if (!mapReady) {
    tft.setTextFont(2);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_ACCENT);
    tft.drawString("ไม่มีแผนที่ในการ์ด SD", SCR_W / 2, CONTENT_Y + 50);
    tft.setTextColor(C_MUTED);
    tft.drawString("เปิดตัวช่วยแผนที่ทั้งจังหวัด", SCR_W / 2, CONTENT_Y + 80);
    tft.drawString("บนคอมพิวเตอร์ เลือกพื้นที่ไทย", SCR_W / 2, CONTENT_Y + 110);
    tft.drawString("แล้วบันทึกโฟลเดอร์ maps ลง SD", SCR_W / 2, CONTENT_Y + 140);
  } else {
    offlineRouteRefresh();
    mapDrawTiles();
  }
  if (!trkLoaded) trkLoad();
  GpsFix gf = gpsGet();
  FootDef f[] = {{B_BACK, "กลับ", I_LEFT, true}, {B_MAP_OUT, "-", I_NONE, mapReady && mapZ > mapMinZ},
                 {B_MAP_IN, "+", I_NONE, mapReady && mapZ < mapMaxZ},
                 {B_MAP_ME, mapFollow ? "ตาม" : "ฉัน", I_NONE, mapReady && gf.valid},
                 {B_MAP_MARK, "ปัก", I_NONE, mapReady}, {B_MAP_PTS, "จุด", I_NONE, true}};
  drawFooter(f, 6);
}

// Drag to pan: the map jumps to the new spot when you lift your finger.
static void mapTouch(bool down, int tx, int ty) {
  static bool dragging = false;
  static int sx, sy, lx, ly;
  if (down) {
    if (!dragging) {
      if (ty < CONTENT_Y || ty >= CONTENT_Y + CONTENT_H) return;   // started on a button
      dragging = true; sx = tx; sy = ty;
    }
    lx = tx; ly = ty;
  } else if (dragging) {
    dragging = false;
    int dx = lx - sx, dy = ly - sy;
    if (abs(dx) + abs(dy) < 8 || !mapReady) return;
    mapFollow = false;
    double n = mapWorld(mapZ);
    mapCx = constrain((int32_t)(mapCx - dx), 0, (int32_t)n - 1);
    mapCy = constrain((int32_t)(mapCy - dy), 0, (int32_t)n - 1);
    drawMap();
  }
}

// ---------------------------------------------------------------- triage assistant
static void drawTriage() {
  btns.clear();
  tft.fillScreen(C_BG);
  drawHeader("ประเมินเหตุ", true);
  const TriNode &nd = *triCur;
  tft.setTextFont(2);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(C_TEXT);
  String question[3];
  int y=CONTENT_Y+2;
  int count=wrapLines(nd.q,SCR_W-16,question,3);
  for(int i=0;i<count;i++) { tft.drawString(question[i],8,y); y+=tft.lineHeight(); }
  y += 6;
  int avail = SCR_H - FOOTER_H - y - 4;
  int bh = min(SCR_W < 300 ? 40 : 54, avail / max<int>(1, nd.n) - 4);
  for (int i = 0; i < nd.n; i++) addBtn(8, y + i * (bh + 4), SCR_W - 16, bh, B_TRI_BASE + i, nd.o[i].label);
  FootDef f[] = {{B_BACK, triDepth ? "กลับ" : "หน้าแรก", I_LEFT, true}};
  drawFooter(f, 1);
}

static String triTitle(const String &id) {
  File f = SD.open(F_CATALOG, FILE_READ);
  String title = id;
  if (!f) return title;
  while (f.available()) {
    String l = readLine(f);
    String p[3];
    if (l.length() && l[0] != '#' && splitTabs(l, p, 3) >= 3 && p[0] == id) { title = asciiOnly(p[2]); break; }
  }
  f.close();
  return title;
}

static void triPick(int i) {
  if (i < 0 || i >= triCur->n) return;
  const char *to = triCur->o[i].to;
  if(!strcmp(to,"g:staying-found")){lostFromTriage=true;screen=SC_LOST;redraw();return;}
  if (!strncmp(to, "g:", 2)) {
    Entry e{String(to + 2), triTitle(String(to + 2)), ""};
    articleFromTriage = true;
    openArticle(e);
    return;
  }
  for (int k = 0; k < N_TRIAGE; k++) {
    if (!strcmp(TRIAGE[k].id, to)) {
      if (triDepth < 8) triStack[triDepth++] = triCur;
      triCur = &TRIAGE[k];
      drawTriage();
      return;
    }
  }
}

// ---------------------------------------------------------------- waypoint list
static String wpInfo(const Waypoint &w) {
  GpsFix g = gpsGet();
  char b[96];
  if (!g.valid) {
    snprintf(b, sizeof(b), "%s  %.4f, %.4f", w.name, w.lat, w.lon);
  } else {
    double d = geoDistM(g.lat, g.lon, w.lat, w.lon), br = geoBearing(g.lat, g.lon, w.lat, w.lon);
    static const char *DIR[] = {"เหนือ", "อีสาน", "ออก", "อาคเนย์", "ใต้", "หรดี", "ตก", "พายัพ"};
    const char *dir = DIR[((int)((br + 22.5) / 45.0)) & 7];
    if (d < 1000) snprintf(b, sizeof(b), "%s  %d ม. %s", w.name, (int)d, dir);
    else snprintf(b, sizeof(b), "%s  %.1f กม. %s", w.name, d / 1000.0, dir);
  }
  return String(b);
}

static void loadPois(){poiCount=offlineFindPois(poiQuery,poiOffset,poiItems,4,poiNext);gpsLoop();}
static void drawPois(){
  btns.clear();tft.fillScreen(C_BG);drawHeader("สถานที่ใน SD",true);
  tft.setTextFont(1);tft.setTextDatum(TL_DATUM);tft.setTextColor(C_MUTED);
  tft.drawString(fitText(poiQuery.length()?poiQuery:String("สถานที่ทั้งหมด"),SCR_W-12),6,CONTENT_Y+5);
  for(int i=0;i<poiCount;i++)addBtn(4,CONTENT_Y+32+i*44,SCR_W-8,41,B_ROW+i,poiItems[i].name);
  if(!poiCount){tft.setTextDatum(MC_DATUM);tft.drawString(poiNext==UINT32_MAX?"ไม่พบสถานที่":"ยังไม่พบ กด ต่อ เพื่อค้นต่อ",SCR_W/2,CONTENT_Y+80);}
  FootDef f[]={{B_BACK,"กลับ",I_LEFT,true},{B_POI_QUERY,"ค้นหา",I_NONE,true},{B_PREV,"ก่อน",I_LEFT,!poiHistory.empty()},{B_NEXT,"ต่อ",I_RIGHT,poiNext!=UINT32_MAX}};drawFooter(f,4);
}
static void drawWps() {
  btns.clear();
  tft.fillScreen(C_BG);
  drawHeader(wpDelMode ? "แตะเพื่อลบ" : wpGoMode ? "เลือกจุด" : "จุดที่บันทึก", true);
  addBtn(SCR_W-74, 2, 72, HEADER_H-4, B_WP_GO, wpGoMode ? "ยกเลิก" : "นำทาง", I_NONE, nWps>0);
  if (!nWps) {
    tft.setTextFont(2);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_MUTED);
    tft.drawString("ยังไม่มีจุดที่บันทึก", SCR_W / 2, CONTENT_Y + 50);
    tft.drawString("เลื่อนจุดเป้าหมายไปกลางจอ", SCR_W / 2, CONTENT_Y + 78);
    tft.drawString("แล้วแตะ ปัก", SCR_W / 2, CONTENT_Y + 108);
  }
  for (int i = 0; i < nWps; i++) addBtn(4, CONTENT_Y + i * ROW_H, SCR_W - 8, ROW_H - 3, B_ROW + i, wpInfo(wps[i]));
  bool armedW = millis() - wpClearMs < 3000, armedT = millis() - trkClearMs < 3000;
  FootDef f[] = {{B_BACK, "กลับ", I_LEFT, true}, {B_WP_DEL, wpDelMode ? "เสร็จ" : "ลบ", I_NONE, nWps > 0},
                 {B_WP_CLEAR, armedW ? "ยืนยัน?" : "ล้างจุด", I_NONE, nWps > 0},
                 {B_TRK_CLEAR, armedT ? "ยืนยัน?" : "ล้างทาง", I_NONE, trkN > 0},
                 {B_MAP_SEARCH,"ค้นหา",I_NONE,sdUp}};
  drawFooter(f, 5);
}

static void wpRowTap(int i) {
  if (i < 0 || i >= nWps) return;
  if(wpGoMode){gotoIdx=i;gotoFrom=SC_WPS;screen=SC_GOTO;redraw();return;}
  if (wpDelMode) {
    for (int k = i; k < nWps - 1; k++) wps[k] = wps[k + 1];
    nWps--;
    wpSave();
    if (!nWps) wpDelMode = false;
    drawWps();
    return;
  }
  if (!mapReady) mapLoadCenter();
  mapFollow=false;
  offlineSetDestination(wps[i].lat,wps[i].lon,wps[i].name);
  if (mapReady) mapLatLonToPx(wps[i].lat, wps[i].lon, mapZ, mapCx, mapCy);
  screen = SC_MAP;
  redraw();
}

static void wpMark() {
  double lat, lon;
  mapPxToLatLon(mapCx, mapCy, mapZ, lat, lon);
  if (nWps >= wpMax()) { snprintf(mapToast, sizeof(mapToast), "เต็ม (%d)", wpMax()); }
  else {
    int next = 1;
    for (int i = 0; i < nWps; i++) { int n = atoi(wps[i].name + 2); if (!strncmp(wps[i].name, "WP", 2) && n >= next) next = n + 1; }
    Waypoint &w = wps[nWps++];
    snprintf(w.name, sizeof(w.name), "WP%d", next);
    w.lat = lat; w.lon = lon;
    if (wpSave()) snprintf(mapToast, sizeof(mapToast), "บันทึก %s", w.name);
    else { wpLoad(); snprintf(mapToast, sizeof(mapToast), "บันทึกไม่ได้ ตรวจ SD"); }
  }
  mapToastMs = millis();
  mapDrawTiles();
}


static void geoNote() {                              // remember the last good position (the clock and sun times need it)
  GpsFix g = gpsGet();
  if (!g.valid) return;
  lastLat = g.lat; lastLon = g.lon; haveLast = true;
  static double savedLat = 999, savedLon = 999;
  if (geoDistM(savedLat, savedLon, g.lat, g.lon) > 5000.0) {   // keep it across reboots, but write rarely
    savedLat = g.lat; savedLon = g.lon;
    prefs.putFloat("llat", (float)g.lat); prefs.putFloat("llon", (float)g.lon);
  }
}

static void gsSay(const char *s) { snprintf(gsToast, sizeof(gsToast), "%s", s); gsToastMs = millis(); }

static String fmtDist(double m) {
  char b[24];
  if (m < 1000) snprintf(b, sizeof(b), "%d m", (int)(m + 0.5));
  else if (m < 100000) snprintf(b, sizeof(b), "%.1f km", m / 1000.0);
  else snprintf(b, sizeof(b), "%d km", (int)(m / 1000.0 + 0.5));
  return String(b);
}
static String fmtDistImp(double m) {
  char b[24];
  double ft = m * 3.28084;
  if (ft < 1000) snprintf(b, sizeof(b), "%d ft", (int)(ft + 0.5));
  else snprintf(b, sizeof(b), "%.1f mi", m / 1609.344);
  return String(b);
}
static const char *cardinal(double deg) {
  static const char *D[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  return D[((int)((deg + 22.5) / 45.0)) & 7];
}

static void bigFont() { tft.setThaiSize(SCR_W < 300 ? 16 : 22); }
static void midFont() { tft.setThaiSize(SCR_W < 300 ? 12 : 16); }
static void lineText(const String &text, int x, int y, uint8_t datum, uint16_t color) {
  tft.setTextDatum(datum);
  tft.setTextColor(color);
  int top = y;
  if (datum / 3 == 1) top -= tft.lineHeight() / 2;
  else if (datum / 3 >= 2) top -= tft.lineHeight();
  tft.fillRect(0, top, SCR_W, tft.lineHeight(), C_BG);
  tft.drawString(fitText(text, SCR_W-12), x, y);
}

// ---- time: GPS gives UTC, the zone is set on the Clock screen ----
static int tzOffsetHours() {
  int base = tzAuto ? (haveLast ? (int)lround(lastLon / 15.0) : 0) : tzHours;
  return base + (tzDst ? 1 : 0);
}
static void civilFromDays(int32_t z, int &y, int &m, int &d) {
  z += 719468;
  int32_t era = (z >= 0 ? z : z - 146096) / 146097;
  unsigned doe = (unsigned)(z - era * 146097);
  unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  y = (int)yoe + era * 400;
  unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  unsigned mp = (5 * doy + 2) / 153;
  d = (int)(doy - (153 * mp + 2) / 5 + 1);
  m = (int)(mp < 10 ? mp + 3 : mp - 9);
  y += (m <= 2);
}
struct LocalTime { int y, mo, d, h, mi, s, wday; };
static LocalTime localFromEpoch(uint32_t epochUtc) {
  int64_t t = (int64_t)epochUtc + (int64_t)tzOffsetHours() * 3600;
  int32_t days = (int32_t)(t / 86400);
  int32_t rem = (int32_t)(t - (int64_t)days * 86400);
  if (rem < 0) { rem += 86400; days--; }
  LocalTime L;
  civilFromDays(days, L.y, L.mo, L.d);
  L.h = rem / 3600; L.mi = (rem % 3600) / 60; L.s = rem % 60;
  L.wday = (int)((days + 4) % 7); if (L.wday < 0) L.wday += 7;
  return L;
}
static String fmtClock(int h, int mi, int s, bool withSec) {
  char b[24];
  if (clock24) {
    if (withSec) snprintf(b, sizeof(b), "%02d:%02d:%02d", h, mi, s); else snprintf(b, sizeof(b), "%02d:%02d", h, mi);
  } else {
    int hh = h % 12 == 0 ? 12 : h % 12; const char *ap = h < 12 ? "AM" : "PM";
    if (withSec) snprintf(b, sizeof(b), "%d:%02d:%02d %s", hh, mi, s, ap); else snprintf(b, sizeof(b), "%d:%02d %s", hh, mi, ap);
  }
  return String(b);
}
static String fmtLocalEpoch(uint32_t epochUtc, bool withSec) {
  LocalTime L = localFromEpoch(epochUtc);
  return fmtClock(L.h, L.mi, L.s, withSec);
}
static String tzLabel() {
  int o = tzOffsetHours();
  char b[24];
  snprintf(b, sizeof(b), "UTC%s%d%s%s", o < 0 ? "-" : "+", abs(o), tzDst ? " DST" : "", tzAuto ? " (auto)" : "");
  return String(b);
}

// ---- where the sun is: the same maths as the phone Tools page ----
static void sunPos(double unixSec, double lat, double lon, double &az, double &alt) {
  const double R = M_PI / 180.0;
  double d = unixSec / 86400.0 + 2440587.5 - 2451545.0;
  double g = fmod(357.529 + 0.98560028 * d, 360.0), q = fmod(280.459 + 0.98564736 * d, 360.0);
  double L = fmod(q + 1.915 * sin(g * R) + 0.020 * sin(2 * g * R), 360.0), e = 23.439 - 0.00000036 * d;
  double ra = atan2(cos(e * R) * sin(L * R), cos(L * R)) / R, dec = asin(sin(e * R) * sin(L * R));
  double gm = fmod(18.697374558 + 24.06570982441908 * d, 24.0);
  double lst = fmod(fmod(gm * 15.0 + lon, 360.0) + 360.0, 360.0), H = (lst - ra) * R, la = lat * R;
  alt = asin(sin(la) * sin(dec) + cos(la) * cos(dec) * cos(H)) / R;
  az = fmod(atan2(sin(H), cos(H) * sin(la) - tan(dec) * cos(la)) / R + 180.0 + 360.0, 360.0);
}
// sunrise and sunset (UTC epoch, 0 = none) for the local day that contains `nowUtc`
static void sunTimes(uint32_t nowUtc, double lat, double lon, uint32_t &rise, uint32_t &set, bool &up) {
  rise = set = 0;
  int64_t off = (int64_t)tzOffsetHours() * 3600;
  int64_t day0 = ((int64_t)nowUtc + off) / 86400 * 86400 - off;      // local midnight, as UTC
  double az, alt, prev;
  sunPos((double)day0, lat, lon, az, prev);
  for (int m = 5; m <= 1440; m += 5) {
    double t = (double)day0 + m * 60.0;
    sunPos(t, lat, lon, az, alt);
    if (prev < -0.833 && alt >= -0.833 && !rise) rise = (uint32_t)(t - 150);   // good to a few minutes
    if (prev >= -0.833 && alt < -0.833 && !set) set = (uint32_t)(t - 150);
    prev = alt;
  }
  sunPos((double)nowUtc, lat, lon, az, alt);
  up = alt > -0.833;
}

// ---- the readout shared by the GPS hub and the lost screen ----
static String localTimeLine() {
  uint32_t ep;
  if (!gpsUtcNow(&ep)) return String("เวลา: รอ GPS");
  return fmtLocalEpoch(ep, true) + "  " + tzLabel();
}
static void drawReadout(int y) {
  GpsFix g = gpsGet();
  int step = SCR_W < 300 ? 28 : 46;
  int cx = SCR_W / 2;
  tft.setThaiSize(SCR_W < 300 ? 14 : 22);
  if (g.valid) {
    char a[28], b[28];
    snprintf(a, sizeof(a), "%.5f %c", fabs(g.lat), g.lat < 0 ? 'S' : 'N');
    snprintf(b, sizeof(b), "%.5f %c", fabs(g.lon), g.lon < 0 ? 'W' : 'E');
    lineText(a, cx, y, TC_DATUM, C_TEXT);
    lineText(b, cx, y + step, TC_DATUM, C_TEXT);
  } else {
    lineText("ยังรับพิกัดไม่ได้", cx, y, TC_DATUM, C_ACCENT);
    lineText("", cx, y + step, TC_DATUM, C_TEXT);
  }
  midFont();
  String l3;
  if (gsToast[0] && millis() - gsToastMs < 3500) l3 = gsToast;
  else if (g.valid) l3 = g.phone ? String("จากโทรศัพท์") : String(g.sats) + " ดาว " + String((int)g.altM) + " ม.";
  else l3 = PIN_GPS_RX < 0 ? "รอโทรศัพท์แชร์พิกัด" : String(g.sats) + " ดาว ควรอยู่กลางแจ้ง";
  lineText(l3, cx, y + 2 * step, TC_DATUM, gsToast[0] && millis() - gsToastMs < 3500 ? NC(0x07E0) : C_MUTED);
  lineText(localTimeLine(), cx, y + 2 * step + (SCR_W < 300 ? 24 : 32), TC_DATUM, C_MUTED);
}

// ---- Where hub ----
static void updateWhere();
static void drawWhere() {
  btns.clear();
  tft.fillScreen(C_BG);
  drawHeader("GPS", true);
  int bh = SCR_W < 300 ? 40 : 56, gap = SCR_W < 300 ? 6 : 8;
  int by = CONTENT_Y + CONTENT_H - 4 * (bh + gap) + gap;
  addBtn(8, by, SCR_W - 16, bh, B_LOST, "หลงทาง");
  addBtn(8, by + (bh + gap), SCR_W - 16, bh, B_GOTO, "ไปยังจุดปัก");
  addBtn(8, by + 2 * (bh + gap), SCR_W - 16, bh, B_CLOCK, "เวลาและดวงอาทิตย์");
  addBtn(8, by + 3 * (bh + gap), SCR_W - 16, bh, B_TRIP, "สถิติเดินทาง");
  updateWhere();
  FootDef f[] = {{B_BACK, "กลับ", I_LEFT, true}};
  drawFooter(f, 1);
}
static void updateWhere() {
  midFont();
  GpsFix fix = gpsGet();
  String position = fix.valid ? String(fix.lat,5) + ", " + String(fix.lon,5) : String("กำลังรอพิกัด GPS");
  lineText(position, SCR_W/2, CONTENT_Y+4, TC_DATUM, C_TEXT);
  lineText(localTimeLine(), SCR_W/2, CONTENT_Y+36, TC_DATUM, C_MUTED);
}

// ---- I'm lost ----
static int wpFind(const char *name) {
  for (int i = 0; i < nWps; i++) if (!strcmp(wps[i].name, name)) return i;
  return -1;
}
static void drawLost() {
  btns.clear();
  tft.fillScreen(C_BG);
  drawHeader("หลงทาง", true);
  wpLoad();
  int bh = SCR_W < 300 ? 38 : 54, gap = SCR_W < 300 ? 4 : 8;
  int by = CONTENT_Y + CONTENT_H - 3 * (bh + gap) + gap;
  addBtn(8, by, SCR_W - 16, bh, B_GOTO_START, "กลับจุดเริ่ม");
  addBtn(8, by + (bh + gap), SCR_W - 16, bh, B_SEND_SPOT, "ส่งพิกัดฉัน");
  addBtn(8, by + 2 * (bh + gap), SCR_W - 16, bh, B_SET_START, "ตั้งจุดเริ่ม");
  drawReadout(CONTENT_Y + (SCR_W < 300 ? 4 : 8));
  FootDef f[] = {{B_BACK, "กลับ", I_LEFT, true}, {B_LOST_GUIDE, "คู่มือ", I_NONE, true}};
  drawFooter(f, 2);
}
static void updateLost() { drawReadout(CONTENT_Y + (SCR_W < 300 ? 4 : 8)); }

static void wpSetStart() {
  GpsFix g = gpsGet();
  if (!g.valid) { gsSay("ยังไม่มีพิกัด GPS"); return; }
  wpLoad();
  int i = wpFind("START");
  if (i < 0) {
    if (nWps >= wpMax()) { gsSay("รายการจุดปักเต็ม"); return; }
    i = nWps++;
    snprintf(wps[i].name, sizeof(wps[i].name), "START");
  }
  wps[i].lat = g.lat; wps[i].lon = g.lon;
  if (wpSave()) gsSay("บันทึกจุดเริ่มแล้ว");
  else { wpLoad(); gsSay("บันทึกไม่ได้ ตรวจ SD"); }
}
static void sendSpot() {
  GpsFix g = gpsGet();
  if (!g.valid) { gsSay("ยังไม่มีพิกัด GPS"); return; }
  if (!meshEnabled) { gsSay("ระบบเมชปิดอยู่"); return; }
  char b[64];
  snprintf(b, sizeof(b), "SOS %.5f,%.5f", g.lat, g.lon);
  gsSay(meshSend(b) ? "ส่งให้บอร์ดใกล้เคียงแล้ว" : "ส่งไม่สำเร็จ");
}

// ---- Go to a waypoint: arrow and distance ----
static void drawArrow(int cx, int cy, int r, double angDeg, uint16_t col) {   // angle clockwise from straight up
  double a = angDeg * M_PI / 180.0;
  auto pt = [&](double rr, double off, int &x, int &y) { double t = a + off; x = cx + (int)lround(rr * sin(t)); y = cy - (int)lround(rr * cos(t)); };
  int tx, ty, lx, ly, rx, ry, mx, my;
  pt(r, 0, tx, ty); pt(r * 0.78, 2.45, lx, ly); pt(r * 0.78, -2.45, rx, ry); pt(r * 0.30, M_PI, mx, my);
  tft.fillTriangle(tx, ty, lx, ly, mx, my, col);
  tft.fillTriangle(tx, ty, rx, ry, mx, my, col);
}
static void drawGoto() {
  btns.clear();
  tft.fillScreen(C_BG);
  drawHeader("ไปยังจุด", true);
  wpLoad();
  if (gotoIdx >= nWps) gotoIdx = 0;
  bool browse = nWps > 1 && gotoFrom != SC_LOST;
  FootDef f[] = {{B_BACK, "กลับ", I_LEFT, true}, {B_GOTO_PREV, "", I_LEFT, browse}, {B_GOTO_NEXT, "", I_RIGHT, browse}};
  drawFooter(f, 3);
}
static void updateGoto() {
  const int cx = SCR_W / 2, cy = CONTENT_Y + CONTENT_H * 44 / 100, R = min<int>(SCR_W, CONTENT_H) * 19 / 100;
  tft.fillRect(0, CONTENT_Y, SCR_W, CONTENT_H, C_BG);
  tft.setTextPadding(0);
  if (nWps == 0) {
    midFont();
    lineText("ยังไม่มีจุดปัก", cx, CONTENT_Y + 50, TC_DATUM, C_ACCENT);
    lineText("ปักจุดบนแผนที่ หรือแตะ", cx, CONTENT_Y + 80, TC_DATUM, C_MUTED);
    lineText("ตั้งจุดเริ่มในหน้าหลงทาง", cx, CONTENT_Y + 100, TC_DATUM, C_MUTED);
    return;
  }
  const Waypoint &w = wps[gotoIdx];
  midFont();
  String target = !strcmp(w.name, "START") ? String("จุดเริ่มต้น") : String(w.name);
  lineText(target + (nWps > 1 && gotoFrom != SC_LOST ? "  (" + String(gotoIdx + 1) + "/" + String(nWps) + ")" : String("")), cx, CONTENT_Y + 4, TC_DATUM, C_ACCENT);
  GpsFix g = gpsGet();
  int yd = cy + R + (SCR_W < 300 ? 29 : 38), y2 = yd + (SCR_W < 300 ? 28 : 38), y3 = y2 + (SCR_W < 300 ? 24 : 32);
  if (!g.valid) {
    lineText("กำลังรอ GPS", cx, cy, MC_DATUM, C_MUTED);
    return;
  }
  double dist = geoDistM(g.lat, g.lon, w.lat, w.lon), brg = geoBearing(g.lat, g.lon, w.lat, w.lon);
  bool head = !g.phone && g.courseMs && millis()-g.courseMs<15000;
  double rel = head ? brg - g.courseDeg : brg;
  bool arrived = dist < 20.0;
  uint16_t col = arrived ? NC(0x07E0) : C_ACCENT;
  tft.drawCircle(cx, cy, R + 10, C_LINE);
  if (head) tft.fillTriangle(cx, cy - R - 16, cx - 6, cy - R - 6, cx + 6, cy - R - 6, C_TEXT);   // straight ahead
  else { tft.setTextFont(1); tft.setTextDatum(MC_DATUM); tft.setTextColor(C_MUTED); tft.drawString("N", cx, cy - R - 22); }
  if (arrived) { tft.fillCircle(cx, cy, R / 3, col); }
  else drawArrow(cx, cy, R, rel, col);
  bigFont();
  lineText(arrived ? String("ถึงจุดหมายแล้ว") : fmtDist(dist), cx, yd, MC_DATUM, col);
  midFont();
  char b[128];
  snprintf(b, sizeof(b), "%s   %d %s", fmtDistImp(dist).c_str(), (int)(brg + 0.5), cardinal(brg));
  lineText(b, cx, y2, MC_DATUM, C_TEXT);
  lineText(head ? (g.speedKmh >= 2.0f ? String("ลูกศรเทียบทิศที่เดิน") : String("ลูกศรเทียบทิศเดินล่าสุด")) : String("ทิศเหนืออยู่บน เดินเพื่อหาทิศ"),
           cx, y3, MC_DATUM, C_MUTED);
}

// ---- Clock and sun ----
static void drawClock() {
  btns.clear();
  tft.fillScreen(C_BG);
  drawHeader("เวลาและดวงอาทิตย์", true);
  { int bh2 = SCR_W < 300 ? 34 : 48; addBtn(8, CONTENT_Y + CONTENT_H - bh2 - 6, SCR_W - 16, bh2, B_ALARM_CYCLE, alarmLabel()); }
  FootDef f[] = {{B_BACK, "", I_LEFT, true}, {B_TZ_DN, "-", I_NONE, true}, {B_TZ_UP, "+", I_NONE, true},
                 {B_TZ_DST, tzDst ? "DST*" : "DST", I_NONE, true}, {B_TZ_AUTO, tzAuto ? "Auto*" : "Auto", I_NONE, true},
                 {B_TZ_24H, clock24 ? "24h*" : "24h", I_NONE, true}};
  drawFooter(f, 6);
}
static void updateClock() {
  int cx = SCR_W / 2, y = CONTENT_Y + (SCR_W < 300 ? 8 : 14);
  int s1 = SCR_W < 300 ? 31 : 56, s2 = SCR_W < 300 ? 24 : 32;
  uint32_t ep;
  if (!gpsUtcNow(&ep)) {
    midFont();
    lineText("กำลังรอเวลา GPS", cx, y + 20, TC_DATUM, C_ACCENT);
    lineText("ให้ GPS รับสัญญาณกลางแจ้ง", cx, y + 20 + s2, TC_DATUM, C_MUTED);
    return;
  }
  LocalTime L = localFromEpoch(ep);
  static const char *MONTH[] = {"ม.ค.", "ก.พ.", "มี.ค.", "เม.ย.", "พ.ค.", "มิ.ย.", "ก.ค.", "ส.ค.", "ก.ย.", "ต.ค.", "พ.ย.", "ธ.ค."};
  bigFont();
  lineText(fmtClock(L.h, L.mi, L.s, true), cx, y, TC_DATUM, C_TEXT);
  midFont();
  char b[128];
  snprintf(b, sizeof(b), "%d %s %d", L.d, MONTH[L.mo - 1], L.y);
  lineText(b, cx, y + s1, TC_DATUM, C_TEXT);
  lineText(tzLabel(), cx, y + s1 + s2, TC_DATUM, C_MUTED);
  snprintf(b, sizeof(b), "เวลา GPS อัปเดต %u วิ ก่อน", (unsigned)gpsTimeSyncAgeS());
  lineText(b, cx, y + s1 + 2 * s2, TC_DATUM, C_MUTED);
  int ys = y + s1 + 3 * s2 + 10;
  if (!haveLast) { lineText("ต้องมีพิกัดเพื่อคำนวณ", cx, ys, TC_DATUM, C_MUTED); return; }
  static uint32_t rise = 0, set = 0; static bool up = false;
  if (!sunCalcMs || millis() - sunCalcMs > 30000) { sunCalcMs = millis(); sunTimes(ep, lastLat, lastLon, rise, set, up); }
  String sunLine = String("ขึ้น ")+(rise ? fmtLocalEpoch(rise,false):String("--"))+"  ตก "+(set ? fmtLocalEpoch(set,false):String("--"));
  lineText(sunLine, cx, ys, TC_DATUM, C_TEXT);
  String l;
  if (up && set && set > ep) { uint32_t m = (set - ep) / 60; l = "เหลือแสง " + String(m / 60) + " ชม. " + String(m % 60) + " นาที"; }
  else if (rise && ep < rise) l = "ดวงอาทิตย์ยังไม่ขึ้น";
  else if (set && ep >= set) l = "ดวงอาทิตย์ตกแล้ว";
  else l = up ? "มีแสงตลอดวัน" : "ดวงอาทิตย์อยู่ใต้ขอบฟ้า";
  lineText(l, cx, ys + s2, TC_DATUM, up ? NC(0xFFE0) : C_MUTED);
}
static void clockSave() {
  prefs.putChar("tz", tzHours); prefs.putBool("tzauto", tzAuto); prefs.putBool("dst", tzDst); prefs.putBool("h24", clock24);
}

static void geoRefresh() {                           // called about once a second while one of these screens is showing
  switch (screen) {
    case SC_WHERE: updateWhere(); break;
    case SC_LOST: updateLost(); break;
    case SC_GOTO: updateGoto(); break;
    case SC_CLOCK: updateClock(); break;
    case SC_TRIP: updateTrip(); break;
    default: break;
  }
}

// ---------------------------------------------------------------- speed, altitude and trip stats
static void tripReset() {
  tripDist = 0; tripMoveS = 0; tripMaxKmh = 0; tripClimb = 0; tripSpd = 0; tripHave = false;
}

// Called from loop(): a sample every 2 seconds while the GPS has a fix.
static void tripLoop(uint32_t now) {
  static uint32_t next = 0;
  if ((int32_t)(now - next) < 0) return;
  next = now + 2000;
  GpsFix g = gpsGet();
  if (!g.valid) { tripHave = false; tripSpd = 0; return; }
  uint32_t dt = g.fixMs - tripFixMs;
  if (!tripHave || tripPhone != g.phone || dt > 12000) {
    tripLat = g.lat;
    tripLon = g.lon;
    tripAltRef = g.altM;
    tripFixMs = tripPositionMs = g.fixMs;
    tripPhone = g.phone;
    tripHave = true;
    tripSpd = g.speedKmh;
    return;
  }
  if (!dt) return;  // The phone's same GPS sample may span several loop ticks.
  tripFixMs = g.fixMs;
  double distance = geoDistM(tripLat, tripLon, g.lat, g.lon);
  double threshold = g.phone ? max(5.0, (double)g.accuracyM) : 5.0;
  bool moved = distance >= threshold;
  uint32_t positionDt = g.fixMs - tripPositionMs;
  float inferred = moved && positionDt ? (float)(distance * 3600.0 / positionDt) : 0;
  tripSpd = g.phone ? inferred : g.speedKmh;
  if (tripSpd >= 1.5f && tripSpd < 250.0f) {
    tripMoveS += (dt + 500) / 1000;
    tripMaxKmh = max(tripMaxKmh, tripSpd);
  }
  if (moved) {
    if (inferred < 250.0f) tripDist += distance;
    tripLat = g.lat;
    tripLon = g.lon;
    tripPositionMs = g.fixMs;
  }
  if (!g.phone) {                                    // climb: only count rises of more than 3 m (altitude is noisy)
    if (g.altM > tripAltRef + 3.0f) { tripClimb += g.altM - tripAltRef; tripAltRef = g.altM; }
    else if (g.altM < tripAltRef - 3.0f) tripAltRef = g.altM;
  }
}

static String fmtHMS(uint32_t s) {
  char b[16];
  snprintf(b, sizeof(b), "%u:%02u:%02u", (unsigned)(s / 3600), (unsigned)((s % 3600) / 60), (unsigned)(s % 60));
  return String(b);
}
static void drawTrip() {
  btns.clear();
  tft.fillScreen(C_BG);
  drawHeader("สถิติเดินทาง", true);
  FootDef f[] = {{B_BACK, "กลับ", I_LEFT, true}, {B_TRIP_RESET, "ล้างสถิติ", I_NONE, true}};
  drawFooter(f, 2);
}
static void updateTrip() {
  GpsFix g = gpsGet();
  int cx = SCR_W / 2, y = CONTENT_Y + (SCR_W < 300 ? 6 : 12);
  int s1 = SCR_W < 300 ? 34 : 54, s2 = SCR_W < 300 ? 24 : 32;
  char b[128];
  if (!g.valid) {
    tft.fillRect(0, CONTENT_Y, SCR_W, CONTENT_H, C_BG);
    midFont();
    lineText(PIN_GPS_RX < 0 ? "รอพิกัดจากโทรศัพท์" : "รอรับพิกัด GPS", cx, y + 30, TC_DATUM, C_ACCENT);
    return;
  }
  tft.fillRect(0, CONTENT_Y, SCR_W, CONTENT_H, C_BG);
  bigFont();
  snprintf(b, sizeof(b), "%.1f กม./ชม.", tripSpd);
  lineText(b, cx, y, TC_DATUM, C_TEXT);
  midFont();
  snprintf(b, sizeof(b), "%.1f mph", tripSpd * 0.621371);
  lineText(b, cx, y + s1, TC_DATUM, C_MUTED);
  if (g.phone) snprintf(b, sizeof(b), "โทรศัพท์ไม่ได้ส่งความสูง");
  else snprintf(b, sizeof(b), "สูง %d ม. (%d ฟุต)", (int)lround(g.altM), (int)lround(g.altM * 3.28084));
  lineText(b, cx, y + s1 + s2, TC_DATUM, C_TEXT);
  if (!g.phone && g.courseMs && millis()-g.courseMs<15000) snprintf(b, sizeof(b), "ทิศ %s %d องศา", cardinal(g.courseDeg), (int)lround(g.courseDeg));
  else snprintf(b, sizeof(b), "เดินสักครู่เพื่อหาทิศ");
  lineText(b, cx, y + s1 + 2 * s2, TC_DATUM, C_TEXT);
  int ty = y + s1 + 3 * s2 + (SCR_W < 300 ? 10 : 16);
  tft.drawFastHLine(10, ty - 6, SCR_W - 20, C_LINE);
  lineText(String("ระยะทาง ") + fmtDist(tripDist) + "  (" + fmtDistImp(tripDist) + ")", cx, ty, TC_DATUM, C_ACCENT);
  lineText(String("เคลื่อนที่ ") + fmtHMS(tripMoveS), cx, ty + s2, TC_DATUM, C_TEXT);
  snprintf(b, sizeof(b), "เร็วสุด %.1f กม./ชม.", tripMaxKmh);
  lineText(b, cx, ty + 2 * s2, TC_DATUM, C_TEXT);
  snprintf(b, sizeof(b), "ไต่สูง %d ม. (%d ฟุต)", (int)lround(tripClimb), (int)lround(tripClimb * 3.28084));
  lineText(b, cx, ty + 3 * s2, TC_DATUM, C_TEXT);
}

// ---------------------------------------------------------------- sunset turn-back alarm
static const uint8_t TB_MIN[] = {0, 30, 60, 90, 120};
static String alarmLabel() {
  if (!tbIdx) return String("เตือนก่อนมืด: ปิด");
  return String("เตือนก่อนมืด ") + String(TB_MIN[tbIdx]) + " นาทีก่อน";
}
static void drawAlarm() {
  btns.clear();
  uint16_t red = NC(tft.color565(170, 30, 30));
  tft.fillScreen(red);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(NC(TFT_WHITE));
  int cx = SCR_W / 2;
  if (SCR_W < 300) tft.setFreeFont(&FreeSansBold18pt7b); else tft.setFreeFont(&FreeSansBold24pt7b);
  tft.drawString("ควรเดินทางกลับ", cx, CONTENT_Y + CONTENT_H / 4);
  tft.setFreeFont(SCR_W < 300 ? &FreeSansBold9pt7b : &FreeSansBold12pt7b);
  uint32_t ep = 0; gpsUtcNow(&ep);
  alarmMinute = ep/60;
  String l1 = alarmSunset ? "อาทิตย์ตก " + fmtLocalEpoch(alarmSunset, false) : String("ใกล้เวลามืดแล้ว");
  tft.drawString(l1, cx, CONTENT_Y + CONTENT_H / 2 - 10);
  if (alarmSunset && ep && alarmSunset > ep) tft.drawString(String((alarmSunset - ep) / 60) + " นาทีจะมืด", cx, CONTENT_Y + CONTENT_H / 2 + 20);
  else tft.drawString("ดวงอาทิตย์ตกแล้ว", cx, CONTENT_Y + CONTENT_H / 2 + 20);
  int bh = SCR_W < 300 ? 40 : 56;
  addBtn(8, SCR_H - 2 * bh - 20, SCR_W - 16, bh, B_ALARM_OK, "รับทราบ");
  addBtn(8, SCR_H - bh - 8, SCR_W - 16, bh, B_ALARM_SNOOZE, "เตือนอีก 15 นาที");
}
// Called from loop(): warn once a day, tbMin minutes before the sun sets where the board is.
static void alarmCheck(uint32_t now) {
  static uint32_t nextMs = 0;
  if (screen == SC_ALARM) {
    digitalWrite(PIN_LED_R, ((now / 500) & 1) ? LOW : HIGH);   // flash the red LED
    lastTouchMs = now;                                       // keep the screen awake
    uint32_t ep;
    if (gpsUtcNow(&ep) && ep/60 != alarmMinute) drawAlarm();
    return;
  }
  if (!tbIdx || screen == SC_LIGHT || (int32_t)(now - nextMs) < 0) return;
  nextMs = now + 15000;
  uint32_t ep;
  if (!gpsUtcNow(&ep) || !haveLast) return;
  uint32_t rise, set; bool up;
  sunTimes(ep, lastLat, lastLon, rise, set, up);
  if (!set) return;
  int32_t day = (int32_t)(((int64_t)ep + (int64_t)tzOffsetHours() * 3600) / 86400);
  uint32_t lead = TB_MIN[tbIdx] * 60UL;
  bool inWindow = ep < set && set - ep <= lead;
  bool fresh = alarmDay != day || (alarmSnoozeEp && ep >= alarmSnoozeEp);
  if (!inWindow || !fresh) return;
  alarmDay = day; alarmSnoozeEp = 0; alarmSunset = set;
  alarmPrev = screen;
  if (sleeping) { sleeping = false; setBacklight(nightNow ? min<int>(brightness, 25) : brightness); }
  lastTouchMs = now;
  screen = SC_ALARM;
  fieldAlert(FieldAlert::Sunset);
  drawAlarm();
}
static void alarmEnd(bool snooze) {
  uint32_t ep = 0; gpsUtcNow(&ep);
  alarmSnoozeEp = snooze ? ep + 900 : 0;
  digitalWrite(PIN_LED_R, HIGH);
  screen = alarmPrev == SC_ALARM ? SC_HOME : alarmPrev;
  redraw();
}


static void loadMapSets() {
  mapSetCount = mapStorageList(mapSetOffset, mapSets, 4, mapSetNext);
  gpsLoop();
}

static void drawMapSets() {
  btns.clear(); tft.fillScreen(C_BG); drawHeader("เลือกจังหวัด", true);
  tft.setTextFont(1); tft.setTextDatum(TL_DATUM); tft.setTextColor(C_MUTED);
  String note = mapSetError.length() ? mapSetError : String("ใช้อยู่: ") + mapStorageName();
  tft.drawString(fitText(note, SCR_W-12), 6, CONTENT_Y+3);
  // Four rows fit the 240x320 CYD; fitText preserves Thai clusters.
  String active = mapStorageRoot();
  for (int i=0; i<mapSetCount; i++) {
    String label = (mapSets[i].path==active ? String("* ") : String()) + mapSets[i].name;
    addBtn(4, CONTENT_Y+31+i*44, SCR_W-8, 41, B_ROW+i, label);
  }
  if (!mapSetCount) {
    tft.setTextDatum(MC_DATUM);
    tft.drawString("ไม่พบชุดแผนที่ที่พร้อมใช้", SCR_W/2, CONTENT_Y+84);
    tft.drawString("ตรวจโฟลเดอร์ maps บน SD", SCR_W/2, CONTENT_Y+112);
  }
  tft.setTextFont(1); tft.setTextDatum(MC_DATUM); tft.setTextColor(C_MUTED);
  tft.drawString(String("หน้า ")+String(mapSetOffset/4+1), SCR_W/2, SCR_H-FOOTER_H-14);
  FootDef f[]={{B_BACK,"กลับ",I_LEFT,true},{B_PREV,"ก่อน",I_LEFT,mapSetOffset>0},
               {B_NEXT,"ต่อ",I_RIGHT,mapSetNext!=UINT32_MAX}};
  drawFooter(f,3);
}

static void mapSetTap(int i) {
  if (i<0 || i>=mapSetCount) return;
  if (!mapStorageSelect(mapSets[i].path)) {
    mapSetError="เลือกไม่ได้ ตรวจการ์ด SD"; drawMapSets(); return;
  }
  fieldTrackLoop();
  // All data below belongs to the chosen set, never the previous province.
  mapFollow=false; mapToast[0]=0; wpDelMode=false;
  wpClearMs=0; trkClearMs=0; poiQuery=""; poiOffset=0; poiNext=UINT32_MAX;
  poiCount=0; poiHistory.clear();
  mapLoadCenter(); wpLoad(); trkLoad(); offlineRouteRefresh();
  screen=SC_MAP; redraw();
}

static void fieldLine(const String &text, int row, uint16_t color=C_TEXT) {
  tft.setTextFont(1);
  tft.setTextColor(color);
  tft.setTextDatum(TL_DATUM);
  tft.drawString(fitText(text, SCR_W-12), 6, CONTENT_Y+3+row*27);
}

static void drawField() {
  btns.clear(); tft.fillScreen(C_BG); drawHeader("ภาคสนาม", true);
  const char *labels[]={"ตรวจความพร้อม", "บันทึกและย้อนรอย", "แบตเตอรี่และเสียง", "วิทยุ LoRa"};
  const uint16_t ids[]={B_READY,B_TRACK,B_HARDWARE,B_LORA};
  for(int i=0;i<4;i++) addBtn(6,CONTENT_Y+6+i*53,SCR_W-12,48,ids[i],labels[i]);
  FootDef f[]={{B_BACK,"กลับ",I_LEFT,true}};drawFooter(f,1);
}

static void drawReady() {
  btns.clear();tft.fillScreen(C_BG);drawHeader("ตรวจความพร้อม",true);
  GpsFix gps=gpsGet();
  fieldLine(sdUp?String("SD ว่าง ")+String((SD.totalBytes()-SD.usedBytes())/1048576)+" MB":"ไม่พบ SD",0);
  fieldLine(gps.valid?String("GPS พร้อม ")+String(gps.sats)+" ดาว":"GPS กำลังรอพิกัด",1);
  fieldLine(String("แผนที่: ")+mapStorageName(),2);
  bool local=false;
  if(mapReady&&gps.valid){int32_t x,y;mapLatLonToPx(gps.lat,gps.lon,mapZ,x,y);local=SD.exists(mapStoragePath(String(mapZ)+"/"+String(x/256)+"/"+String(y/256)+".jpg"));}
  fieldLine(!gps.valid?"รับพิกัดเพื่อตรวจภาพแผนที่":local?"มีภาพ ณ พิกัด ในซูมที่เปิด":"พิกัดนี้ยังไม่มีภาพในซูมที่เปิด",3);
  FieldBattery battery=fieldBattery();
  fieldLine(battery.valid?String("แบต ")+String(battery.volts,2)+" V ~"+String(battery.percent)+"%":"แบต: เปิดวงจรวัดในตั้งค่า",4);
  fieldLine(String("LoRa: ")+fieldLoRaStateText(),5);
  fieldLine(fieldNotice,6,C_ACCENT);
  FootDef f[]={{B_BACK,"กลับ",I_LEFT,true},{B_SUGGEST,"หาแผนที่",I_NONE,gps.valid},{B_USE_SUGGEST,"ใช้ชุดนี้",I_NONE,suggestedMap.path.length()>0}};drawFooter(f,3);
}

static void suggestMap() {
  GpsFix gps=gpsGet();suggestedMap={};
  if(!gps.valid){fieldNotice="ยังไม่มีพิกัด GPS";return;}
  uint32_t offset=0,next;
  MapSetEntry item;
  double best=1e20;
  do {
    if(mapStorageList(offset,&item,1,next)!=1) break;
    File f=SD.open(item.path+"/center.txt");
    String center=f?readLine(f):String();if(f)f.close();
    double lat,lon;int z,minz,maxz;
    if(sscanf(center.c_str(),"%lf %lf %d %d %d",&lat,&lon,&z,&minz,&maxz)==5){
      int32_t x,y;mapLatLonToPx(gps.lat,gps.lon,maxz,x,y);
      if(SD.exists(item.path+"/"+String(maxz)+"/"+String(x/256)+"/"+String(y/256)+".jpg")){
        double distance=geoDistM(gps.lat,gps.lon,lat,lon);
        if(distance<best){best=distance;suggestedMap=item;}
      }
    }
    offset=next;gpsLoop();yield();
  } while(next!=UINT32_MAX);
  fieldNotice=suggestedMap.path.length()?String("แนะนำ ")+suggestedMap.name:"ไม่พบชุดที่มีภาพ ณ พิกัด";
}

static void drawTrack() {
  btns.clear();tft.fillScreen(C_BG);drawHeader("บันทึกและย้อนรอย",true);
  FieldTrackStatus state=fieldTrackStatus();
  fieldLine(state.recording?"กำลังบันทึก GPS ลง SD":"หยุดบันทึกอยู่",0);
  fieldLine(String(state.points)+" จุด / "+String(state.bytes/1024)+" KB",1);
  fieldLine(state.error.length()?state.error:fieldNotice,2,C_ACCENT);
  addBtn(6,CONTENT_Y+88,SCR_W-12,38,B_REC_TOGGLE,state.recording?"หยุดบันทึก":"เริ่มบันทึกใหม่");
  addBtn(6,CONTENT_Y+131,SCR_W-12,38,B_REVERSE,"ย้อนรอยบนแผนที่",I_NONE,state.available&&!state.recording);
  addBtn(6,CONTENT_Y+174,SCR_W-12,38,B_STOP_REVERSE,"ปิดเส้นย้อนรอย",I_NONE,state.backtracking);
  FootDef f[]={{B_BACK,"กลับ",I_LEFT,true}};drawFooter(f,1);
}

static void drawHardware() {
  btns.clear();tft.fillScreen(C_BG);drawHeader("แบตเตอรี่และเสียง",true);
  FieldConfig config=fieldConfig();FieldBattery battery=fieldBattery();
  fieldLine(battery.valid?String(battery.volts,3)+" V / ~"+String(battery.percent)+"%":"วงจรวัด ADC35 / แบต 1 เซลล์",0);
  fieldLine("47k + 47k และ C 100nF",1,C_MUTED);
  addBtn(6,CONTENT_Y+56,SCR_W-12,38,B_BATTERY,config.batteryEnabled?"วัดแบต: เปิด":"วัดแบต: ปิด");
  addBtn(6,CONTENT_Y+101,SCR_W-12,38,B_SOUND,config.soundEnabled?"เสียงเตือน: เปิด":"เสียงเตือน: ปิด");
  addBtn(6,CONTENT_Y+146,SCR_W-12,38,B_SOUND_TEST,"ทดสอบลำโพง");
  fieldLine(fieldNotice,7,C_ACCENT);
  FootDef f[]={{B_BACK,"กลับ",I_LEFT,true}};drawFooter(f,1);
}

static void drawLoRa() {
  btns.clear();tft.fillScreen(C_BG);drawHeader("วิทยุ E220",true);
  fieldLine(fieldLoRaStateText(),0,C_ACCENT);
  FieldRadioMessage items[2];uint32_t latest;
  int count=fieldLoRaSnapshot(items,2,&latest);
  for(int i=0;i<count;i++) fieldLine(String(items[i].mine?"> ":"< ")+items[i].text,i+1);
  fieldLine(fieldNotice,3,C_MUTED);
  FieldConfig config=fieldConfig();
  addBtn(4,CONTENT_Y+113,(SCR_W-12)/2,36,B_LORA_ENABLE,config.loraEnabled?"ปิดโมดูล":"เปิดโมดูล");
  addBtn(SCR_W/2+2,CONTENT_Y+113,(SCR_W-12)/2,36,B_LORA_CONFIG,"ตั้งค่า 11",I_NONE,config.loraEnabled);
  addBtn(4,CONTENT_Y+155,(SCR_W-12)/2,36,B_LORA_NORMAL,"ใช้งาน 00",I_NONE,config.loraEnabled);
  addBtn(SCR_W/2+2,CONTENT_Y+155,(SCR_W-12)/2,36,B_LORA_WRITE,"พิมพ์ส่ง",I_NONE,fieldLoRaState()==FieldLoRaState::Ready);
  FootDef f[]={{B_BACK,"กลับ",I_LEFT,true},{B_LORA_SEND_POS,"ส่งพิกัด",I_NONE,gpsGet().valid&&fieldLoRaState()==FieldLoRaState::Ready}};drawFooter(f,2);
}

static void redraw() {
  switch (screen) {
    case SC_FIELD: drawField();break;
    case SC_READY: drawReady();break;
    case SC_TRACK: drawTrack();break;
    case SC_HARDWARE: drawHardware();break;
    case SC_LORA: drawLoRa();break;

    case SC_WHERE: drawWhere(); break;
    case SC_LOST: drawLost(); break;
    case SC_GOTO: drawGoto(); updateGoto(); break;
    case SC_CLOCK: drawClock(); updateClock(); break;
    case SC_TRIP: drawTrip(); updateTrip(); break;
    case SC_ALARM: drawAlarm(); break;
    case SC_MAP: drawMap(); break;
    case SC_MAP_SETS: drawMapSets(); break;
    case SC_TRIAGE: drawTriage(); break;
    case SC_WPS: drawWps(); break;
    case SC_POIS: drawPois(); break;
    case SC_NEAR: drawNear(); break;
    case SC_RADAR: drawRadar(); break;
    case SC_MESH: meshMarkRead(); drawMesh(); break;
    case SC_QUICK: drawQuick(); break;
    case SC_KEYS: drawKeys(); break;
    case SC_HOME: drawHome(); break;
    case SC_CATEGORY: drawList(cats[curCat].name, false); break;
    case SC_LIBRARY: drawList("คลังคู่มือ", true); break;
    case SC_WIFI: drawWifi(); break;
    case SC_SETTINGS: drawSettings(); break;
    case SC_INFO: drawInfo(); break;
    case SC_NOSD: drawNoSD(); break;
    case SC_ARTICLE:
    case SC_BOOK:
      btns.clear();
      drawHeader(screen == SC_BOOK ? list[bookIdx].title : articleTitle, true);
      drawDocView();
      drawDocFooter();
      break;
  }
}

// ---------------------------------------------------------------- touch
#if defined(BOARD_CYD_28)
#include "touch28.h"
static Touch28 touch28;

static void touchBegin() { touch28.begin(SCR_W, SCR_H); }
static bool touchRead(uint16_t *x, uint16_t *y) { return touch28.read(*x, *y); }
static bool touchHeld() { return touch28.pressed(); }

// Returns false if nothing was tapped within 15 s.
static bool waitTap(int px, int py, uint16_t &rx, uint16_t &ry) {
  while (touch28.pressed()) delay(10);                 // wait for release
  tft.fillScreen(TFT_BLACK);
  tft.drawCircle(px, py, 10, TFT_MAGENTA);
  tft.drawFastHLine(px - 14, py, 29, TFT_MAGENTA);
  tft.drawFastVLine(px, py - 14, 29, TFT_MAGENTA);
  uint32_t t0 = millis();
  while (!touch28.readRaw(rx, ry)) {
    if (millis() - t0 > 15000) return false;
    delay(10);
  }
  delay(150);
  touch28.readRaw(rx, ry);                             // settled reading
  while (touch28.pressed()) delay(10);
  return true;
}

static void calibrateTouch() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextFont(2);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("เทียบสัมผัส", SCR_W / 2, SCR_H / 2 - 20);
  tft.drawString("แตะเครื่องหมายที่ปรากฏทีละจุด", SCR_W / 2, SCR_H / 2 + 6);
  delay(1500);
  uint16_t a, b, c, d, e, f;
  bool ok = false, tapped = true;
  for (int attempt = 0; attempt < 4 && tapped && !ok; attempt++) {
    tapped = waitTap(CAL_INSET, CAL_INSET, a, b) &&
             waitTap(SCR_W - CAL_INSET, CAL_INSET, c, d) &&
             waitTap(CAL_INSET, SCR_H - CAL_INSET, e, f);
    if (!tapped) break;
    touch28.setFromTaps(a, b, c, d, e, f);
    // Sanity check: both axes must span a real part of the panel. A collapsed range
    // (finger carried over, same spot tapped twice) would leave a sliver of touch area.
    ok = (touch28.xMax - touch28.xMin) > 1500 && (touch28.yMax - touch28.yMin) > 1500 &&
         abs((int)c - (int)a) + abs((int)d - (int)b) > 1200 &&
         abs((int)e - (int)a) + abs((int)f - (int)b) > 1200;
    if (!ok) {
      tft.fillScreen(TFT_BLACK);
      tft.setTextColor(TFT_RED);
      tft.drawString("ไม่พบการแตะ โปรดลองอีกครั้ง", SCR_W / 2, SCR_H / 2);
      delay(1500);
    }
  }
  if (tapped && ok) {
    // calibration good
  } else {
    // No taps: keep the built-in defaults (the ranges from the working ESPHome CYD) and don't save.
    touch28 = Touch28();
    touch28.begin(SCR_W, SCR_H);
    return;
  }
  uint16_t cal[8] = {touch28.swapXY, touch28.flipX, touch28.flipY, touch28.xMin, touch28.xMax, touch28.yMin, touch28.yMax, 1};
  prefs.putBytes("cal28b", cal, sizeof(cal));
}

static bool loadTouchCal() {
  uint16_t cal[8];
  if (prefs.getBytes("cal28b", cal, sizeof(cal)) != sizeof(cal) || cal[7] != 1) return false;
  touch28.swapXY = cal[0]; touch28.flipX = cal[1]; touch28.flipY = cal[2];
  touch28.xMin = cal[3]; touch28.xMax = cal[4]; touch28.yMin = cal[5]; touch28.yMax = cal[6];
  if (touch28.xMax <= touch28.xMin || touch28.yMax <= touch28.yMin ||
      touch28.xMax - touch28.xMin < 1500 || touch28.yMax - touch28.yMin < 1500) {
    touch28 = Touch28();
    touch28.begin(SCR_W, SCR_H);
    return false;                                      // bad saved range: calibrate again
  }
  return true;
}
#else
static void touchBegin() {}
static bool touchRead(uint16_t *x, uint16_t *y) { return tft.getTouch(x, y, 500); }
static bool touchHeld() { return tft.getTouchRawZ() > 500; }
static bool loadTouchCal() {
  uint16_t cal[5];
  if (prefs.getBytes("cal", cal, sizeof(cal)) != sizeof(cal)) return false;
  tft.setTouch(cal);
  return true;
}

static void calibrateTouch() {
  uint16_t cal[5];
  tft.fillScreen(TFT_BLACK);
  tft.setTextFont(2);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("เทียบสัมผัส", SCR_W / 2, SCR_H / 2 - 30);
  tft.drawString("แตะมุมลูกศรที่ปรากฏทีละจุด", SCR_W / 2, SCR_H / 2);
  tft.drawString("(ใช้เล็บหรือปากกาสัมผัสจะแม่นยำ)", SCR_W / 2, SCR_H / 2 + 24);
  delay(1200);
  tft.calibrateTouch(cal, TFT_MAGENTA, TFT_BLACK, 15);
  prefs.putBytes("cal", cal, sizeof(cal));
  tft.setTouch(cal);
}
#endif

static bool startSD() {
  if (sdUp) return true;
  sdSPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  for (int i = 0; i < 3; i++) {
    if (SD.begin(PIN_SD_CS, sdSPI, 20000000)) { sdUp = true; return true; }
    delay(200);
  }
  return false;
}

static void startup() {
  if (!startSD()) {
    Serial.println("[sd] card NOT found");
    startMesh();                                       // mesh works without a card (radio only, no hotspot)
    screen = SC_NOSD;
    redraw();
    return;
  }
  mapStorageBegin();
  fieldHardwareBegin(true);
  fieldTrackBegin();
  mapLoadCenter();
  loadConfig();
  loadCatalog();
  Serial.printf("[sd] OK: %d categories, %d articles\n", nCats, nArticles);
  if (wifiOn) webStart(wcfg);
  startMesh();
  screen = SC_HOME;
  redraw();
}

static void scrollDoc(int dir) {
  int maxScroll = max(0, doc.height() - CONTENT_H);
  int step = CONTENT_H - 44;
  if (screen == SC_BOOK && dir > 0 && scrollY >= maxScroll && bookPart < bookParts) { bookPart++; openBookPart(false); return; }
  if (screen == SC_BOOK && dir < 0 && scrollY <= 0 && bookPart > 1) { bookPart--; openBookPart(true); return; }
  int ns = constrain(scrollY + dir * step, 0, maxScroll);
  if (ns == scrollY) return;
  scrollY = ns;
  drawDocView();
  drawDocFooter();
}

static void goBack() {
  switch (screen) {
    case SC_FIELD: screen=SC_HOME;break;
    case SC_READY: case SC_TRACK: case SC_HARDWARE: case SC_LORA: screen=SC_FIELD;break;
    case SC_WHERE: screen=gpsHubFrom;break;
    case SC_LOST: screen=lostFromTriage?SC_TRIAGE:SC_WHERE;break;
    case SC_GOTO: screen=gotoFrom;break;
    case SC_CLOCK: case SC_TRIP: screen=SC_WHERE;break;
    case SC_ALARM: alarmEnd(false);return;
    case SC_INFO: screen=SC_SETTINGS;break;
    case SC_ARTICLE: doc.free(); if(articleFromLost){articleFromLost=false;screen=SC_LOST;} else if (articleFromTriage) { articleFromTriage = false; screen = SC_TRIAGE; } else screen = SC_CATEGORY; break;
    case SC_TRIAGE:
      if (triDepth > 0) triCur = triStack[--triDepth];
      else { screen = sdUp ? SC_HOME : SC_NOSD; }
      break;
    case SC_WPS: wpDelMode = false; wpGoMode = false; screen = SC_MAP; break;
    case SC_MAP_SETS: screen = SC_MAP; break;
    case SC_POIS: screen=SC_WPS;break;
    case SC_BOOK: doc.free(); screen = SC_LIBRARY; break;
    case SC_QUICK:
    case SC_RADAR: screen = SC_NEAR; break;
    case SC_NEAR: screen=SC_MESH;break;
    case SC_KEYS: screen = kbTarget==3 ? SC_LORA : kbTarget==2 ? SC_POIS : SC_MESH; break;
    default: list.clear(); screen = sdUp ? SC_HOME : SC_NOSD; break;
  }
  redraw();
}

// ---------------------------------------------------------------- flashlight + SOS
// Tap cycles white light -> red light (keeps night vision) -> SOS strobe -> exit.
static const uint8_t SOS_UNITS[] = {1,1, 1,1, 1,3,  3,1, 3,1, 3,3,  1,1, 1,1, 1,7};   // on,off,on,off... in units
static const int SOS_UNIT_MS = 220;
static int sosIdx = 0;
static uint32_t sosNextMs = 0;
static bool sosOn = false;

static void setRgbLed(bool on) {
  digitalWrite(PIN_LED_R, on ? LOW : HIGH);
  digitalWrite(PIN_LED_G, on ? LOW : HIGH);
  if (!fieldConfig().loraEnabled) digitalWrite(PIN_LED_B, on ? LOW : HIGH);
}

static void drawLight() {
  static const char *HINTS[] = {"แตะ: ไฟแดง", "แตะ: ไฟกะพริบ SOS", "แตะ: ออก"};
  uint16_t bg = (lightMode == 1) ? TFT_RED : TFT_WHITE;
  if (lightMode == 2) bg = TFT_BLACK;                  // SOS starts dark and flashes
  tft.fillScreen(bg);
  tft.setTextFont(2);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(lightMode == 1 ? TFT_BLACK : (lightMode == 2 ? TFT_DARKGREY : TFT_BLACK), bg);
  tft.drawString(HINTS[lightMode], SCR_W / 2, SCR_H - 16);
  sosIdx = 0;
  sosOn = false;
  sosNextMs = millis();
}

static void enterLight() {
  screen = SC_LIGHT;
  lightMode = 0;
  btns.clear();
  setBacklight(255);
  drawLight();
}

static void exitLight() {
  setRgbLed(false);
  setBacklight(brightness);
  screen = sdUp ? SC_HOME : SC_NOSD;
  redraw();
}

static void lightTap() {
  lightMode++;
  setRgbLed(false);
  if (lightMode > 2) { exitLight(); return; }
  drawLight();
}

static void lightLoop(uint32_t now) {
  if (lightMode != 2 || (int32_t)(now - sosNextMs) < 0) return;
  sosOn = (sosIdx % 2 == 0);                           // even steps are "on"
  tft.fillScreen(sosOn ? TFT_WHITE : TFT_BLACK);
  setRgbLed(sosOn);
  sosNextMs = now + (uint32_t)SOS_UNITS[sosIdx] * SOS_UNIT_MS;
  sosIdx = (sosIdx + 1) % (int)(sizeof(SOS_UNITS));
}

// ---------------------------------------------------------------- night dim (light sensor)
#if PIN_LDR >= 0
// Measured on the board: ~0 in bright light, ~60 in a lit room, ~270 with a finger over it (higher = darker).
#define LDR_DARK_ADC   200     // dim the screen above this
#define LDR_LIGHT_ADC  120     // back to normal below this
static int ldrRead() {
  long sum = 0;
  for (int i = 0; i < 8; i++) sum += analogRead(PIN_LDR);
  return sum / 8;
}

static void nightLoop(uint32_t now) {
  static uint32_t next = 0;
  static uint32_t logMs = 0;
  if ((int32_t)(now - next) < 0) return;
  next = now + 500;
  int v = ldrRead();
  if (now - logMs > 3000) { logMs = now; Serial.printf("[ldr] %d\n", v); }
  if (!nightAuto || sleeping || screen == SC_LIGHT) return;
  bool wasNight = nightNow;
  if (!nightNow && v > LDR_DARK_ADC) nightNow = true;
  else if (nightNow && v < LDR_LIGHT_ADC) nightNow = false;
  if (nightNow != wasNight) {
    setBacklight(nightNow ? min<int>(brightness, 25) : brightness);
  }
}
#else
static void nightLoop(uint32_t) {}
#endif

// ---------------------------------------------------------------- settings backup on the SD card
#define F_BACKUP "/swiki-settings.txt"
#if defined(BOARD_CYD_28)
  #define CAL_KEY "cal28b"
  #define CAL_N 8
#else
  #define CAL_KEY "cal"
  #define CAL_N 5
#endif

static void saveBackup() {
  if (!sdUp) return;
  SD.remove(F_BACKUP);
  File f = SD.open(F_BACKUP, FILE_WRITE);
  if (!f) return;
  f.println("# ไฟล์สำรองการตั้งค่า (กู้คืนอัตโนมัติหลังแฟลชใหม่)");
  f.printf("bright=%u\n", brightness);
  f.printf("tmo=%u\n", timeoutIdx);
  f.printf("inv=%d\n", invertColors ? 1 : 0);
  f.printf("wifi=%d\n", wifiOn ? 1 : 0);
  f.printf("night=%d\n", nightAuto ? 1 : 0);
  f.printf("mesh=%d\n", meshEnabled ? 1 : 0);
  f.printf("nick=%s\n", meshName());
  uint16_t cal[CAL_N];
  if (prefs.getBytes(CAL_KEY, cal, sizeof(cal)) == sizeof(cal)) {
    f.print("cal=");
    for (int i = 0; i < CAL_N; i++) f.printf(i ? ",%u" : "%u", cal[i]);
    f.println();
  }
  f.close();
}

static void restoreBackup() {
  if (!sdUp) return;
  File f = SD.open(F_BACKUP, FILE_READ);
  if (!f) return;
  while (f.available()) {
    String l = readLine(f);
    l.trim();
    int eq = l.indexOf('=');
    if (l.isEmpty() || l[0] == '#' || eq < 0) continue;
    String k = l.substring(0, eq), v = l.substring(eq + 1);
    if (k == "bright") prefs.putUChar("bright", constrain(v.toInt(), 20, 255));
    else if (k == "tmo") prefs.putUChar("tmo", v.toInt() % 5);
    else if (k == "inv") prefs.putBool("inv", v.toInt() != 0);
    else if (k == "wifi") prefs.putBool("wifi", v.toInt() != 0);
    else if (k == "night") prefs.putBool("night", v.toInt() != 0);
    else if (k == "mesh") prefs.putBool("mesh", v.toInt() != 0);
    else if (k == "nick" && v.length()) { char nick[MESH_NAME_LEN];utf8Copy(nick,v.c_str(),sizeof(nick));prefs.putString("nick",nick); }
    else if (k == "cal") {
      uint16_t cal[CAL_N];
      int n = 0, start = 0;
      while (n < CAL_N && start <= (int)v.length()) {
        int comma = v.indexOf(',', start);
        if (comma < 0) comma = v.length();
        cal[n++] = (uint16_t)v.substring(start, comma).toInt();
        start = comma + 1;
      }
      if (n == CAL_N) prefs.putBytes(CAL_KEY, cal, sizeof(cal));
    }
  }
  f.close();
}

// ---------------------------------------------------------------- easter egg
static void goodBoy() {
  tft.fillScreen(C_BG);
  int cx = SCR_W / 2, cy = SCR_H / 2 - 20;
  uint16_t pad = C_ACCENT;
  tft.fillEllipse(cx, cy + 22, 34, 26, pad);                 // big pad
  tft.fillEllipse(cx - 44, cy - 14, 11, 15, pad);            // toes
  tft.fillEllipse(cx - 16, cy - 36, 11, 15, pad);
  tft.fillEllipse(cx + 16, cy - 36, 11, 15, pad);
  tft.fillEllipse(cx + 44, cy - 14, 11, 15, pad);
  tft.setFreeFont(SCR_W < 300 ? &FreeSansBold12pt7b : &FreeSansBold18pt7b);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_TEXT);
  tft.drawString("เจ้าตูบแสนดี!", cx, cy + 80);
  if (goodboyIdx < 0) {                                       // dog guides not on the SD card yet
    tft.setTextFont(2);
    tft.setTextColor(C_MUTED);
    tft.drawString("คัดลอกไฟล์ sdcard รุ่นล่าสุด", cx, cy + 112);
    tft.drawString("เพื่อเปิดคู่มือปฐมพยาบาลสุนัข", cx, cy + 142);
    delay(2500);
    redraw();
    return;
  }
  delay(1400);
  curCat = goodboyIdx;
  listPage = 0;
  loadCategoryList(curCat);
  screen = SC_CATEGORY;
  redraw();
}

static void startMesh() {
  if (meshEnabled) meshBegin(wcfg.channel, webRunning());
}

static void onButton(uint16_t id) {
  if (id >= B_KEY_BASE) {                              // on-screen keyboard character
    uint32_t cp=id-B_KEY_BASE;
    if(!kbThai&&cp<128&&kbShift&&isalpha((unsigned char)cp))cp=toupper(cp);
    char encoded[5]={0};
    if(cp<128)encoded[0]=cp;
    else if(cp<2048) {encoded[0]=0xC0|(cp>>6);encoded[1]=0x80|(cp&63);}
    else {encoded[0]=0xE0|(cp>>12);encoded[1]=0x80|((cp>>6)&63);encoded[2]=0x80|(cp&63);}
    if(kbBuf.length()+strlen(encoded)<=(unsigned)kbMax())kbBuf+=encoded;
    drawKbText();
    return;
  }
  if (id >= B_TRI_BASE && id < B_TRI_BASE + 5) { triPick(id - B_TRI_BASE); return; }
  if (id >= B_QUICK_BASE && id < B_QUICK_BASE + 10) {  // quick message
    static const char *QUICK[] = {"ปลอดภัย", "ขอความช่วยเหลือ", "กำลังออกเดินทาง", "อยู่ที่พัก", "พบน้ำแล้ว", "มาหาฉัน"};
    if (id - B_QUICK_BASE < 6) meshSend(QUICK[id - B_QUICK_BASE]);
    screen = SC_MESH;
    redraw();
    return;
  }
  if (id >= B_FIELD && id <= B_LORA_SEND_POS) {
    switch(id){
      case B_FIELD: screen=SC_FIELD;break;
      case B_READY: suggestedMap={};fieldNotice="";screen=SC_READY;break;
      case B_TRACK: fieldNotice="GPX ดาวน์โหลดผ่านหน้าแผนที่มือถือ";screen=SC_TRACK;break;
      case B_HARDWARE: fieldNotice="";screen=SC_HARDWARE;break;
      case B_LORA: fieldNotice="M0/M1 สลับ 11 ตั้งค่า / 00 ใช้";screen=SC_LORA;break;
      case B_REC_TOGGLE: if(!fieldTrackSetRecording(!fieldTrackRecording()))fieldNotice="บันทึกไม่ได้ ตรวจ SD";break;
      case B_REVERSE: if(fieldTrackStartBacktrack()){screen=SC_MAP;mapFollow=false;}else fieldNotice="ไม่มีเส้นทางย้อนรอย";break;
      case B_STOP_REVERSE: fieldTrackClearBacktrack();break;
      case B_SUGGEST: suggestMap();break;
      case B_USE_SUGGEST:
        if(mapStorageSelect(suggestedMap.path)){fieldTrackLoop();mapLoadCenter();wpLoad();trkLoad();offlineRouteRefresh();mapFollow=false;screen=SC_MAP;}
        else fieldNotice="เปิดชุดแผนที่ไม่ได้";
        break;
      case B_BATTERY: case B_SOUND: {
        FieldConfig config=fieldConfig();
        if(id==B_BATTERY)config.batteryEnabled=!config.batteryEnabled;
        else config.soundEnabled=!config.soundEnabled;
        fieldNotice=fieldSaveConfig(config)?"บันทึกแล้ว":"บันทึกไม่ได้ ตรวจ SD";break;
      }
      case B_SOUND_TEST: fieldAlert(FieldAlert::Test);break;
      case B_LORA_ENABLE: {
        FieldConfig config=fieldConfig();config.loraEnabled=!config.loraEnabled;
        fieldNotice=fieldSaveConfig(config)?"เปลี่ยนการตั้งค่าแล้ว":"บันทึกไม่ได้ ตรวจ SD";break;
      }
      case B_LORA_CONFIG: fieldNotice=fieldLoRaConfigure()?"กำลังอ่านและตรวจโมดูล":"ตั้ง M0=M1 เป็น 1 ก่อน";break;
      case B_LORA_NORMAL: fieldNotice=fieldLoRaNormalMode()?"พร้อมส่งข้อความ":"ตรวจสถานะและ M0/M1";break;
      case B_LORA_WRITE: kbTarget=3;kbBuf="";kbShift=false;screen=SC_KEYS;break;
      case B_LORA_SEND_POS:{GpsFix fix=gpsGet();fieldNotice=fix.valid&&fieldLoRaSend(String("พิกัด ")+String(fix.lat,6)+","+String(fix.lon,6))?"ส่งไปยังโมดูลแล้ว":"ส่งไม่สำเร็จ";break;}
      default:break;
    }
    redraw();return;
  }
  if (id >= B_WHERE && id <= B_TRIP_RESET) { switch(id) {
    case B_WHERE: gpsHubFrom = screen; screen = SC_WHERE; redraw(); break;
    case B_TRIP: screen = SC_TRIP; redraw(); break;
    case B_TRIP_RESET: tripReset(); updateTrip(); break;
    case B_ALARM_CYCLE: tbIdx = (tbIdx + 1) % 5; prefs.putUChar("tb", tbIdx); alarmDay = -1; drawClock(); updateClock(); break;
    case B_ALARM_OK: alarmEnd(false); break;
    case B_ALARM_SNOOZE: alarmEnd(true); break;
    case B_LOST: lostFromTriage = false; screen = SC_LOST; redraw(); break;
    case B_GOTO: gotoIdx = 0; gotoFrom = SC_WHERE; screen = SC_GOTO; redraw(); break;
    case B_CLOCK: screen = SC_CLOCK; redraw(); break;
    case B_GOTO_START: {
      wpLoad();
      int i = wpFind("START");
      if (i < 0) { gsSay("กรุณาตั้งจุดเริ่มก่อน"); updateLost(); break; }
      gotoIdx = i; gotoFrom = SC_LOST; screen = SC_GOTO; redraw();
      break;
    }
    case B_SEND_SPOT: sendSpot(); updateLost(); break;
    case B_SET_START: wpSetStart(); updateLost(); break;
    case B_GOTO_PREV: if (nWps) { gotoIdx = (gotoIdx + nWps - 1) % nWps; updateGoto(); } break;
    case B_GOTO_NEXT: if (nWps) { gotoIdx = (gotoIdx + 1) % nWps; updateGoto(); } break;
    case B_LOST_GUIDE: {
      Entry e{String("staying-found"), triTitle(String("staying-found")), ""};
      articleFromLost = true; articleFromTriage = false;
      openArticle(e);
      break;
    }
    case B_WP_GO: wpGoMode = !wpGoMode; if (wpGoMode) wpDelMode = false; drawWps(); break;
    case B_TZ_DN:
    case B_TZ_UP: {
      if (tzAuto) { tzHours = haveLast ? (int8_t)lround(lastLon / 15.0) : 0; tzAuto = false; }
      tzHours = constrain(tzHours + (id == B_TZ_UP ? 1 : -1), -12, 14);
      sunCalcMs = 0; clockSave(); drawClock(); updateClock();
      break;
    }
    case B_TZ_DST: tzDst = !tzDst; sunCalcMs = 0; clockSave(); drawClock(); updateClock(); break;
    case B_TZ_AUTO: tzAuto = true; sunCalcMs = 0; clockSave(); drawClock(); updateClock(); break;
    case B_TZ_24H: clock24 = !clock24; clockSave(); drawClock(); updateClock(); break;
default:break;}return;}
  if (id >= B_ROW) {
    int idx = id - B_ROW;
    if (screen == SC_MAP_SETS) { mapSetTap(idx); return; }
    if (screen == SC_WPS) { wpRowTap(idx); return; }
    if (screen == SC_POIS) {if(idx<poiCount){OfflinePoi &p=poiItems[idx];offlineSetDestination(p.lat,p.lon,p.name);mapFollow=false;if(!mapReady)mapLoadCenter();if(mapReady)mapLatLonToPx(p.lat,p.lon,mapZ,mapCx,mapCy);screen=SC_MAP;redraw();}return;}
    if (idx >= (int)list.size()) return;
    if (screen == SC_CATEGORY) openArticle(list[idx]);
    else if (screen == SC_LIBRARY) {
      bookIdx = idx;
      bookParts = max(1, (int)list[idx].extra.toInt());
      bookPart = 1;
      openBookPart(false);
    }
    return;
  }
  if (id >= B_TILE) {
    curCat = id - B_TILE;
    listPage = 0;
    loadCategoryList(curCat);
    screen = SC_CATEGORY;
    redraw();
    return;
  }
  switch (id) {
    case B_BACK: goBack(); break;
    case B_UP:
      if (screen == SC_ARTICLE || screen == SC_BOOK) scrollDoc(-1);
      else if (listPage > 0) { listPage--; redraw(); }
      break;
    case B_DOWN:
      if (screen == SC_ARTICLE || screen == SC_BOOK) scrollDoc(1);
      else if ((listPage + 1) * listRowsPerPage() < (int)list.size()) { listPage++; redraw(); }
      break;
    case B_PREV:
      if(screen==SC_MAP_SETS){mapSetOffset=mapSetOffset>=4?mapSetOffset-4:0;loadMapSets();redraw();}
      else if(screen==SC_POIS){if(!poiHistory.empty()){poiOffset=poiHistory.back();poiHistory.pop_back();loadPois();redraw();}}
      else if (screen == SC_HOME) { if (homePage > 0) { homePage--; redraw(); } }
      else if(screen==SC_NEAR) { nearPage--;redraw(); }
      else if (bookPart > 1) { bookPart--; openBookPart(false); }
      break;
    case B_NEXT:
      if(screen==SC_MAP_SETS){if(mapSetNext!=UINT32_MAX){mapSetOffset=mapSetNext;loadMapSets();redraw();}}
      else if(screen==SC_POIS){if(poiNext!=UINT32_MAX){if(poiHistory.size()<128)poiHistory.push_back(poiOffset);poiOffset=poiNext;loadPois();redraw();}}
      else if (screen == SC_HOME) { homePage++; redraw(); }   // drawHome clamps to the last page
      else if(screen==SC_NEAR) { nearPage++;redraw(); }
      else if (bookPart < bookParts) { bookPart++; openBookPart(false); }
      break;
    case B_LIBRARY: listPage = 0; loadLibraryList(); screen = SC_LIBRARY; redraw(); break;
    case B_WIFI: screen = SC_WIFI; redraw(); break;
    case B_SETTINGS: screen = SC_SETTINGS; redraw(); break;
    case B_INFO: screen=SC_INFO;redraw();break;
    case B_LIGHT: enterLight(); break;
    case B_MESH: screen = SC_MESH; redraw(); break;
    case B_MESH_TOGGLE:
      meshEnabled = !meshEnabled;
      prefs.putBool("mesh", meshEnabled);
      if (meshEnabled) startMesh(); else meshEnd();
      saveBackup();
      redraw();
      break;
    case B_MESH_QUICK: screen = SC_QUICK; redraw(); break;
    case B_MESH_NEAR: screen = SC_NEAR; redraw(); break;
    case B_RADAR: screen = SC_RADAR; redraw(); break;
    case B_GPS_SHARE: gpsShare = !gpsShare; prefs.putBool("gpsshare", gpsShare); redraw(); break;
    case B_MAP_SETS: mapSetOffset=0;mapSetError="";loadMapSets();screen=SC_MAP_SETS;redraw();break;
    case B_MAP: if (!mapReady) mapLoadCenter(); if (!trkLoaded) trkLoad(); wpLoad(); screen = SC_MAP; redraw(); break;
    case B_MAP_IN:
      if (mapZ < mapMaxZ) { mapZ++; mapCx *= 2; mapCy *= 2; redraw(); }
      break;
    case B_MAP_OUT:
      if (mapZ > mapMinZ) { mapZ--; mapCx /= 2; mapCy /= 2; redraw(); }
      break;
    case B_TRIAGE: triCur = &TRIAGE[0]; triDepth = 0; screen = SC_TRIAGE; redraw(); break;
    case B_MAP_ME: {
      GpsFix g = gpsGet();
      if (mapReady && g.valid) { mapFollow=!mapFollow; mapLatLonToPx(g.lat, g.lon, mapZ, mapCx, mapCy); redraw(); }
      break;
    }
    case B_MAP_MARK: wpMark(); break;
    case B_MAP_PTS: wpLoad(); wpDelMode = false; wpGoMode = false; screen = SC_WPS; redraw(); break;
    case B_WP_DEL: wpGoMode=false; wpDelMode = !wpDelMode; drawWps(); break;
    case B_WP_CLEAR:
      if (millis() - wpClearMs < 3000) { nWps = 0; wpDelMode = false; wpSave(); wpClearMs = 0; }
      else wpClearMs = millis();
      drawWps();
      break;
    case B_TRK_CLEAR:
      if (millis() - trkClearMs < 3000) { trkClear(); trkClearMs = 0; }
      else trkClearMs = millis();
      drawWps();
      break;
    case B_MAP_SEARCH: poiOffset=0;poiHistory.clear();loadPois();screen=SC_POIS;redraw();break;
    case B_POI_QUERY: kbTarget=2;kbBuf=poiQuery;kbShift=false;screen=SC_KEYS;redraw();break;
    case B_MESH_TYPE: kbTarget = 0; kbBuf = ""; kbShift = false; screen = SC_KEYS; redraw(); break;
    case B_MESH_NAME: kbTarget = 1; kbBuf = meshName(); kbShift = false; screen = SC_KEYS; redraw(); break;
    case B_KB_LANG: kbThai=!kbThai; drawKeys(); break;
    case B_KB_PAGE: kbShift=!kbShift; drawKeys(); break;
    case B_KB_DEL: if (kbBuf.length()) kbBuf.remove(utf8PreviousCluster(kbBuf.c_str(),kbBuf.length())); drawKbText(); break;
    case B_KB_SHIFT: kbShift = !kbShift; drawKeys(); break;
    case B_KB_SEND:
      if(kbTarget==3){fieldNotice=fieldLoRaSend(kbBuf)?"เข้าคิวส่งแล้ว":"ส่งไม่ได้ ตรวจโมดูล";screen=SC_LORA;redraw();break;}
      if(kbTarget==2){poiQuery=kbBuf;poiQuery.trim();poiOffset=0;poiHistory.clear();loadPois();screen=SC_POIS;redraw();break;}
      if (kbTarget == 0) {
        kbBuf.trim();
        if (kbBuf.length()) meshSend(kbBuf.c_str());
      } else {
        meshSetName(kbBuf.c_str());
        prefs.putString("nick", meshName());
        saveBackup();
      }
      screen = SC_MESH;
      redraw();
      break;
    case B_NIGHT:
      nightAuto = !nightAuto;
      prefs.putBool("night", nightAuto);
      if (!nightAuto && nightNow) { nightNow = false; gNightUI = false; setBacklight(brightness); }
      saveBackup();
      redraw();
      break;
    case B_BACKUP_SAVE: saveBackup(); showLoading("บันทึกลงการ์ด SD แล้ว"); delay(900); redraw(); break;
    case B_BACKUP_LOAD:
      if (!sdUp || !SD.exists(F_BACKUP)) { showLoading("ไม่มีไฟล์สำรองในการ์ด SD"); delay(1200); redraw(); break; }
      restoreBackup();
      showLoading("กู้คืนแล้ว กำลังเริ่มใหม่...");
      delay(900);
      ESP.restart();
      break;
    case B_WIFI_TOGGLE:
      wifiOn = !wifiOn;
      prefs.putBool("wifi", wifiOn);
      saveBackup();
      showLoading(wifiOn ? "กำลังเปิดฮอตสปอต..." : "กำลังปิดฮอตสปอต...");
      meshEnd();                                        // the radio is reconfigured; mesh restarts after
      if (wifiOn) webStart(wcfg); else webStop();
      startMesh();
      redraw();
      break;
    case B_BRIGHT_DN: brightness = max(20, brightness - 30); setBacklight(nightNow ? min<int>(brightness, 25) : brightness); prefs.putUChar("bright", brightness); saveBackup(); redraw(); break;
    case B_BRIGHT_UP: brightness = min(255, brightness + 30); setBacklight(nightNow ? min<int>(brightness, 25) : brightness); prefs.putUChar("bright", brightness); saveBackup(); redraw(); break;
    case B_TIMEOUT: timeoutIdx = (timeoutIdx + 1) % 5; prefs.putUChar("tmo", timeoutIdx); saveBackup(); redraw(); break;
    case B_INVERT: invertColors = !invertColors; tft.invertDisplay(invertColors); prefs.putBool("inv", invertColors); saveBackup(); redraw(); break;
    case B_CALIB: calibrateTouch(); saveBackup(); redraw(); break;
    case B_RETRY: startup(); break;
  }
}

static void handleTap(int x, int y) {
  if (screen == SC_LIGHT) { lightTap(); return; }
  // Easter egg: tap the title on the home screen five times.
  if (screen == SC_HOME && y < HEADER_H && x < SCR_W - 70) {
    static uint8_t eggTaps = 0;
    static uint32_t eggT0 = 0;
    uint32_t now = millis();
    if (now - eggT0 > 3000) eggTaps = 0;
    if (eggTaps == 0) eggT0 = now;
    if (++eggTaps >= 5) { eggTaps = 0; goodBoy(); return; }
  }
  for (size_t i = 0; i < btns.size(); i++) {
    Btn b = btns[i];
    if (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h) {
      if (!b.enabled) return;
      if (b.id >= B_KEY_BASE) {
        // keyboard keys: no highlight, the text box updates instead
      } else if (b.id >= B_TILE) {      // tiles and list rows: quick highlight frame
        tft.drawRoundRect(b.x, b.y, b.w, b.h, 8, C_ACCENT);
        tft.drawRoundRect(b.x + 1, b.y + 1, b.w - 2, b.h - 2, 8, C_ACCENT);
      } else if (!(b.y == 0 && b.id == B_BACK)) {
        drawButton(b, true);
      }
      onButton(b.id);
      return;
    }
  }
}

// ---------------------------------------------------------------- main
void setup() {
  Serial.begin(115200);

  gpsBegin();                          // no-op unless the board profile wires a GPS (PIN_GPS_RX)

  // Audio amp (if present on 3.5" board)
#if PIN_AUDIO_EN >= 0
  pinMode(PIN_AUDIO_EN, OUTPUT);
  digitalWrite(PIN_AUDIO_EN, HIGH);   // speaker amp off
#endif

  // RGB LED pins (active low)
  pinMode(PIN_LED_R, OUTPUT); digitalWrite(PIN_LED_R, HIGH);
  pinMode(PIN_LED_G, OUTPUT); digitalWrite(PIN_LED_G, HIGH);
  pinMode(PIN_LED_B, OUTPUT); digitalWrite(PIN_LED_B, HIGH);

  // Deselect SPI devices early to avoid bus conflicts during init
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);
#if defined(TOUCH_CS) && (TOUCH_CS >= 0)
  pinMode(TOUCH_CS, OUTPUT);
  digitalWrite(TOUCH_CS, HIGH);
#endif

#if PIN_LDR >= 0
  pinMode(PIN_LDR, INPUT);
  analogSetPinAttenuation(PIN_LDR, ADC_11db);
#endif

  prefs.begin("swiki", false);
  // Fresh flash (no saved settings): bring them back from the SD card backup if there is one.
  if (!prefs.isKey("bright")) {
    if (startSD() && SD.exists(F_BACKUP)) { restoreBackup(); Serial.println("[backup] settings restored from SD"); }
    if (!prefs.isKey("bright")) prefs.putUChar("bright", 200);
  }
  brightness = prefs.getUChar("bright", 200);
  timeoutIdx = prefs.getUChar("tmo", 2) % 5;
  invertColors = prefs.getBool("inv", false);
  wifiOn = prefs.getBool("wifi", true);
  nightAuto = prefs.getBool("night", false);
  gpsShare = prefs.getBool("gpsshare", false);
  tbIdx=prefs.getUChar("tb",0)%5;tzHours=prefs.getChar("tz",7);tzAuto=prefs.getBool("tzauto",false);tzDst=prefs.getBool("dst",false);clock24=prefs.getBool("h24",true);
  if(prefs.isKey("llat")){lastLat=prefs.getFloat("llat",0);lastLon=prefs.getFloat("llon",0);haveLast=true;}
  meshEnabled = prefs.getBool("mesh", true);
  if (prefs.isKey("nick")) meshSetName(prefs.getString("nick").c_str());

  tft.init();
  tft.setRotation(0);
  tft.invertDisplay(invertColors);
  tft.fillScreen(C_BG);
  ledcSetup(0, 5000, 8);
  ledcAttachPin(PIN_BACKLIGHT, 0);
  setBacklight(brightness);

  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(nullptr);

  // Splash screen scaled to display height
  int splashCenter = SCR_H / 2 - 40;
  if (SCR_W < 300) tft.setFreeFont(&FreeSansBold12pt7b); else tft.setFreeFont(&FreeSansBold18pt7b);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_ACCENT);
  tft.drawString("คู่มือ", SCR_W / 2, splashCenter);
  tft.setTextColor(C_TEXT);
  tft.drawString("เอาตัวรอด", SCR_W / 2, splashCenter + 44);
  tft.setTextFont(2);
  tft.setTextColor(C_MUTED);
  tft.drawString("ข้อมูลภาคสนามแบบออฟไลน์", SCR_W / 2, splashCenter + 82);
  tft.drawString("แตะจอค้างเพื่อเทียบสัมผัส", SCR_W / 2, SCR_H - (SCR_W < 300 ? 24 : 40));

  touchBegin();
  bool haveCal = loadTouchCal();
  delay(1500);
  bool justCalibrated = false;
  if (!haveCal || touchHeld()) { calibrateTouch(); justCalibrated = true; }

  startup();
  if (justCalibrated) saveBackup();
  lastTouchMs = millis();
}

#ifdef SHOT_SERIAL
// Developer tool (build with -DSHOT_SERIAL=1): drive the UI and dump the screen over USB serial
// so docs screenshots come from the real device. Not part of the release binaries.
static void shotRow(int y) {
  static uint8_t row[SCR_W * 3];
  static char line[SCR_W * 6 + 16];
  tft.readRectRGB(0, y, SCR_W, 1, row);
  int n = snprintf(line, sizeof(line), "R%03d:", y);
  for (int x = 0; x < SCR_W * 3; x++) n += snprintf(line + n, sizeof(line) - n, "%02X", row[x]);
  line[n++] = '\n';
  for (int i = 0; i < n; i += 32) {            // small paced chunks: the CH340 drops bytes if flooded
    Serial.write((uint8_t *)line + i, min(32, n - i));
    Serial.flush();
    delay(3);
  }
}

// 'p' dumps every row as an ASCII line; 'r' + 3 digits re-sends a single row (the USB link drops
// bytes at this size, so the host re-requests damaged rows).
static void shotDump() {
  Serial.printf("SHOT %d %d\n", SCR_W, SCR_H);
  for (int y = 0; y < SCR_H; y++) shotRow(y);
  Serial.println("SHOTEND");
}

static void shotCommand(char c) {
  switch (c) {
    case 'p': shotDump(); break;
    case 'r': {                                   // followed by three digits: re-send that row
      char d[4] = {0};
      for (int i = 0; i < 3; i++) { uint32_t t0 = millis(); while (!Serial.available() && millis() - t0 < 200) {} d[i] = Serial.available() ? Serial.read() : '0'; }
      shotRow(atoi(d));
      break;
    }
    case 'H': goBack(); screen = SC_HOME; redraw(); break;
    case 'S': screen = SC_SETTINGS; redraw(); break;
    case 'W': screen = SC_WIFI; redraw(); break;
    case 'L': enterLight(); break;
    case 'F': meshDemo(); break;
    case 'Z': { bool ok = meshSend("ทดสอบจากบอร์ด"); Serial.printf("[mesh] sent test message: %s\n", ok ? "ok" : "FAILED (mesh off or rate limit)"); break; }
    case 'M': screen = SC_MESH; redraw(); break;
    case 'N': screen = SC_NEAR; redraw(); break;
    case 'P': if (!mapReady) mapLoadCenter(); screen = SC_MAP; redraw(); break;
    case 'I': onButton(B_MAP_IN); break;
    case 'O': onButton(B_MAP_OUT); break;
    case 'Q': screen = SC_QUICK; redraw(); break;
    case 'K': kbTarget = 0; kbBuf = "พบน้ำที่ลำธาร"; screen = SC_KEYS; redraw(); break;
    case 'T': lightTap(); break;
    case 'G': goodBoy(); break;
    case 'B': listPage = 0; loadLibraryList(); screen = SC_LIBRARY; redraw(); break;
    case 'C': curCat = 1; listPage = 0; loadCategoryList(curCat); screen = SC_CATEGORY; redraw(); break;
    case 'A': if (!list.empty()) openArticle(list[0]); break;
    case 'D': if (!list.empty()) { scrollY = 0; scrollDoc(1); } break;
  }
}
#endif

void loop() {
#ifdef SHOT_SERIAL
  while (Serial.available()) shotCommand(Serial.read());
#endif
  static bool wasDown = false;
  uint16_t tx, ty;
  bool down = touchRead(&tx, &ty);
  uint32_t now = millis();
  if (screen == SC_MAP) mapTouch(down, tx, ty);

  if (down && !wasDown) {
    lastTouchMs = now;
    if (sleeping) {               // first tap only wakes the screen
      sleeping = false;
      setBacklight(brightness);
    } else {
      handleTap(tx, ty);
    }
  }
  if (down) lastTouchMs = now;
  wasDown = down;

  if (screen == SC_LIGHT) { lastTouchMs = now; lightLoop(now); }   // flashlight keeps the screen awake
  nightLoop(now);
  gpsLoop();
  trkLoop(now);
  static uint32_t geoTick = 0;
  if (now - geoTick >= 1000) {
    geoTick = now;
    geoNote();
    if (!sleeping) geoRefresh();
  }
  tripLoop(now);
  alarmCheck(now);
  if(screen==SC_MAP&&!sleeping&&mapReady){
    static uint32_t tick=0,lastDest=0;static bool lastValid=false,lastPhone=false,lastData=false;
    static int32_t lastGpsX=INT32_MIN,lastGpsY=INT32_MIN;
    static int lastSpeed=-1,lastSats=-1;
    if(now-tick>=1000){
      tick=now;GpsFix g=gpsGet();OfflineDestination d=offlineDestination();bool routeChanged=offlineRouteRefresh();
      bool changed=g.valid!=lastValid||g.phone!=lastPhone||(g.sentences>0)!=lastData||g.sats!=lastSats||d.seq!=lastDest||routeChanged;
      if(g.valid){
        int32_t x,y;mapLatLonToPx(g.lat,g.lon,mapZ,x,y);
        if(x!=lastGpsX||y!=lastGpsY||(int)g.speedKmh!=lastSpeed)changed=true;
        lastGpsX=x;lastGpsY=y;lastSpeed=(int)g.speedKmh;
        if(mapFollow){lastTouchMs=now;if(abs(x-mapCx)>2||abs(y-mapCy)>2){mapCx=x;mapCy=y;changed=true;}}
      }
      if(changed){if(g.valid!=lastValid)drawMap();else mapDrawTiles();}
      lastValid=g.valid;lastPhone=g.phone;lastData=g.sentences>0;lastSats=g.sats;lastDest=d.seq;
    }
  }
  {                                                    // share our position with nearby boards (only when the user turned that on)
    GpsFix gf = gpsGet();
    meshSetMyPos(gpsShare && gf.valid, gf.lat, gf.lon);
  }
  if (screen == SC_RADAR && !sleeping) {               // keep the radar live
    static uint32_t radarMs = 0;
    if (now - radarMs > 2000) { radarMs = now; drawRadar(); }
  }

  fieldHardwareLoop(now);
  static uint32_t fieldDrawMs=0;
  if(!sleeping && now-fieldDrawMs>=2000){
    fieldDrawMs=now;
    if(screen==SC_READY)drawReady();
    else if(screen==SC_TRACK)drawTrack();
    else if(screen==SC_LORA)drawLoRa();
    else if(screen==SC_HARDWARE)drawHardware();
  }
  if(fieldLoRaTakeNew())fieldAlert(FieldAlert::Message);
  fieldTrackLoop();
  static uint32_t batteryDrawMs=0;
  if(!sleeping && screen!=SC_LIGHT && screen!=SC_ALARM && now-batteryDrawMs>=1000){batteryDrawMs=now;drawBatteryStatus();}
  meshLoop();
  {                                                    // a message sent from a phone changes the list too
    static uint32_t lastSeq = 0;
    uint32_t sq = meshChangeCount();                   // also changes when a "got it" reply arrives
    if (sq != lastSeq) { lastSeq = sq; if (screen == SC_MESH && !sleeping) redraw(); }
  }
  if (meshTakeNew()) {
    fieldAlert(FieldAlert::Message);                                 // a message arrived from another board
    if (sleeping) { sleeping = false; setBacklight(nightNow ? min<int>(brightness, 25) : brightness); }
    lastTouchMs = now;
    if (screen != SC_LIGHT) {
      digitalWrite(PIN_LED_G, LOW);                    // brief green blink
      ledOffMs = now + 300;
    }
    if (screen == SC_MESH || screen == SC_HOME) redraw();
  }
  if (ledOffMs && (int32_t)(now - ledOffMs) >= 0) { digitalWrite(PIN_LED_G, HIGH); ledOffMs = 0; }
  if (screen == SC_NEAR && !sleeping) {                // keep the signal bars live
    static uint32_t nearMs = 0;
    if (now - nearMs > 2000) { nearMs = now; drawNear(); }
  }

  uint16_t tmo = TIMEOUTS_S[timeoutIdx];
  if (!sleeping && tmo && now - lastTouchMs > (uint32_t)tmo * 1000) {
    sleeping = true;
    setBacklight(0);
  }

  // keep the phone counter fresh
  if (!sleeping && now - lastStatusMs > 2000) {
    lastStatusMs = now;
    int c = webClientCount();
    if (c != lastClients) {
      lastClients = c;
      if (screen == SC_HOME) drawWifiBadge();
      else if (screen == SC_WIFI) redraw();
    }
  }
  delay(15);
}
