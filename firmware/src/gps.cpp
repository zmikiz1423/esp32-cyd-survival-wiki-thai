#include "gps.h"
#include "config.h"
#include <esp_timer.h>
#include <float.h>

static constexpr uint32_t PHONE_FRESH_MS = 45000;
static constexpr uint32_t FIX_FRESH_MS = 10000;
static constexpr uint32_t MOTION_FRESH_MS = 15000;

// Keep 0 for “never” without rounding an even timestamp into the future.
static uint32_t stampMs(uint32_t now) { return now ? now : UINT32_MAX; }

static struct { double lat, lon; float acc; uint32_t ms; } phoneFix = {};
static portMUX_TYPE phoneMux = portMUX_INITIALIZER_UNLOCKED;

void gpsSetPhone(double lat, double lon, float accM) {
  if (!isfinite(lat) || !isfinite(lon) || fabs(lat) > 90 || fabs(lon) > 180) return;
  portENTER_CRITICAL(&phoneMux);
  phoneFix.lat = lat;
  phoneFix.lon = lon;
  phoneFix.acc = isfinite(accM) && accM >= 0 ? accM : 0;
  phoneFix.ms = stampMs(millis());
  portEXIT_CRITICAL(&phoneMux);
}

static bool phoneFresh(GpsFix *out) {
  portENTER_CRITICAL(&phoneMux);
  const uint32_t now = millis();
  if (phoneFix.ms && now - phoneFix.ms > PHONE_FRESH_MS) phoneFix.ms = 0;
  const auto sample = phoneFix;
  portEXIT_CRITICAL(&phoneMux);
  if (!sample.ms) return false;
  if (out) {
    const uint32_t sentences = out->sentences;
    *out = {};
    out->sentences = sentences;
    out->valid = true;
    out->lat = sample.lat;
    out->lon = sample.lon;
    out->phone = true;
    out->fixMs = sample.ms;
    out->accuracyM = sample.acc;
  }
  return true;
}

// A GPS sync continues to supply time after the position fix is lost. ESP32's
// 64-bit monotonic timer keeps holdover correct across the millis() wrap.
struct GpsClock { uint32_t epochS; int64_t syncUs; };
static GpsClock gpsClock = {};
static portMUX_TYPE clockMux = portMUX_INITIALIZER_UNLOCKED;

static GpsClock clockCopy() {
  portENTER_CRITICAL(&clockMux);
  GpsClock clock = gpsClock;
  portEXIT_CRITICAL(&clockMux);
  return clock;
}

bool gpsUtcNow(uint32_t *epochS) {
  GpsClock clock = clockCopy();
  if (!clock.epochS) return false;
  const int64_t epoch = clock.epochS + (esp_timer_get_time() - clock.syncUs) / 1000000;
  if (epoch > UINT32_MAX) return false;
  if (epochS) *epochS = (uint32_t)epoch;
  return true;
}

uint32_t gpsTimeSyncAgeS() {
  GpsClock clock = clockCopy();
  if (!clock.epochS) return UINT32_MAX;
  const int64_t elapsed = (esp_timer_get_time() - clock.syncUs) / 1000000;
  return elapsed < UINT32_MAX ? (uint32_t)elapsed : UINT32_MAX;
}

#if PIN_GPS_RX >= 0
static GpsFix fix = {};
static uint32_t speedMs = 0;
static portMUX_TYPE moduleMux = portMUX_INITIALIZER_UNLOCKED;
static char line[100];
static uint8_t len = 0;

static GpsFix moduleCopy(uint32_t &speedStamp) {
  portENTER_CRITICAL(&moduleMux);
  const GpsFix result = fix;
  speedStamp = speedMs;
  portEXIT_CRITICAL(&moduleMux);
  return result;
}

static bool decimalDigit(char c) { return c >= '0' && c <= '9'; }
static int digitPair(const char *s) { return (s[0] - '0') * 10 + s[1] - '0'; }

// Days since 1970-01-01 for a validated civil date (Howard Hinnant's algorithm).
static int32_t daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  int era = y / 400;
  unsigned yoe = (unsigned)(y - era * 400);
  unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int)doe - 719468;
}

static void syncUtc(const char *time, const char *date) {
  if (strlen(time) < 6 || strlen(date) != 6) return;
  for (int i = 0; i < 6; i++) {
    if (!decimalDigit(time[i]) || !decimalDigit(date[i])) return;
  }
  if (time[6]) {
    if (time[6] != '.' || !decimalDigit(time[7])) return;
    for (const char *digit = time + 7; *digit; digit++) {
      if (!decimalDigit(*digit)) return;
    }
  }
  const int hh = digitPair(time), mm = digitPair(time + 2), ss = digitPair(time + 4);
  const int day = digitPair(date), month = digitPair(date + 2);
  const int year = 2000 + digitPair(date + 4);  // RMC carries a two-digit year.
  if (hh > 23 || mm > 59 || ss > 59 || month < 1 || month > 12 || day < 1) return;
  static constexpr uint8_t monthDays[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
  if (day > monthDays[month - 1] + (month == 2 && leap)) return;
  const uint32_t epoch = (uint32_t)daysFromCivil(year, month, day) * 86400UL
                       + hh * 3600UL + mm * 60UL + ss;
  portENTER_CRITICAL(&clockMux);
  gpsClock = {epoch, esp_timer_get_time()};
  portEXIT_CRITICAL(&clockMux);
}

// Parse a complete, finite decimal field. Some optional NMEA fields are empty.
static double number(const char *value) {
  const char *p = value;
  if (*p == '-' || *p == '+') p++;
  if (!decimalDigit(*p)) return NAN;
  while (decimalDigit(*p)) p++;
  if (*p == '.') {
    p++;
    if (!decimalDigit(*p)) return NAN;
    while (decimalDigit(*p)) p++;
  }
  if (*p) return NAN;
  const double result = strtod(value, nullptr);
  return isfinite(result) ? result : NAN;
}

static float floatOrZero(double value) {
  return isfinite(value) && fabs(value) <= FLT_MAX ? (float)value : 0;
}

// ddmm.mmmm + hemisphere -> signed degrees, with hemisphere checked by axis.
static double toDeg(const char *value, const char *hemi, bool latitude) {
  if (strlen(hemi) != 1 || !strchr(latitude ? "NS" : "EW", *hemi)) return NAN;
  const int limit = latitude ? 90 : 180;
  const double raw = number(value);
  if (!isfinite(raw) || raw < 0 || raw > limit * 100) return NAN;
  const int degrees = (int)(raw / 100);
  const double minutes = raw - degrees * 100;
  if (minutes >= 60) return NAN;
  const double result = degrees + minutes / 60.0;
  return (*hemi == 'S' || *hemi == 'W') ? -result : result;
}

static int splitFields(char *s, char **f, int max) {
  int n = 0;
  f[n++] = s;
  for (; *s && n < max; s++) {
    if (*s == ',') { *s = 0; f[n++] = s + 1; }
  }
  return n;
}

static void expireMotion(GpsFix &sample, uint32_t &speedStamp, uint32_t now) {
  if (!sample.valid || !sample.courseMs || now - sample.courseMs >= MOTION_FRESH_MS) {
    sample.courseMs = 0;
    sample.courseDeg = 0;
  }
  if (!sample.valid || !speedStamp || now - speedStamp >= MOTION_FRESH_MS) {
    speedStamp = 0;
    sample.speedKmh = 0;
  }
}

static void handle(char *s) {
  char *f[20];
  const int n = splitFields(s, f, 20);
  if (strlen(f[0]) != 5) return;
  const char *kind = f[0] + 2;  // Skip the talker ID (GP, GN, GL...).
  uint32_t speedStamp;
  GpsFix next = moduleCopy(speedStamp);
  const uint32_t now = millis();
  if (!strcmp(kind, "GGA") && n >= 10) {
    const double quality = number(f[6]);
    const double satellites = number(f[7]);
    next.sats = satellites >= 0 && satellites <= UINT8_MAX ? (uint8_t)satellites : 0;
    next.hdop = floatOrZero(number(f[8]));
    if (next.hdop < 0) next.hdop = 0;
    const double lat = toDeg(f[2], f[3], true), lon = toDeg(f[4], f[5], false);
    next.valid = quality > 0 && isfinite(lat) && isfinite(lon);
    if (next.valid) {
      next.lat = lat;
      next.lon = lon;
      next.altM = floatOrZero(number(f[9]));
      next.fixMs = stampMs(now);
    }
  } else if (!strcmp(kind, "RMC") && n >= 9) {
    const double lat = toDeg(f[3], f[4], true), lon = toDeg(f[5], f[6], false);
    next.valid = !strcmp(f[2], "A") && isfinite(lat) && isfinite(lon);
    next.courseMs = 0;
    next.courseDeg = 0;
    if (next.valid) {
      next.lat = lat;
      next.lon = lon;
      next.fixMs = stampMs(now);
      const double knots = number(f[7]);
      next.speedKmh = knots >= 0 ? floatOrZero(knots * 1.852) : 0;
      speedStamp = stampMs(now);
      const double course = number(f[8]);
      if (next.speedKmh >= 2 && course >= 0 && course <= 360) {
        next.courseDeg = course == 360 ? 0 : (float)course;
        next.courseMs = stampMs(now);
      }
      if (n >= 10) syncUtc(f[1], f[9]);
    }
  } else return;
  expireMotion(next, speedStamp, now);
  portENTER_CRITICAL(&moduleMux);
  fix = next;
  speedMs = speedStamp;
  portEXIT_CRITICAL(&moduleMux);
}

static int hexv(char c) {
  if (decimalDigit(c)) return c - '0';
  c |= 32;
  return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

static void endOfLine() {
  line[len] = 0;
  len = 0;
  char *star = strchr(line, '*');
  if (!star || !star[1] || !star[2] || star[3]) return;
  uint8_t sum = 0;
  for (char *p = line + 1; p < star; p++) sum ^= (uint8_t)*p;
  const int hi = hexv(star[1]), lo = hexv(star[2]);
  if (hi < 0 || lo < 0 || sum != (uint8_t)(hi * 16 + lo)) return;
  *star = 0;
  portENTER_CRITICAL(&moduleMux);
  fix.sentences++;
  portEXIT_CRITICAL(&moduleMux);
  handle(line + 1);
}

void gpsBegin() {
  // RX is explicit, so Arduino 2.0.17 leaves TX unassigned when passed -1.
  // This frees IO22 for the optional E220 UART without driving its RX line.
  Serial2.setRxBufferSize(2048);
  Serial2.begin(9600, SERIAL_8N1, PIN_GPS_RX, -1);
}

void gpsLoop() {
  while (Serial2.available()) {
    const int c = Serial2.read();
    if (c == '$') { line[0] = '$'; len = 1; }
    else if (c == '\n' || c == '\r') { if (len) endOfLine(); }
    else if (len && len < sizeof(line) - 1 && c >= 32 && c <= 126) line[len++] = (char)c;
    else len = 0;
  }
  portENTER_CRITICAL(&moduleMux);
  const uint32_t now = millis();
  if (fix.valid && now - fix.fixMs > FIX_FRESH_MS) fix.valid = false;
  expireMotion(fix, speedMs, now);
  portEXIT_CRITICAL(&moduleMux);
  phoneFresh(nullptr);  // Retire expired samples even if the module has a fix.
}

bool gpsPresent() { return true; }
GpsFix gpsGet() {
  uint32_t speedStamp;
  GpsFix result = moduleCopy(speedStamp);
  const uint32_t now = millis();
  if (result.valid && now - result.fixMs > FIX_FRESH_MS) result.valid = false;
  expireMotion(result, speedStamp, now);
  if (!result.valid) phoneFresh(&result);
  return result;
}
#else
void gpsBegin() {}
void gpsLoop() { phoneFresh(nullptr); }
bool gpsPresent() { return phoneFresh(nullptr); }
GpsFix gpsGet() { GpsFix g{}; phoneFresh(&g); g.sentences = g.valid ? 1 : 0; return g; }
#endif

uint32_t gpsFixAgeS() {
  GpsFix g = gpsGet();
  return g.fixMs ? (millis() - g.fixMs) / 1000 : UINT32_MAX;
}
