// Off-grid text messaging between boards over ESP-NOW (no router, no internet).
// Messages are broadcast with a hop count and re-broadcast by every board that hears them,
// so they can hop across more than two units. They are NOT encrypted.
#pragma once
#include <Arduino.h>

#define MESH_NAME_LEN 12
#define MESH_TEXT_LEN 64
#define MESH_LOG      12

struct MeshMsg {
  char from[MESH_NAME_LEN];
  char text[MESH_TEXT_LEN];
  bool mine;
  uint32_t ms;       // millis() when sent or received
  uint32_t seq;      // 1, 2, 3...: lets a phone ask for "everything after message N"
  uint32_t id;       // the id the message travels under, so "got it" replies can be matched to it
  uint8_t ackN;      // how many boards have said they got it (only counted for messages we sent)
  char ack[6][MESH_NAME_LEN];   // the first few of those boards
};

bool meshBegin(uint8_t channel, bool apActive);   // (re)start; apActive = the SurvivalWiki hotspot is running
void meshEnd();
bool meshActive();
void meshLoop();                                  // call from loop()
bool meshSend(const char *text);
// For phones using the hotspot's chat page. Safe to call from the web server task: the message is
// queued and sent from meshLoop. `from` is the name the phone chose.
bool meshQueueSend(const char *from, const char *text);
int  meshSnapshot(MeshMsg *out, int max, uint32_t *latestSeq);   // thread-safe copy of the message log
uint32_t meshLastSeq();                           // sequence number of the newest message (0 = none yet)
uint32_t meshChangeCount();                       // goes up on any change to the log, including "got it" replies
int  meshCount();
const MeshMsg &meshAt(int i);                     // 0 = oldest
const char *meshName();
void meshSetName(const char *name);
// Rough distance from signal strength. These two numbers are tuned on real boards (see the range issue).
#define MESH_RSSI_1M  (-47)     // signal at 1 metre, dBm
#define MESH_PATH_N   2.5f      // path loss exponent: ~2 open ground, 3+ indoors or in woods

struct MeshPeer {
  char name[MESH_NAME_LEN];
  int8_t rssi;           // dBm, smoothed; 0 = not measured yet (only heard through a relay)
  uint32_t ageS;         // seconds since we last heard it
  bool hasPos;           // the board shared a GPS position recently
  double lat, lon;
  uint32_t posAgeS;      // seconds since that position arrived
};
// Share our GPS position in the hello pings (call with valid=false to stop sharing). Positions are NOT encrypted.
void meshSetMyPos(bool valid, double lat, double lon);
int   meshPeerList(MeshPeer *out, int max);       // strongest first
float meshDistanceM(int rssi);                    // metres, -1 if unknown. A rough guess only.
int   meshBars(int rssi);                         // 0..4

bool meshTakeNew();                               // true once per received message
int  meshUnread();
void meshMarkRead();
int  meshPeersHeard();                            // boards heard in the last 2 minutes (hello pings every 30 s)
#ifdef SHOT_SERIAL
void meshDemo();                                  // screenshot builds only
#endif
