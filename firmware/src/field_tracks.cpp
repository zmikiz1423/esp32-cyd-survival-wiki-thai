#include "field_tracks.h"
#include "gps.h"
#include "map_storage.h"
#include <SD.h>
#include <math.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace {
constexpr uint32_t MAX_TRACK_BYTES = 4UL * 1024 * 1024;
constexpr int MAX_BACK_POINTS = 600;
SemaphoreHandle_t trackMutex = nullptr;
uint32_t activeRevision = 0;
String activeRoot;
bool recording = false;
bool pendingSegment = true;
bool havePrevious = false;
uint32_t previousFixMs = 0;
uint32_t lastAttemptMs = 0;
double previousLat = 0, previousLon = 0;
uint32_t writtenPoints = 0;
unsigned exportReaders = 0;
String error;
OfflineRoutePoint reversePoints[MAX_BACK_POINTS];
int reverseCount = 0;

struct TrackPoint {
  double lat, lon, altitude;
  uint32_t epoch;
  bool start;
};

bool lock(TickType_t ticks = 0) {
  return trackMutex && xSemaphoreTake(trackMutex, ticks) == pdTRUE;
}
void unlock() { xSemaphoreGive(trackMutex); }

void updateSelection() {
  MapStorageSnapshot selected = mapStorageSnapshot();
  if (selected.revision == activeRevision) return;
  recording = false;
  reverseCount = 0;
  havePrevious = false;
  pendingSegment = true;
  previousFixMs = 0;
  writtenPoints = 0;
  error = "";
  activeRoot = selected.root;
  activeRevision = selected.revision;
}

String boundedLine(File &file);
bool parsePoint(const String &line, TrackPoint &point);

bool validSource(const String &path) {
  File file = SD.open(path, FILE_READ);
  bool valid = file && !file.isDirectory() && file.size() > 0 && file.size() <= MAX_TRACK_BYTES;
  TrackPoint point;
  bool found = false;
  for (int i = 0; valid && i < 8 && file.available(); i++) {
    if (parsePoint(boundedLine(file), point)) { found = true; break; }
  }
  if (file) file.close();
  return valid && found;
}

String sourcePath() {
  for (const char *name : {"/recorded-track.csv", "/last-track.csv", "/track.csv"}) {
    String path = activeRoot + name;
    if (validSource(path)) return path;
  }
  return String();
}

String boundedLine(File &file) {
  String line;
  bool tooLong = false;
  while (file.available()) {
    char c = file.read();
    if (c == '\n') break;
    if (c == '\r') continue;
    if (line.length() < 160) line += c;
    else tooLong = true;
  }
  return tooLong ? String() : line;
}

bool parsePoint(const String &line, TrackPoint &point) {
  if (!line.length() || line[0] == '#') return false;
  unsigned long epoch = 0;
  unsigned segment = 0;
  char trailing;
  int fields = sscanf(line.c_str(), "%lf,%lf,%lf,%lu,%u%c", &point.lat, &point.lon,
                      &point.altitude, &epoch, &segment, &trailing);
  if (fields != 5) {
    // Legacy th4 breadcrumbs contain latitude and longitude only.
    fields = sscanf(line.c_str(), "%lf,%lf%c", &point.lat, &point.lon, &trailing);
    if (fields != 2) return false;
    point.altitude = NAN;
    epoch = 0;
    segment = 0;
  }
  if (!isfinite(point.lat) || !isfinite(point.lon) || fabs(point.lat) > 85.05112878 || fabs(point.lon) > 180) return false;
  point.epoch = (uint32_t)epoch;
  point.start = segment != 0;
  return true;
}

bool nextPoint(File &file, TrackPoint &point, bool &gap) {
  while (file.available()) {
    String line = boundedLine(file);
    if (parsePoint(line, point)) {
      point.start = point.start || gap;
      gap = false;
      return true;
    }
    if (line.length() && line[0] != '#') gap = true;
    yield();
  }
  return false;
}

double distanceMeters(double latA, double lonA, double latB, double lonB) {
  constexpr double RAD = 3.14159265358979323846 / 180;
  double lat = (latB - latA) * RAD;
  double lon = (lonB - lonA) * RAD;
  double a = sin(lat / 2) * sin(lat / 2) + cos(latA * RAD) * cos(latB * RAD) * sin(lon / 2) * sin(lon / 2);
  return 6371000 * 2 * atan2(sqrt(a), sqrt(max(0.0, 1 - a)));
}
}  // namespace

void fieldTrackBegin() {
  if (!trackMutex) trackMutex = xSemaphoreCreateMutex();
  if (!lock(pdMS_TO_TICKS(50))) return;
  recording = false;
  reverseCount = 0;
  activeRevision = 0;
  updateSelection();
  unlock();
}

bool fieldTrackRecording() {
  if (!lock(pdMS_TO_TICKS(20))) return false;
  updateSelection();
  bool result = recording;
  unlock();
  return result;
}

bool fieldTrackSetRecording(bool enabled) {
  if (!lock(pdMS_TO_TICKS(50))) return false;
  updateSelection();
  if (!enabled) {
    recording = false;
    unlock();
    return true;
  }
  if (recording) { unlock(); return true; }
  if (exportReaders) { error = "กำลังส่งออก GPX กรุณารอ"; unlock(); return false; }
  File center = SD.open(activeRoot + "/center.txt", FILE_READ);
  bool ready = center && !center.isDirectory();
  if (center) center.close();
  if (!ready) { error = "ไม่พบชุดแผนที่ในการ์ด SD"; unlock(); return false; }
  String current = activeRoot + "/recorded-track.csv";
  String previous = activeRoot + "/last-track.csv";
  String temporary = activeRoot + "/recorded-track.tmp";
  File file = SD.open(temporary, FILE_WRITE);
  const char *header = "# CYD track v1: lat,lon,altitude,utc,start\n";
  bool written = file && file.print(header) == strlen(header);
  if (file) file.close();
  if (!written) { SD.remove(temporary); error = "เริ่มบันทึกไม่ได้ ตรวจพื้นที่ SD"; unlock(); return false; }
  bool hadCurrent = SD.exists(current);
  if (hadCurrent) {
    if (SD.exists(previous) && !SD.remove(previous)) { SD.remove(temporary); error = "เก็บเส้นทางเดิมไม่ได้"; unlock(); return false; }
    if (!SD.rename(current, previous)) { SD.remove(temporary); error = "เก็บเส้นทางเดิมไม่ได้"; unlock(); return false; }
  }
  if (!SD.rename(temporary, current)) {
    if (hadCurrent) SD.rename(previous, current);
    error = "เปิดไฟล์เส้นทางใหม่ไม่ได้";
    unlock();
    return false;
  }
  recording = true;
  pendingSegment = true;
  havePrevious = false;
  previousFixMs = 0;
  lastAttemptMs = 0;
  writtenPoints = 0;
  error = "";
  unlock();
  return true;
}

void fieldTrackLoop() {
  if (!lock()) return;
  updateSelection();
  if (!recording) { unlock(); return; }
  uint32_t now = millis();
  GpsFix fix = gpsGet();
  if (!fix.valid || !fix.fixMs || now - fix.fixMs > 10000 || !isfinite(fix.lat) || !isfinite(fix.lon) ||
      fabs(fix.lat) > 85.05112878 || fabs(fix.lon) > 180 || (fix.phone && fix.accuracyM > 100) || (!fix.phone && fix.hdop > 10)) {
    pendingSegment = true;
    unlock();
    return;
  }
  if (fix.fixMs == previousFixMs || (lastAttemptMs && now - lastAttemptMs < 2000)) { unlock(); return; }
  lastAttemptMs = now;
  previousFixMs = fix.fixMs;
  if (havePrevious && !pendingSegment && distanceMeters(previousLat, previousLon, fix.lat, fix.lon) < max(5.0, (double)fix.accuracyM)) {
    unlock();
    return;
  }
  uint32_t epoch = 0;
  gpsUtcNow(&epoch);
  char row[144];
  int length = snprintf(row, sizeof(row), "%.7f,%.7f,%.2f,%lu,%u\n", fix.lat, fix.lon,
                        fix.phone ? NAN : (double)fix.altM, (unsigned long)epoch, pendingSegment ? 1U : 0U);
  File file = SD.open(activeRoot + "/recorded-track.csv", FILE_APPEND);
  bool full = file && file.size() + length > MAX_TRACK_BYTES;
  bool saved = length > 0 && length < (int)sizeof(row) && file && !full &&
               file.write((const uint8_t *)row, length) == (size_t)length;
  if (file) file.close();
  if (!saved) {
    recording = false;
    error = full ? "ไฟล์เส้นทางเต็ม 4 MB หยุดบันทึกแล้ว" : "บันทึก SD ไม่สำเร็จ หยุดแล้ว";
  } else {
    previousLat = fix.lat;
    previousLon = fix.lon;
    havePrevious = true;
    pendingSegment = false;
    writtenPoints++;
  }
  unlock();
}

FieldTrackStatus fieldTrackStatus() {
  FieldTrackStatus result = {};
  if (!lock(pdMS_TO_TICKS(30))) { result.error = "กำลังอ่านข้อมูลเส้นทาง"; return result; }
  updateSelection();
  result.recording = recording;
  result.backtracking = reverseCount != 0;
  result.points = writtenPoints;
  result.error = error;
  String path = sourcePath();
  result.available = path.length();
  File file = path.length() ? SD.open(path, FILE_READ) : File();
  if (file) { result.bytes = file.size(); file.close(); }
  unlock();
  return result;
}

String fieldTrackStatusJson() {
  FieldTrackStatus status = fieldTrackStatus();
  return String("{\"recording\":") + (status.recording ? "true" : "false") +
    ",\"available\":" + (status.available ? "true" : "false") +
    ",\"backtracking\":" + (status.backtracking ? "true" : "false") +
    ",\"points\":" + String(status.points) + ",\"bytes\":" + String(status.bytes) +
    ",\"error\":\"" + status.error + "\"}";
}

bool fieldTrackStartBacktrack() {
  if (!lock(pdMS_TO_TICKS(50))) return false;
  updateSelection();
  reverseCount = 0;
  String path = sourcePath();
  File file = path.length() ? SD.open(path, FILE_READ) : File();
  if (!file) { error = "ไม่มีเส้นทางให้ย้อนรอย"; unlock(); return false; }
  TrackPoint point;
  bool gap = true;
  uint32_t total = 0;
  while (nextPoint(file, point, gap)) { total++; if ((total & 255) == 0) { gpsLoop(); yield(); } }
  if (!total) { file.close(); error = "เส้นทางยังไม่มีพิกัด"; unlock(); return false; }
  file.seek(0);
  gap = true;
  uint32_t stride = total <= MAX_BACK_POINTS ? 1 : (total - 2 + MAX_BACK_POINTS - 3) / (MAX_BACK_POINTS - 2);
  uint32_t index = 0;
  bool segment = true;
  while (nextPoint(file, point, gap)) {
    segment = segment || point.start;
    if ((index % stride == 0 && reverseCount < MAX_BACK_POINTS - 1) || index + 1 == total) {
      reversePoints[reverseCount++] = {(float)point.lat, (float)point.lon, segment};
      segment = false;
    }
    index++;
    if ((index & 255) == 0) { gpsLoop(); yield(); }
  }
  file.close();
  // A segment break belongs before the following point in each direction.
  for (int i = 0; i + 1 < reverseCount; i++) reversePoints[i].start = reversePoints[i + 1].start;
  for (int i = 0; i < reverseCount / 2; i++) {
    OfflineRoutePoint temp = reversePoints[i];
    reversePoints[i] = reversePoints[reverseCount - 1 - i];
    reversePoints[reverseCount - 1 - i] = temp;
  }
  reversePoints[0].start = true;
  error = "";
  unlock();
  return true;
}

void fieldTrackClearBacktrack() {
  if (!lock(pdMS_TO_TICKS(30))) return;
  reverseCount = 0;
  unlock();
}

const OfflineRoutePoint *fieldTrackBacktrack(int &count) {
  count = mapStorageRevision() == activeRevision ? reverseCount : 0;
  return reversePoints;
}

bool fieldTrackExport(FieldTrackSink sink, void *context, uint32_t expectedRevision) {
  if (!sink || !lock(pdMS_TO_TICKS(50))) return false;
  updateSelection();
  if (expectedRevision != UINT32_MAX && expectedRevision != activeRevision) { unlock(); return false; }
  String path = sourcePath();
  File file = path.length() ? SD.open(path, FILE_READ) : File();
  if (!file) { unlock(); return false; }
  uint32_t limit = file.size();
  exportReaders++;
  unlock();
  auto emit = [&](const char *text) { return sink(text, strlen(text), context); };
  bool ok = emit("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<gpx version=\"1.1\" creator=\"Survival Wiki Thai\" xmlns=\"http://www.topografix.com/GPX/1/1\"><trk><name>CYD recorded track</name>");
  bool gap = true, segmentOpen = false;
  TrackPoint point;
  uint32_t count = 0;
  while (ok && file.position() < limit && nextPoint(file, point, gap)) {
    if (point.start || !segmentOpen) {
      if (segmentOpen) ok = emit("</trkseg>");
      if (ok) ok = emit("<trkseg>");
      segmentOpen = true;
    }
    char row[192];
    int length = snprintf(row, sizeof(row), "<trkpt lat=\"%.7f\" lon=\"%.7f\">", point.lat, point.lon);
    if (ok) ok = sink(row, length, context);
    if (ok && isfinite(point.altitude)) {
      length = snprintf(row, sizeof(row), "<ele>%.2f</ele>", point.altitude);
      ok = sink(row, length, context);
    }
    if (ok && point.epoch) {
      time_t epoch = point.epoch;
      struct tm utc;
      if (gmtime_r(&epoch, &utc)) {
        length = snprintf(row, sizeof(row), "<time>%04d-%02d-%02dT%02d:%02d:%02dZ</time>", utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday, utc.tm_hour, utc.tm_min, utc.tm_sec);
        ok = sink(row, length, context);
      }
    }
    if (ok) ok = emit("</trkpt>\n");
    if ((++count & 63) == 0) yield();
  }
  if (ok && !segmentOpen) ok = emit("<trkseg>");
  if (ok) ok = emit("</trkseg></trk></gpx>\n");
  file.close();
  if (lock(portMAX_DELAY)) { exportReaders--; unlock(); }
  return ok;
}
