// Survival Wiki - board configuration
// Supports:
//  - Hosyond / LCDWiki 3.5" ESP32-32E (E32R35T), ST7796U + XPT2046 resistive touch (320x480)
//  - Sunton 2.8" ESP32-2432S028R CYD, ILI9341 + XPT2046 resistive touch (240x320)
#pragma once

#define FW_VERSION "1.6.0-th4"

// ---- Hardware Profile ----
#if defined(BOARD_CYD_28)
  #define BOARD_NAME     "ESP32-2432S028 (2.8\" CYD)"
  #define SCR_W          240
  #define SCR_H          320
  #define PIN_BACKLIGHT   21   // LCD backlight PWM on 2.8" CYD
  #define PIN_SD_CS        5   // microSD on VSPI bus
  #define PIN_SD_SCK      18
  #define PIN_SD_MISO     19
  #define PIN_SD_MOSI     23
  #define PIN_LED_R        4   // RGB LED, active low
  #define PIN_LED_G       16
  #define PIN_LED_B       17
  #define PIN_AUDIO_EN    -1   // No external amp enable (GPIO 26 DAC audio out)
  #define PIN_LDR         34   // light sensor (input only)
  #define PIN_GPS_RX      27   // GPS TX goes here (CN1 IO27)
  #define PIN_GPS_TX      22   // spare CN1 pin; leave the GPS RX wire off
  #define HEADER_H        36
  #define FOOTER_H        38
  #define ROW_H           38
  #define TILE_GAP         6
#else
  #define BOARD_NAME     "Hosyond 3.5\" E32R35T"
  #define SCR_W          320
  #define SCR_H          480
  #define PIN_BACKLIGHT   27   // LCD backlight PWM on 3.5" E32R35T
  #define PIN_SD_CS        5   // microSD on VSPI bus
  #define PIN_SD_SCK      18
  #define PIN_SD_MISO     19
  #define PIN_SD_MOSI     23
  #define PIN_LED_R       22   // RGB LED, active low
  #define PIN_LED_G       16
  #define PIN_LED_B       17
  #define PIN_AUDIO_EN     4   // speaker amp enable, LOW = on (kept off)
  #define PIN_LDR         -1   // no light sensor wired on this board
  #define PIN_GPS_RX      -1   // GPS not wired yet: pick a free pin first
  #define PIN_GPS_TX      -1
  #define HEADER_H        44
  #define FOOTER_H        52
  #define ROW_H           48
  #define TILE_GAP         8
#endif

#define CONTENT_Y  HEADER_H
#define CONTENT_H  (SCR_H - HEADER_H - FOOTER_H)
#define MARGIN       8

// ---- Colors (RGB565) ----
// Night mode (2.8" light sensor): every UI color goes through NC(), which maps it to a dim red
// shade of the same brightness so the screen keeps your night vision.
extern bool gNightUI;
static inline uint16_t NC(uint16_t c) {
  if (!gNightUI) return c;
  uint16_t r = (c >> 8) & 0xF8, g = (c >> 3) & 0xFC, b = (c << 3) & 0xF8;
  uint16_t l = (r * 77 + g * 150 + b * 29) >> 8;     // 0..255 luma
  return (l & 0xF8) << 8;
}
#define C_BG        NC(0x0861)   // near-black blue
#define C_PANEL     NC(0x18E3)   // dark grey
#define C_HEADER    NC(0x2124)
#define C_TEXT      NC(0xFFFF)
#define C_MUTED     NC(0xAD55)
#define C_ACCENT    NC(0xFD20)   // safety orange
#define C_WARN_BG   NC(0x4A00)   // dark amber
#define C_WARN_BAR  NC(0xFD20)
#define C_LINE      NC(0x3186)
#define C_PRESS     NC(0x4A69)

// ---- Files on the SD card ----
#define F_CONFIG     "/config.txt"
#define F_CATEGORIES "/wiki/categories.tsv"
#define F_CATALOG    "/wiki/catalog.tsv"
#define D_ARTICLES   "/wiki/articles/"
#define F_LIBRARY    "/library/catalog.tsv"
#define D_LIBRARY    "/library/"
#define F_WEB_INDEX  "/web/index.html"

// ---- Limits (no PSRAM on this board: keep RAM use modest) ----
#define MAX_CATEGORIES   16
#define MAX_ARTICLES    400
#define MAX_BOOKS        40
#define MAX_DOC_BYTES  32768   // UTF-8 Thai expansion; reject larger files instead of truncating
