#pragma once
// XPT2046 touch for the 2.8" CYD (ESP32-2432S028R). On this board the touch chip
// has its own SPI pins (not the display bus), which TFT_eSPI's built-in touch
// can't use, so it is bit-banged here. Pins match the working ESPHome cyd-panel.
#include <Arduino.h>

#define T28_CLK 25
#define T28_MOSI 32
#define T28_MISO 39
#define T28_CS 33
#define T28_IRQ 36
#define T28_PRESS_MIN 400   // pressure reading that counts as a touch
#define CAL_INSET 32   // calibration markers sit this far in from the edges (panel corners are hard to reach)

struct Touch28 {
  // Calibration: which raw axis is screen X, direction flags, raw range per axis.
  bool swapXY = true;
  bool flipX = false, flipY = false;
  uint16_t xMin = 280, xMax = 3860, yMin = 340, yMax = 3860;   // raw ranges of the screen X / Y axes
  int w = 240, h = 320;

  void begin(int width, int height) {
    w = width; h = height;
    pinMode(T28_CLK, OUTPUT); pinMode(T28_MOSI, OUTPUT);
    pinMode(T28_MISO, INPUT); pinMode(T28_CS, OUTPUT);
    pinMode(T28_IRQ, INPUT);
    digitalWrite(T28_CS, HIGH); digitalWrite(T28_CLK, LOW);
  }

  uint16_t xfer(uint8_t cmd) {
    digitalWrite(T28_CS, LOW);
    for (int i = 7; i >= 0; i--) {
      digitalWrite(T28_MOSI, (cmd >> i) & 1);
      digitalWrite(T28_CLK, HIGH); digitalWrite(T28_CLK, LOW);
    }
    uint16_t v = 0;
    for (int i = 0; i < 16; i++) {
      digitalWrite(T28_CLK, HIGH);
      v = (v << 1) | digitalRead(T28_MISO);
      digitalWrite(T28_CLK, LOW);
    }
    digitalWrite(T28_CS, HIGH);
    return (v >> 3) & 0x0FFF;
  }

  // Touch pressure from the chip itself. Some clone boards don't wire the IRQ pin, so presses
  // are detected from this instead of the interrupt line.
  int pressure() {
    int z1 = xfer(0xB0), z2 = xfer(0xC0);
    return z1 + 4095 - z2;
  }

  // Raw reading (axes as the chip reports them). Returns false when not pressed.
  bool readRaw(uint16_t &rx, uint16_t &ry) {
    if (pressure() < T28_PRESS_MIN) return false;
    uint32_t sx = 0, sy = 0;
    for (int i = 0; i < 4; i++) { sx += xfer(0xD0); sy += xfer(0x90); }
    rx = sx / 4; ry = sy / 4;
    return pressure() >= T28_PRESS_MIN && rx > 100 && ry > 100;
  }

  static int scale(int v, int lo, int hi, int span, bool flip) {
    long p = (long)(v - lo) * span / (hi - lo);
    if (p < 0) p = 0; if (p >= span) p = span - 1;
    return flip ? span - 1 - p : p;
  }

  bool read(uint16_t &x, uint16_t &y) {
    uint16_t rx, ry;
    if (!readRaw(rx, ry)) return false;
    uint16_t ax = swapXY ? ry : rx, ay = swapXY ? rx : ry;
    x = scale(ax, xMin, xMax, w, flipX);
    y = scale(ay, yMin, yMax, h, flipY);
    return true;
  }

  bool pressed() { return pressure() >= T28_PRESS_MIN; }

  // Work out axis order and direction from three taps: top-left, top-right, bottom-left.
  void setFromTaps(uint16_t tlx, uint16_t tly, uint16_t trx, uint16_t try_, uint16_t blx, uint16_t bly) {
    swapXY = abs((int)try_ - (int)tly) > abs((int)trx - (int)tlx);   // raw Y moves along screen X
    uint16_t tlA = swapXY ? tly : tlx, trA = swapXY ? try_ : trx;
    uint16_t tlB = swapXY ? tlx : tly, blB = swapXY ? blx : bly;
    flipX = trA < tlA; flipY = blB < tlB;
    xMin = min(tlA, trA); xMax = max(tlA, trA);
    yMin = min(tlB, blB); yMax = max(tlB, blB);
    // Taps were CAL_INSET px in from the edges, so stretch the range to the full screen.
    int ex = (xMax - xMin) * CAL_INSET / (w - 2 * CAL_INSET), ey = (yMax - yMin) * CAL_INSET / (h - 2 * CAL_INSET);
    xMin = max(0, (int)xMin - ex); xMax = min(4095, (int)xMax + ex);
    yMin = max(0, (int)yMin - ey); yMax = min(4095, (int)yMax + ey);
  }
};
