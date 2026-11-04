#include "mesh.h"
#include "utf8.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#define MESH_MAGIC 0x53      // 'S'
#define MESH_VER   1
#define MESH_VER_ACK 2       // "got it" replies; older firmware drops anything that isn't version 1
#define MESH_TTL   4         // hops a message may travel

#pragma pack(push, 1)
struct Pkt {
  uint8_t magic, ver, ttl, rsv;
  uint32_t id;
  char from[MESH_NAME_LEN];
  char text[MESH_TEXT_LEN];
};
#pragma pack(pop)

static const uint8_t BCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static bool active = false;
static char myName[MESH_NAME_LEN] = "";
static MeshMsg logv[MESH_LOG];
static int logN = 0;
static int unread = 0;
static bool gotNew = false;
static uint32_t lastSendMs = 0;
static uint32_t nextHello = 0;
#define HELLO_MS 30000UL      // each board announces itself this often, so "heard" shows who is nearby
#define HEARD_MS 120000UL     // a board counts as nearby if we heard it within this long

// Signal strength of recent ESP-NOW frames, by sender MAC. The normal receive callback does not
// report RSSI, so a passive packet sniffer (management frames only) reads it from the radio header.
struct RssiEntry { uint8_t mac[6]; int8_t rssi; uint32_t ms; };
static RssiEntry rssiTab[8];

static void onSniff(void *buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT) return;
  const wifi_promiscuous_pkt_t *pkt = (const wifi_promiscuous_pkt_t *)buf;
  const uint8_t *p = pkt->payload;
  if (pkt->rx_ctrl.sig_len < 28) return;
  if (p[0] != 0xD0) return;                                              // action frame
  if (p[24] != 0x7F || p[25] != 0x18 || p[26] != 0xFE || p[27] != 0x34) return;   // vendor action, Espressif OUI = ESP-NOW
  const uint8_t *src = p + 10;
  int slot = 0;
  for (int i = 0; i < 8; i++) {
    if (memcmp(rssiTab[i].mac, src, 6) == 0) { slot = i; break; }
    if (rssiTab[i].ms < rssiTab[slot].ms) slot = i;
  }
  memcpy(rssiTab[slot].mac, src, 6);
  rssiTab[slot].rssi = pkt->rx_ctrl.rssi;
  rssiTab[slot].ms = millis();
}

static int8_t rssiFor(const uint8_t *mac) {
  for (int i = 0; i < 8; i++)
    if (memcmp(rssiTab[i].mac, mac, 6) == 0 && millis() - rssiTab[i].ms < 500) return rssiTab[i].rssi;
  return 0;                                                              // unknown
}

// Receive queue (filled from the Wi-Fi task, drained in meshLoop)
#define RXQ 8
struct Rx { Pkt p; int8_t rssi; };
static Rx rxq[RXQ];
static volatile int rxHead = 0, rxTail = 0;
static portMUX_TYPE rxMux = portMUX_INITIALIZER_UNLOCKED;

// Duplicate filter
#define SEEN_N 64
static uint32_t seen[SEEN_N];
static int seenPos = 0;

// Re-broadcasts waiting for their random delay
struct Pending { Pkt p; uint32_t due; bool used; };
static Pending pend[12];

// Boards heard recently
struct Peer { char name[MESH_NAME_LEN]; uint32_t ms; int8_t rssi; bool hasPos; double lat, lon; uint32_t posMs; };   // rssi 0 = never measured
static bool myPosValid = false;
static double myLat = 0, myLon = 0;
#define POS_MS 15000UL        // hello interval while we share a position
#define POS_STALE_MS 90000UL  // a shared position older than this is not shown
static Peer peers[8];

static uint32_t keyOf(const Pkt &p) {
  uint32_t h = p.id;
  for (int i = 0; i < MESH_NAME_LEN && p.from[i]; i++) h = h * 31 + (uint8_t)p.from[i];
  return h | 1;     // never 0, so 0 means "empty slot"
}

static bool seenBefore(uint32_t k) {
  for (int i = 0; i < SEEN_N; i++) if (seen[i] == k) return true;
  return false;
}
static void markSeen(uint32_t k) { seen[seenPos] = k; seenPos = (seenPos + 1) % SEEN_N; }

// The message log and peer table are read by the web server task (phone chat page), so they are
// guarded by a spinlock. Keep the locked sections short.
static portMUX_TYPE dataMux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t seqCounter = 0;
static uint32_t changeCounter = 0;     // seqCounter plus every "got it" reply

static void addLog(const char *from, const char *text, bool mine, uint32_t id = 0) {
  portENTER_CRITICAL(&dataMux);
  if (logN == MESH_LOG) { memmove(&logv[0], &logv[1], sizeof(MeshMsg) * (MESH_LOG - 1)); logN--; }
  MeshMsg &m = logv[logN++];
  memset(&m, 0, sizeof(m));
  utf8Copy(m.from, from, sizeof(m.from));
  utf8Copy(m.text, text, sizeof(m.text));
  m.mine = mine;
  m.ms = millis();
  m.seq = ++seqCounter;
  m.id = id;
  changeCounter++;
  portEXIT_CRITICAL(&dataMux);
}

// Another board says it got message `id`. Count it once per board, on the message we sent.
static void noteAck(uint32_t id, const char *who) {
  portENTER_CRITICAL(&dataMux);
  for (int i = 0; i < logN; i++) {
    MeshMsg &m = logv[i];
    if (!m.mine || m.id != id) continue;
    bool known = false;
    for (int k = 0; k < 6 && k < m.ackN; k++) if (strcmp(m.ack[k], who) == 0) known = true;
    if (!known && m.ackN < 250) {
      if (m.ackN < 6) utf8Copy(m.ack[m.ackN], who, MESH_NAME_LEN);
      m.ackN++;
      changeCounter++;
    }
    break;
  }
  portEXIT_CRITICAL(&dataMux);
}

int meshSnapshot(MeshMsg *out, int max, uint32_t *latestSeq) {
  portENTER_CRITICAL(&dataMux);
  int n = min(logN, max);
  for (int i = 0; i < n; i++) out[i] = logv[logN - n + i];
  if (latestSeq) *latestSeq = seqCounter;
  portEXIT_CRITICAL(&dataMux);
  return n;
}

// rssi is only passed for frames that came straight from the author (not a relay), 0 otherwise.
static void notePeer(const char *name, int rssi, bool hasPos = false, double lat = 0, double lon = 0) {
  portENTER_CRITICAL(&dataMux);
  int free_i = -1, oldest = 0, hit = -1;
  for (int i = 0; i < 8; i++) {
    if (peers[i].name[0] && strcmp(peers[i].name, name) == 0) { hit = i; break; }
    if (!peers[i].name[0] && free_i < 0) free_i = i;
    if (peers[i].ms < peers[oldest].ms) oldest = i;
  }
  if (hit >= 0) {
    peers[hit].ms = millis();
    if (hasPos) { peers[hit].hasPos = true; peers[hit].lat = lat; peers[hit].lon = lon; peers[hit].posMs = millis(); }
    if (rssi) peers[hit].rssi = peers[hit].rssi ? (int8_t)((peers[hit].rssi * 3 + rssi) / 4) : (int8_t)rssi;   // smooth the jitter
  } else {
    int i = free_i >= 0 ? free_i : oldest;
    utf8Copy(peers[i].name, name, sizeof(peers[i].name));
    peers[i].ms = millis();
    peers[i].rssi = (int8_t)rssi;
    peers[i].hasPos = hasPos; peers[i].lat = lat; peers[i].lon = lon; peers[i].posMs = hasPos ? millis() : 0;
  }
  portEXIT_CRITICAL(&dataMux);
}

static void onRecv(const uint8_t *mac, const uint8_t *data, int len) {
  if (len != (int)sizeof(Pkt)) return;
  int8_t rssi = rssiFor(mac);
  portENTER_CRITICAL(&rxMux);
  int next = (rxHead + 1) % RXQ;
  if (next != rxTail) { memcpy(&rxq[rxHead].p, data, sizeof(Pkt)); rxq[rxHead].rssi = rssi; rxHead = next; }
  portEXIT_CRITICAL(&rxMux);
}

static void onSent(const uint8_t *mac, esp_now_send_status_t st) {}

static void sanitize(char *s, size_t n) {
  s[n - 1] = 0;
  int len=strlen(s),r=0,w=0;
  while(r<len) {
    int start=r;uint32_t cp=utf8Decode(s,len,r);
    if(cp==0xFFFD)continue;
    if(cp<32||cp==127) {s[w++]=' ';continue;}
    for(int j=start;j<r;j++)s[w++]=s[j];
  }
  s[w]=0;
}

static void defaultName() {
  if (myName[0]) return;
  uint64_t mac = ESP.getEfuseMac();
  snprintf(myName, sizeof(myName), "N%02X%02X", (uint8_t)(mac >> 32), (uint8_t)(mac >> 40));
}

bool meshActive() { return active; }
const char *meshName() { defaultName(); return myName; }
void meshSetName(const char *name) {
  String t = String(name);
  t.trim();
  if (!t.length()) { myName[0] = 0; defaultName(); return; }
  utf8Copy(myName, t.c_str(), sizeof(myName));
  sanitize(myName, sizeof(myName));
}

bool meshBegin(uint8_t channel, bool apActive) {
  meshEnd();
  defaultName();
  if (!apActive) {                       // hotspot off: bring the radio up ourselves, on the same channel
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);
  }
  if (esp_now_init() != ESP_OK) { Serial.println("[mesh] esp_now_init failed"); return false; }
  esp_now_register_recv_cb(onRecv);
  esp_now_register_send_cb(onSent);
  esp_now_peer_info_t peer;
  memset(&peer, 0, sizeof(peer));
  memcpy(peer.peer_addr, BCAST, 6);
  peer.channel = 0;                      // current channel
  peer.encrypt = false;
  peer.ifidx = apActive ? WIFI_IF_AP : WIFI_IF_STA;
  if (esp_now_add_peer(&peer) != ESP_OK) { Serial.println("[mesh] add_peer failed"); esp_now_deinit(); return false; }
  // The signal listener (promiscuous mode) stops phones from joining the hotspot, so it only runs when the
  // hotspot is off. With the hotspot on, boards still talk, but there is no signal-based distance.
  if (!apActive) {
    wifi_promiscuous_filter_t filt;
    filt.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;     // management frames only: ESP-NOW rides in these
    esp_wifi_set_promiscuous_filter(&filt);
    esp_wifi_set_promiscuous_rx_cb(onSniff);
    esp_wifi_set_promiscuous(true);
  }
  active = true;
  nextHello = millis() + 1500 + (esp_random() % 2000);
  Serial.printf("[mesh] on, name %s, channel %d, %s\n", myName, channel, apActive ? "with hotspot" : "radio only");
  return true;
}

void meshEnd() {
  if (!active) return;
  esp_wifi_set_promiscuous(false);
  esp_now_deinit();
  active = false;
  for (auto &p : pend) p.used = false;
}

static void txPkt(const Pkt &p) { esp_now_send(BCAST, (const uint8_t *)&p, sizeof(Pkt)); }

static bool sendAs(const char *from, const char *text) {
  if (!active || !text || !text[0] || strlen(text)>=MESH_TEXT_LEN) return false;
  uint32_t now = millis();
  if (now - lastSendMs < 700) return false;      // simple rate limit
  lastSendMs = now;
  Pkt p;
  memset(&p, 0, sizeof(p));
  p.magic = MESH_MAGIC; p.ver = MESH_VER; p.ttl = MESH_TTL;
  p.id = esp_random();
  utf8Copy(p.from, from, sizeof(p.from));
  utf8Copy(p.text, text, sizeof(p.text));
  sanitize(p.from, sizeof(p.from));
  sanitize(p.text, sizeof(p.text));
  markSeen(keyOf(p));
  addLog(p.from, p.text, true, p.id);
  txPkt(p);
  Serial.printf("[mesh] sent message %08x from %s\n", (unsigned)p.id, p.from);
  return true;
}

bool meshSend(const char *text) { return sendAs(myName, text); }

// Messages typed on a phone wait here until meshLoop (the main task) can send them.
struct OutReq { char from[MESH_NAME_LEN]; char text[MESH_TEXT_LEN]; };
#define OUTQ 4
static OutReq outq[OUTQ];
static volatile int outHead = 0, outTail = 0;
static portMUX_TYPE outMux = portMUX_INITIALIZER_UNLOCKED;

bool meshQueueSend(const char *from, const char *text) {
  if (!active || !text || !text[0] || strlen(text)>=MESH_TEXT_LEN || (from&&strlen(from)>=MESH_NAME_LEN)) return false;
  bool ok = false;
  portENTER_CRITICAL(&outMux);
  int next = (outHead + 1) % OUTQ;
  if (next != outTail) {
    utf8Copy(outq[outHead].from, (from && from[0]) ? from : "Phone", sizeof(outq[0].from));
    utf8Copy(outq[outHead].text, text, sizeof(outq[0].text));
    outHead = next;
    ok = true;
  }
  portEXIT_CRITICAL(&outMux);
  return ok;
}

// Pass a packet on with one hop fewer, after a short random delay so boards don't all talk at once.
static void relay(const Pkt &p, uint32_t now) {
  if (p.ttl <= 1) return;
  for (auto &q : pend) {
    if (q.used) continue;
    q.p = p; q.p.ttl--;
    q.due = now + 30 + (esp_random() % 120);
    q.used = true;
    return;
  }
}

// "Got it" reply for message `id`: a tiny packet at the newer protocol version, which boards on older
// firmware ignore instead of showing as a blank message.
static void queueAck(uint32_t id, uint32_t now) {
  for (auto &q : pend) {
    if (q.used) continue;
    memset(&q.p, 0, sizeof(Pkt));
    q.p.magic = MESH_MAGIC; q.p.ver = MESH_VER_ACK; q.p.rsv = 2; q.p.ttl = MESH_TTL;
    q.p.id = id;
    utf8Copy(q.p.from, myName, sizeof(q.p.from));
    markSeen(keyOf(q.p));                          // so we ignore our own reply when it comes back around
    Serial.printf("[mesh] will tell the sender we got message %08x\n", (unsigned)id);
    q.due = now + 60 + (esp_random() % 200);
    q.used = true;
    return;
  }
}

void meshLoop() {
  if (!active) return;
  uint32_t now = millis();
  // send one waiting phone message (the rate limit decides when the next one may go)
  if (outTail != outHead && now - lastSendMs >= 700) {
    OutReq r;
    portENTER_CRITICAL(&outMux);
    memcpy(&r, &outq[outTail], sizeof(OutReq));
    outTail = (outTail + 1) % OUTQ;
    portEXIT_CRITICAL(&outMux);
    sendAs(r.from, r.text);
  }
  // drain received packets
  while (true) {
    Pkt p;
    int8_t rssi = 0;
    portENTER_CRITICAL(&rxMux);
    bool have = rxTail != rxHead;
    if (have) { memcpy(&p, &rxq[rxTail].p, sizeof(Pkt)); rssi = rxq[rxTail].rssi; rxTail = (rxTail + 1) % RXQ; }
    portEXIT_CRITICAL(&rxMux);
    if (!have) break;
    if (p.magic != MESH_MAGIC || (p.ver != MESH_VER && p.ver != MESH_VER_ACK)) continue;
    sanitize(p.from, sizeof(p.from));
    sanitize(p.text, sizeof(p.text));
    if (p.ver == MESH_VER_ACK) {                   // "got it" reply: p.id is the message, p.from is who got it
      if (p.rsv != 2) continue;
      uint32_t ak = keyOf(p);
      if (seenBefore(ak)) continue;
      markSeen(ak);
      if (strcmp(p.from, myName) == 0) continue;
      Serial.printf("[mesh] %s says it got message %08x\n", p.from, (unsigned)p.id);
      noteAck(p.id, p.from);
      relay(p, now);                               // acks hop too, so far-away boards can confirm
      continue;
    }
    if (p.rsv == 1) {                              // hello ping: note the board, nothing else
      if (strcmp(p.from, myName) != 0) {            // hello pings are never relayed, so rssi is the author's
        double la, lo; bool pos = false;
        if (p.text[0] == 'P' && sscanf(p.text + 1, "%lf,%lf", &la, &lo) == 2 && fabs(la) <= 90 && fabs(lo) <= 180) pos = true;   // older boards send no text in hellos
        notePeer(p.from, rssi, pos, la, lo);
      }
      continue;
    }
    uint32_t k = keyOf(p);
    if (seenBefore(k)) continue;
    markSeen(k);
    if (strcmp(p.from, myName) == 0) continue;   // someone using our name: don't show our own echo
    addLog(p.from, p.text, false, p.id);
    Serial.printf("[mesh] from %s: %s (ttl %d, rssi %d)\n", p.from, p.text, p.ttl, rssi);
    unread++;
    gotNew = true;
    queueAck(p.id, now);                           // tell the sender we got it
    relay(p, now);                                 // and pass it on after a short random delay
  }
  for (auto &q : pend) {
    if (q.used && (int32_t)(now - q.due) >= 0) { txPkt(q.p); q.used = false; }
  }
  if ((int32_t)(now - nextHello) >= 0) {           // hello ping: direct neighbours only, not passed on
    nextHello = now + (myPosValid ? POS_MS : HELLO_MS);
    Pkt h;
    memset(&h, 0, sizeof(h));
    h.magic = MESH_MAGIC; h.ver = MESH_VER; h.ttl = 1; h.rsv = 1;
    h.id = esp_random();
    utf8Copy(h.from, myName, sizeof(h.from));
    if (myPosValid) snprintf(h.text, sizeof(h.text), "P%.5f,%.5f", myLat, myLon);   // older boards ignore a hello's text
    txPkt(h);
  }
}

#ifdef SHOT_SERIAL
// Screenshot builds only: fill the inbox with a believable conversation.
void meshDemo() {
  logN = 0;
  addLog("Jim", "Made it to the ridge. Water at the creek.", false);
  logv[logN - 1].ms = millis() - 14 * 60000UL;
  addLog("Sam", "Copy. Heading your way.", false);
  logv[logN - 1].ms = millis() - 9 * 60000UL;
  addLog("me", "Need help", true);
  utf8Copy(logv[logN - 1].from, myName, sizeof(logv[0].from));
  logv[logN - 1].ackN = 2;
  utf8Copy(logv[logN - 1].ack[0], "Jim", MESH_NAME_LEN);
  utf8Copy(logv[logN - 1].ack[1], "Sam", MESH_NAME_LEN);
  logv[logN - 1].ms = millis() - 3 * 60000UL;
  addLog("Sam", "On my way, 10 minutes out.", false);
  logv[logN - 1].ms = millis() - 20000UL;
  for (auto &p : peers) p.name[0] = 0;
  notePeer("Jim", -52);
  notePeer("Sam", -71);
  notePeer("Alex", -84);
}
#endif

void meshSetMyPos(bool valid, double lat, double lon) { myPosValid = valid; myLat = lat; myLon = lon; }

uint32_t meshLastSeq() { return seqCounter; }
uint32_t meshChangeCount() { return changeCounter; }
int meshCount() { return logN; }
const MeshMsg &meshAt(int i) { return logv[i]; }
bool meshTakeNew() { bool g = gotNew; gotNew = false; return g; }
int meshUnread() { return unread; }
void meshMarkRead() { unread = 0; }

int meshPeersHeard() {
  int n = 0;
  uint32_t now = millis();
  for (auto &p : peers) if (p.name[0] && now - p.ms < HEARD_MS) n++;
  return n;
}

// Nearby boards, strongest signal first. Boards we have only heard via a relay have rssi 0 and go last.
int meshPeerList(MeshPeer *out, int max) {
  int n = 0;
  uint32_t now = millis();
  portENTER_CRITICAL(&dataMux);
  for (auto &p : peers) {
    if (!p.name[0] || now - p.ms >= HEARD_MS || n >= max) continue;
    int i = n++;
    while (i > 0 && ((p.rssi ? p.rssi : -200) > (out[i - 1].rssi ? out[i - 1].rssi : -200))) { out[i] = out[i - 1]; i--; }
    utf8Copy(out[i].name, p.name, sizeof(out[i].name));
    out[i].rssi = p.rssi;
    out[i].ageS = (now - p.ms) / 1000;
    out[i].hasPos = p.hasPos && now - p.posMs < POS_STALE_MS;
    out[i].lat = p.lat; out[i].lon = p.lon;
    out[i].posAgeS = (now - p.posMs) / 1000;
  }
  portEXIT_CRITICAL(&dataMux);
  return n;
}

// Log-distance path loss: rssi = A - 10 * n * log10(d). A is the signal at 1 m, n depends on the surroundings.
float meshDistanceM(int rssi) {
  if (!rssi) return -1;
  return powf(10.0f, ((float)MESH_RSSI_1M - rssi) / (10.0f * MESH_PATH_N));
}

int meshBars(int rssi) {
  if (!rssi) return 0;
  if (rssi >= -55) return 4;
  if (rssi >= -65) return 3;
  if (rssi >= -75) return 2;
  if (rssi >= -85) return 1;
  return 0;
}

