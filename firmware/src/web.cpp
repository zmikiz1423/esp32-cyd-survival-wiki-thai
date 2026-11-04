#include "web.h"
#include "config.h"
#include "mesh.h"
#include "meshpage.h"
#include "mappage.h"
#include "httpscert.h"
#include "gps.h"
#include "offline_maps.h"
#include "toolspage.h"
#include <esp_https_server.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <SD.h>

static WebServer server(80);
static DNSServer dns;
static TaskHandle_t webTaskHandle = nullptr;
static volatile bool running = false;
static volatile bool stopRequested = false;
static bool routesAdded = false;
static const IPAddress apIP(192, 168, 4, 1);

// Send the visitor to the wiki home page. Phones open this automatically
// when their connectivity check gets redirected (the "captive portal" pop-up).
static void redirectHome() {
  server.sendHeader("Location", String("http://") + apIP.toString() + "/", true);
  server.sendHeader("Cache-Control", "no-cache");
  server.send(302, "text/plain", "");
}

static bool streamPath(const char *path, const char *type, const char *cache) {
  String gz = String(path) + ".gz";
  File f;
  bool zipped = false;
  if (SD.exists(gz)) { f = SD.open(gz, FILE_READ); zipped = true; }
  else if (SD.exists(path)) { f = SD.open(path, FILE_READ); }
  if (!f) return false;
  if (cache) server.sendHeader("Cache-Control", cache);
  if (zipped) server.sendHeader("Content-Encoding", "gzip");
  server.setContentLength(f.size());
  server.send(200, type, "");
  uint8_t buf[2048];
  while (f.available()) {
    size_t n = f.read(buf, sizeof(buf));
    if (!n) break;
    if (server.client().write(buf, n) != n) break;
  }
  f.close();
  return true;
}

static void handleIndex() {
  if (!streamPath(F_WEB_INDEX, "text/html", "no-cache")) {
    server.send(500, "text/html; charset=utf-8",
                "<html lang='th'><meta charset='utf-8'><meta name='viewport' content='width=device-width'>"
                "<h1>คู่มือเอาตัวรอด</h1><p>ไม่พบการ์ด SD หรือไฟล์ /web/index.html</p></html>");
  }
}

static void handleStatus() {
  String j = "{\"version\":\"" FW_VERSION "\",\"clients\":";
  j += WiFi.softAPgetStationNum();
  j += ",\"uptime\":";
  j += millis() / 1000;
  j += ",\"sdTotalMB\":";
  j += (uint32_t)(SD.totalBytes() / (1024 * 1024));
  j += ",\"sdUsedMB\":";
  j += (uint32_t)(SD.usedBytes() / (1024 * 1024));
  j += "}";
  server.sendHeader("Cache-Control", "no-cache");
  server.send(200, "application/json", j);
}

// ---- mesh chat for phones ----
static String jsonEsc(const char *s) {
  String o;
  for (; *s; s++) {
    if (*s == '"' || *s == '\\') { o += '\\'; o += *s; }
    else if ((uint8_t)*s >= 32) o += *s;
  }
  return o;
}

static void handleMapPage() {
  server.sendHeader("Cache-Control", "no-cache");
  server.send_P(200, "text/html", MAP_PAGE);
}

static void handleToolsPage() {
  server.sendHeader("Cache-Control", "no-cache");
  server.send_P(200, "text/html", TOOLS_PAGE);
}

static void handleMeshPage() {
  server.sendHeader("Cache-Control", "no-cache");
  server.send_P(200, "text/html", MESH_PAGE);
}

// Messages after `since`, plus who is nearby and who has "heard" our messages. Every list is a snapshot,
// so this is safe to call from the web server task and from the HTTPS server task.
static String buildMeshJson(uint32_t since) {
  MeshMsg msgs[MESH_LOG];
  uint32_t latest = 0;
  int n = meshSnapshot(msgs, MESH_LOG, &latest);
  if (since > latest) since = 0;                       // the board restarted: send everything again
  uint32_t now = millis();
  String j = "{\"name\":\"" + jsonEsc(meshName()) + "\",\"on\":" + (meshActive() ? "1" : "0") +
             ",\"seq\":" + String(latest) + ",\"msgs\":[";
  bool first = true;
  for (int i = 0; i < n; i++) {
    if (msgs[i].seq <= since) continue;
    if (!first) j += ',';
    first = false;
    j += "{\"n\":" + String(msgs[i].seq) + ",\"f\":\"" + jsonEsc(msgs[i].from) + "\",\"t\":\"" +
         jsonEsc(msgs[i].text) + "\",\"a\":" + String((now - msgs[i].ms) / 1000) + "}";
  }
  // "Heard by" for every message this board sent: c = how many boards said they got it, k = the first few names
  j += "],\"acks\":[";
  first = true;
  for (int i = 0; i < n; i++) {
    if (!msgs[i].mine) continue;
    if (!first) j += ',';
    first = false;
    j += "{\"n\":" + String(msgs[i].seq) + ",\"c\":" + String(msgs[i].ackN) + ",\"k\":[";
    for (int k = 0; k < 6 && k < msgs[i].ackN; k++) {
      if (k) j += ',';
      j += "\"" + jsonEsc(msgs[i].ack[k]) + "\"";
    }
    j += "]}";
  }
  j += "],\"peers\":[";
  MeshPeer pl[8];
  int pn = meshPeerList(pl, 8);
  for (int i = 0; i < pn; i++) {
    if (i) j += ',';
    float d = meshDistanceM(pl[i].rssi);
    j += "{\"n\":\"" + jsonEsc(pl[i].name) + "\",\"r\":" + String((int)pl[i].rssi) + ",\"d\":" +
         String(d < 0 ? -1 : (int)(d + 0.5f)) + "}";
  }
  j += "]}";
  return j;
}

// Validate complete UTF-8 before queueing. Limits remain bytes to retain compatibility
// with existing ESP-NOW packets; never cut a Thai character or its marks in half.
static bool validUtf8(const String &s) {
  for (size_t i = 0; i < s.length();) {
    uint8_t c = (uint8_t)s[i];
    if (c < 0x80) { if (c < 32 || c == 127) return false; ++i; continue; }
    int n = c >= 0xC2 && c <= 0xDF ? 2 : c >= 0xE0 && c <= 0xEF ? 3 : c >= 0xF0 && c <= 0xF4 ? 4 : 0;
    if (!n || i + n > s.length()) return false;
    for (int k = 1; k < n; ++k) if (((uint8_t)s[i + k] & 0xC0) != 0x80) return false;
    uint8_t d = (uint8_t)s[i + 1];
    if ((c == 0xE0 && d < 0xA0) || (c == 0xED && d >= 0xA0) ||
        (c == 0xF0 && d < 0x90) || (c == 0xF4 && d >= 0x90)) return false;
    i += n;
  }
  return true;
}

static bool phoneSend(String from, String text) {
  from.trim();
  text.trim();
  if (from.length() >= MESH_NAME_LEN || text.length() >= MESH_TEXT_LEN ||
      !validUtf8(from) || !validUtf8(text)) return false;
  return text.length() && meshQueueSend(from.length() ? from.c_str() : "Phone", text.c_str());
}

static void handleMeshApi() {
  server.sendHeader("Cache-Control", "no-cache");
  server.send(200, "application/json", buildMeshJson(server.arg("since").toInt()));
}

static void handleMeshSend() {
  bool ok = phoneSend(server.arg("from"), server.arg("text"));
  server.sendHeader("Cache-Control", "no-cache");
  server.send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

// ---- https (so the phone's browser will share its location) ----
// Browsers only give a page the phone's GPS over https. This second server serves the map and chat pages,
// the same JSON and the map tiles on port 443 with a self-signed certificate.
static httpd_handle_t sslServer = nullptr;

static esp_err_t sslText(httpd_req_t *req, const char *type, const char *body) {
  httpd_resp_set_type(req, type);
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}
static esp_err_t sslMapPage(httpd_req_t *req) {
  Serial.printf("[https] map page (heap %u, lowest %u)\n", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap());
  return sslText(req, "text/html", MAP_PAGE);
}
static esp_err_t sslMeshPage(httpd_req_t *req) { return sslText(req, "text/html", MESH_PAGE); }
static esp_err_t sslToolsPage(httpd_req_t *req) { return sslText(req, "text/html", TOOLS_PAGE); }

static esp_err_t sslRoot(httpd_req_t *req) {           // the wiki itself is on plain http
  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
  return httpd_resp_send(req, "", 0);
}

static esp_err_t sslMeshApi(httpd_req_t *req) {
  uint32_t since = 0;
  char q[48], v[16];
  if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK && httpd_query_key_value(q, "since", v, sizeof(v)) == ESP_OK)
    since = strtoul(v, nullptr, 10);
  return sslText(req, "application/json", buildMeshJson(since).c_str());
}

static String urlDecode(const char *s) {
  String o;
  for (; *s; s++) {
    if (*s == '+') o += ' ';
    else if (*s == '%' && isxdigit((unsigned char)s[1]) && isxdigit((unsigned char)s[2])) {
      char h[3] = {s[1], s[2], 0};
      o += (char)strtol(h, nullptr, 16);
      s += 2;
    } else o += *s;
  }
  return o;
}

static esp_err_t sslMeshSend(httpd_req_t *req) {
  // Percent-encoded Thai uses up to nine bytes per codepoint on the wire.
  char body[512], from[64] = "", text[256] = "";
  if (req->content_len >= sizeof(body)) return sslText(req, "application/json", "{\"ok\":false}");
  int r = 0;
  while (r < req->content_len) {
    int got = httpd_req_recv(req, body + r, req->content_len - r);
    if (got <= 0) return sslText(req, "application/json", "{\"ok\":false}");
    r += got;
  }
  if (r > 0) {
    body[r] = 0;
    httpd_query_key_value(body, "from", from, sizeof(from));
    httpd_query_key_value(body, "text", text, sizeof(text));
  }
  bool ok = phoneSend(urlDecode(from), urlDecode(text));
  return sslText(req, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

// The phone's map page posts its GPS position here so the board can use it (map, radar, mesh sharing).
static esp_err_t sslPos(httpd_req_t *req) {
  char body[96], a[24] = "", b[24] = "", c[12] = "";
  int len = req->content_len < sizeof(body) - 1 ? req->content_len : sizeof(body) - 1;
  int r = len > 0 ? httpd_req_recv(req, body, len) : 0;
  bool ok = false;
  if (r > 0) {
    body[r] = 0;
    bool hasLat = httpd_query_key_value(body, "lat", a, sizeof(a)) == ESP_OK;
    bool hasLon = httpd_query_key_value(body, "lon", b, sizeof(b)) == ESP_OK;
    httpd_query_key_value(body, "acc", c, sizeof(c));
    double lat = atof(a), lon = atof(b);
    ok = hasLat && hasLon && fabs(lat) <= 90.0 && fabs(lon) <= 180.0 && !(lat == 0.0 && lon == 0.0);
    if (ok) gpsSetPhone(lat, lon, (float)atof(c));
  }
  return sslText(req, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
}
// Map APIs have bounded response sizes and share the same implementation on
// HTTP and HTTPS, so searching and selecting a route also work while sharing GPS.
static String mapApi(const String &path,const String &q,const String &offset,const String &name,const String &lat,const String &lon,bool post){
  if(!post&&path=="/api/map/status")return offlineStatusJson();
  if(!post&&path=="/api/map/pois")return offlinePoisJson(q,(uint32_t)max(0L,offset.toInt()));
  if(!post&&path=="/api/map/routes")return offlineRoutesJson();
  if(!post&&path=="/api/map/route")return offlineRouteJson();
  if(post&&path=="/api/map/route")return offlineSelectRoute(name)?"{\"ok\":true}":"{\"ok\":false}";
  if(post&&path=="/api/map/destination"){
    char *a=nullptr,*b=nullptr;double la=strtod(lat.c_str(),&a),lo=strtod(lon.c_str(),&b);
    bool ok=lat.length()&&lon.length()&&a&&b&&!*a&&!*b&&offlineSetDestination(la,lo,name);
    return ok?"{\"ok\":true}":"{\"ok\":false}";
  }
  return "{\"error\":\"Unknown map API\"}";
}
static void handleMapApi(){server.sendHeader("Cache-Control","no-store");server.send(200,"application/json",mapApi(server.uri(),server.arg("q"),server.arg("offset"),server.arg("name"),server.arg("lat"),server.arg("lon"),server.method()==HTTP_POST));}
static esp_err_t sslMapApi(httpd_req_t *req){
  char body[1024]={},value[512]={};bool post=req->method==HTTP_POST;
  if(post){if(req->content_len>=sizeof(body))return sslText(req,"application/json","{\"ok\":false}");int pos=0;while(pos<req->content_len){int n=httpd_req_recv(req,body+pos,req->content_len-pos);if(n<=0)return ESP_FAIL;pos+=n;}}
  else if(httpd_req_get_url_query_len(req)>=sizeof(body))return sslText(req,"application/json","{\"ok\":false}");
  else httpd_req_get_url_query_str(req,body,sizeof(body));
  auto arg=[&](const char *key)->String{value[0]=0;httpd_query_key_value(body,key,value,sizeof(value));return urlDecode(value);};
  String path=req->uri;int qm=path.indexOf('?');if(qm>=0)path=path.substring(0,qm);
  return sslText(req,"application/json",mapApi(path,arg("q"),arg("offset"),arg("name"),arg("lat"),arg("lon"),post).c_str());
}
// /maps/{z}/{x}/{y}.jpg and /maps/center.txt, streamed from the SD card in small pieces.
static esp_err_t sslMapFile(httpd_req_t *req) {
  char path[72];
  strlcpy(path, req->uri, sizeof(path));
  char *qm = strchr(path, '?');
  if (qm) *qm = 0;
  bool fontFile=strcmp(path,"/web/Sarabun-Regular.ttf")==0;
  if ((!fontFile&&strncmp(path, "/maps/", 6) != 0) || strstr(path, "..") || strlen(req->uri) >= sizeof(path)) return httpd_resp_send_404(req);
  File f = SD.open(path, FILE_READ);
  if (!f || f.isDirectory()) { Serial.printf("[https] 404 %s\n", path); return httpd_resp_send_404(req); }
  Serial.printf("[https] %s %u bytes (heap %u, lowest %u)\n", path, (unsigned)f.size(), (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap());
  httpd_resp_set_type(req, fontFile ? "font/ttf" : strstr(path, ".jpg") ? "image/jpeg" : "text/plain");
  httpd_resp_set_hdr(req, "Cache-Control", "max-age=86400");
  uint8_t *buf = (uint8_t *)malloc(2048);
  esp_err_t res = ESP_OK;
  while (buf && f.available()) {
    size_t n = f.read(buf, 2048);
    if (!n) break;
    if (httpd_resp_send_chunk(req, (const char *)buf, n) != ESP_OK) { res = ESP_FAIL; break; }
  }
  if (res == ESP_OK) httpd_resp_send_chunk(req, nullptr, 0);
  free(buf);
  f.close();
  return res;
}

static void sslStart() {
  if (sslServer) return;
  httpd_ssl_config_t cfg = HTTPD_SSL_CONFIG_DEFAULT();
  cfg.cacert_pem = (const uint8_t *)HTTPS_CERT;         // in this version the server's own certificate goes here
  cfg.cacert_len = sizeof(HTTPS_CERT);
  cfg.prvtkey_pem = (const uint8_t *)HTTPS_KEY;
  cfg.prvtkey_len = sizeof(HTTPS_KEY);
  cfg.httpd.max_open_sockets = 3;                        // each secure connection costs about 40 KB of memory
  cfg.httpd.lru_purge_enable = true;                     // drop the oldest idle connection when a new one needs room
  cfg.httpd.stack_size = 10240;
  cfg.httpd.uri_match_fn = httpd_uri_match_wildcard;
  cfg.httpd.max_uri_handlers = 12;
  if (httpd_ssl_start(&sslServer, &cfg) != ESP_OK) {
    sslServer = nullptr;
    Serial.printf("[https] could not start (free heap %u)\n", (unsigned)ESP.getFreeHeap());
    return;
  }
  const httpd_uri_t routes[] = {
    {"/", HTTP_GET, sslRoot, nullptr},
    {"/map", HTTP_GET, sslMapPage, nullptr},
    {"/mesh", HTTP_GET, sslMeshPage, nullptr},
    {"/tools", HTTP_GET, sslToolsPage, nullptr},
    {"/api/mesh", HTTP_GET, sslMeshApi, nullptr},
    {"/api/mesh/send", HTTP_POST, sslMeshSend, nullptr},
    {"/api/pos", HTTP_POST, sslPos, nullptr},
    {"/api/map/*", HTTP_GET, sslMapApi, nullptr},
    {"/api/map/*", HTTP_POST, sslMapApi, nullptr},
    {"/maps/*", HTTP_GET, sslMapFile, nullptr},
    {"/web/Sarabun-Regular.ttf", HTTP_GET, sslMapFile, nullptr},
  };
  for (const httpd_uri_t &r : routes) httpd_register_uri_handler(sslServer, &r);
  Serial.printf("[https] listening on port 443 (free heap %u)\n", (unsigned)ESP.getFreeHeap());
}

static void sslStop() {
  if (!sslServer) return;
  httpd_ssl_stop(sslServer);
  sslServer = nullptr;
}
static void handleNotFound() {
  // Any request for another host name (google.com, captive.apple.com, ...) goes home.
  String host = server.hostHeader();
  if (host.length() && host != apIP.toString() && host != "survival.wiki") {
    redirectHome();
    return;
  }
  server.send(404, "text/html; charset=utf-8",
              "<html lang='th'><meta charset='utf-8'><meta name=viewport content='width=device-width'><h2>ไม่พบหน้านี้</h2>"
              "<p><a href='/'>กลับสู่คู่มือเอาตัวรอด</a></p></html>");
}

static void webTask(void *) {
  while (!stopRequested) {
    dns.processNextRequest();
    server.handleClient();
    vTaskDelay(pdMS_TO_TICKS(2));
  }
  webTaskHandle = nullptr;
  vTaskDelete(nullptr);
}

void webStart(const WifiConfig &cfg) {
  if (running) return;
  static bool eventsAdded = false;
  if (!eventsAdded) {
    eventsAdded = true;
    WiFi.onEvent([](WiFiEvent_t e, WiFiEventInfo_t info) {
      if (e == ARDUINO_EVENT_WIFI_AP_STACONNECTED) Serial.println("[wifi] phone connected");
      else if (e == ARDUINO_EVENT_WIFI_AP_STADISCONNECTED) Serial.println("[wifi] phone disconnected");
      else if (e == ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED)
        Serial.printf("[wifi] gave phone address " IPSTR "\n", IP2STR(&info.wifi_ap_staipassigned.ip));
    });
  }
  // Start the hotspot with the chip's defaults (address 192.168.4.1, DHCP on).
  // Setting the address before the AP is up can leave phones without an IP
  // address, which phones report as "unable to join".
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  const char *pass = cfg.password.length() >= 8 ? cfg.password.c_str() : nullptr;
  bool ok = WiFi.softAP(cfg.ssid.c_str(), pass, cfg.channel, 0, 4);
  delay(300);
  Serial.printf("[wifi] hotspot '%s' %s, %s, channel %d, address %s\n", cfg.ssid.c_str(),
                ok ? "started" : "FAILED", pass ? "password set" : "open",
                cfg.channel, WiFi.softAPIP().toString().c_str());

  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", apIP);

  if (!routesAdded) {
  routesAdded = true;
  // Connectivity checks used by Android, iOS/macOS and Windows -> portal pop-up
  const char *probes[] = {"/generate_204", "/gen_204", "/hotspot-detect.html",
                          "/library/test/success.html", "/connecttest.txt",
                          "/ncsi.txt", "/redirect", "/success.txt", "/canonical.html",
                          "/fwlink"};
  for (const char *p : probes) server.on(p, redirectHome);

  server.on("/", handleIndex);
  server.on("/index.html", handleIndex);
  server.on("/api/status", handleStatus);
  server.on("/mesh", handleMeshPage);
  server.on("/tools", handleToolsPage);
  server.on("/map", handleMapPage);
  for(const char *p:{"/api/map/status","/api/map/pois","/api/map/routes","/api/map/route"})server.on(p,HTTP_GET,handleMapApi);
  server.on("/api/map/route",HTTP_POST,handleMapApi);
  server.on("/api/map/destination",HTTP_POST,handleMapApi);
  server.serveStatic("/maps", SD, "/maps", "max-age=86400");
  server.on("/api/mesh", handleMeshApi);
  server.on("/api/mesh/send", HTTP_POST, handleMeshSend);
  server.serveStatic("/web", SD, "/web", "max-age=86400");
  server.serveStatic("/wiki", SD, "/wiki", "max-age=3600");
  server.serveStatic("/library", SD, "/library", "max-age=86400");
  server.onNotFound(handleNotFound);
  }
  server.begin();
  sslStart();

  stopRequested = false;
  running = true;
  xTaskCreatePinnedToCore(webTask, "web", 12288, nullptr, 1, &webTaskHandle, 0);   // room for the chat snapshot
}

void webStop() {
  if (!running) return;
  stopRequested = true;
  for (int i = 0; i < 200 && webTaskHandle; i++) delay(5);
  sslStop();
  server.stop();
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  running = false;
}

bool webRunning() { return running; }
int webClientCount() { return running ? WiFi.softAPgetStationNum() : 0; }
String webIP() { return apIP.toString(); }

static String qrEscape(const String &s) {
  String o;
  for (char c : s) {
    if (c == '\\' || c == ';' || c == ',' || c == ':' || c == '"') o += '\\';
    o += c;
  }
  return o;
}

String webQRText(const WifiConfig &cfg) {
  if (cfg.password.length() >= 8)
    return "WIFI:T:WPA;S:" + qrEscape(cfg.ssid) + ";P:" + qrEscape(cfg.password) + ";;";
  return "WIFI:T:nopass;S:" + qrEscape(cfg.ssid) + ";;";
}
