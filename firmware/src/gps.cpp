#include "gps.h"
#include "config.h"

// ---- a position sent by the phone's browser (web.cpp, /api/pos). Used when no GPS module has a fix. ----
#define PHONE_FRESH_MS 45000
static struct { double lat, lon; float acc; uint32_t ms; } phoneFix = {};
static portMUX_TYPE phoneMux = portMUX_INITIALIZER_UNLOCKED;

void gpsSetPhone(double lat, double lon, float accM) {
  if (!isfinite(lat) || !isfinite(lon) || fabs(lat) > 90 || fabs(lon) > 180) return;
  portENTER_CRITICAL(&phoneMux);
  phoneFix.lat = lat; phoneFix.lon = lon; phoneFix.acc = accM;
  phoneFix.ms = millis() | 1;                        // never 0, 0 means "never"
  portEXIT_CRITICAL(&phoneMux);
}

static bool phoneFresh(GpsFix *out) {
  portENTER_CRITICAL(&phoneMux);
  double lat = phoneFix.lat, lon = phoneFix.lon;
  uint32_t ms = phoneFix.ms;
  float acc = phoneFix.acc;
  portEXIT_CRITICAL(&phoneMux);
  if (!ms || millis() - ms > PHONE_FRESH_MS) return false;
  if (out) { out->valid = true; out->lat = lat; out->lon = lon; out->phone = true; out->fixMs = ms; out->accuracyM = acc; out->speedKmh = 0; out->courseDeg = 0; out->hdop = 0; }
  return true;
}

#if PIN_GPS_RX >= 0
static GpsFix fix = {};
static portMUX_TYPE moduleMux = portMUX_INITIALIZER_UNLOCKED;
static GpsFix moduleCopy(){portENTER_CRITICAL(&moduleMux);GpsFix f=fix;portEXIT_CRITICAL(&moduleMux);return f;}
static char line[100];
static uint8_t len = 0;

static int hexv(char c) { return c >= '0' && c <= '9' ? c - '0' : (c | 32) >= 'a' && (c | 32) <= 'f' ? (c | 32) - 'a' + 10 : -1; }

// Splits an NMEA body in place at commas. Returns the number of fields.
static int splitFields(char *s, char **f, int max) {
  int n = 0; f[n++] = s;
  for (; *s && n < max; s++) if (*s == ',') { *s = 0; f[n++] = s + 1; }
  return n;
}

// ddmm.mmmm + hemisphere -> signed degrees
static double toDeg(const char *v, const char *hemi) {
  if (!*v || strlen(hemi)!=1 || !strchr("NSEW",*hemi)) return NAN;
  char *end=nullptr;double raw = strtod(v,&end);
  if(end==v||*end||!isfinite(raw)||raw<0)return NAN;
  int deg = (int)(raw / 100);if(raw-deg*100>=60)return NAN;
  double d = deg + (raw - deg * 100) / 60.0;
  return (*hemi == 'S' || *hemi == 'W') ? -d : d;
}

static void handle(char *s) {                       // s = text between '$' and '*'
  char *f[20]; int n = splitFields(s, f, 20);
  const char *t = f[0];
  if (strlen(t) < 5) return;
  t += 2;                                          // skip talker (GP, GN, GL...)
  GpsFix next=moduleCopy();
  if (!strncmp(t, "GGA", 3) && n >= 10) {
    int q = atoi(f[6]);
    next.sats = atoi(f[7]);
    next.hdop = atof(f[8]);if(!isfinite(next.hdop)||next.hdop<0)next.hdop=0;
    if (q > 0 && *f[2] && *f[4]) {
      double lat=toDeg(f[2],f[3]),lon=toDeg(f[4],f[5]);
      next.valid=isfinite(lat)&&isfinite(lon)&&fabs(lat)<=90&&fabs(lon)<=180&&strchr("NS",*f[3])&&strchr("EW",*f[5]);
      if(next.valid){next.lat=lat;next.lon=lon;next.altM=atof(f[9]);if(!isfinite(next.altM))next.altM=0;next.fixMs=millis();}
    } else next.valid = false;
  } else if (!strncmp(t, "RMC", 3) && n >= 9) {
    if (*f[2] == 'A' && *f[3] && *f[5]) {
      double lat = toDeg(f[3], f[4]), lon = toDeg(f[5], f[6]);
      next.valid=isfinite(lat)&&isfinite(lon)&&fabs(lat)<=90&&fabs(lon)<=180&&strchr("NS",*f[4])&&strchr("EW",*f[6]);
      if (next.valid) {
        next.lat = lat; next.lon = lon; next.valid = true; next.fixMs = millis();
        next.speedKmh = atof(f[7]) * 1.852f;
        next.courseDeg = atof(f[8]);
        if(!isfinite(next.speedKmh)||next.speedKmh<0)next.speedKmh=0;
        if(!isfinite(next.courseDeg)||next.courseDeg<0)next.courseDeg=0;
        else next.courseDeg=fmod(next.courseDeg,360.0f);
      }
    } else next.valid = false;
  }
  portENTER_CRITICAL(&moduleMux);fix=next;portEXIT_CRITICAL(&moduleMux);
}

static void endOfLine() {
  line[len] = 0; len = 0;
  if (line[0] != '$') return;
  char *star = strchr(line, '*');
  if (!star || !star[1] || !star[2]) return;
  uint8_t sum = 0; for (char *p = line + 1; p < star; p++) sum ^= (uint8_t)*p;
  int hi = hexv(star[1]), lo = hexv(star[2]);
  if (hi < 0 || lo < 0 || sum != (uint8_t)(hi * 16 + lo)) return;   // bad checksum: ignore
  *star = 0; portENTER_CRITICAL(&moduleMux);fix.sentences++;portEXIT_CRITICAL(&moduleMux);
  handle(line + 1);
}

void gpsBegin() {
  // TX pin is a spare pin that stays unconnected: -1 would fall back to GPIO 17, which is the blue LED on the 2.8".
  Serial2.setRxBufferSize(2048); // buffer NMEA while an SD tile is decoded
  Serial2.begin(9600, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
}

void gpsLoop() {
  while (Serial2.available()) {
    char c = Serial2.read();
    if (c == '\n' || c == '\r') { if (len) endOfLine(); }
    else if (len < sizeof(line) - 1) line[len++] = c;
    else len = 0;                                   // overlong line: drop it
  }
  portENTER_CRITICAL(&moduleMux);
  if (fix.valid && millis() - fix.fixMs > 10000) fix.valid = false;
  portEXIT_CRITICAL(&moduleMux);   // stale
}

bool gpsPresent() { return true; }
GpsFix gpsGet() {
  GpsFix f = moduleCopy();
  if (f.valid) return f;                              // the module has a fix: it wins
  GpsFix p = f;
  if (phoneFresh(&p)) return p;                       // otherwise use the phone's position
  return f;
}
uint32_t gpsFixAgeS() {
  GpsFix g = gpsGet();
  return g.fixMs ? (millis() - g.fixMs) / 1000 : 0xFFFFFFFF;
}
#else
void gpsBegin() {}
void gpsLoop() {}
bool gpsPresent() { return phoneFresh(nullptr); }
GpsFix gpsGet() { GpsFix g{}; phoneFresh(&g); g.sentences = g.valid ? 1 : 0; return g; }
uint32_t gpsFixAgeS() {
  GpsFix g = gpsGet();
  return g.fixMs ? (millis() - g.fixMs) / 1000 : 0xFFFFFFFF;
}
#endif
