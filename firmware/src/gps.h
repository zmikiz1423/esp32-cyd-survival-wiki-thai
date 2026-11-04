// Optional GPS (Teyleten ATGM336H or any NMEA module, 9600 baud). Read-only: we only listen.
// Enabled when PIN_GPS_RX >= 0 (see config.h). Tracked in issue #12.
#pragma once
#include <Arduino.h>

struct GpsFix {
  bool     valid;      // a position fix is currently held
  double   lat, lon;   // degrees, south and west negative
  float    altM;       // metres above sea level
  float    speedKmh;
  float    courseDeg;  // direction of travel; not a magnetic compass
  float    hdop;
  float    accuracyM;  // phone-reported accuracy (0 when unknown)
  uint8_t  sats;       // satellites used
  uint32_t fixMs;      // millis() of the last good fix, 0 = never
  uint32_t sentences;  // valid NMEA sentences seen (0 = no GPS module talking)
  bool     phone;      // the position came from the phone browser, not a GPS module
};

void gpsBegin();                     // call once from setup()
void gpsLoop();                      // call from loop(): reads whatever has arrived
bool gpsPresent();                   // compiled in (PIN_GPS_RX >= 0)
GpsFix gpsGet();                     // copy of the latest state
uint32_t gpsFixAgeS();               // seconds since the last fix, 0xFFFFFFFF if none
void gpsSetPhone(double lat, double lon, float accM);   // a phone shared its position (phone map page); used when no module has a fix
