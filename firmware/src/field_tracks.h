#pragma once
#include <Arduino.h>
#include "offline_maps.h"

struct FieldTrackStatus {
  bool recording;
  bool available;
  bool backtracking;
  uint32_t points;
  uint32_t bytes;
  String error;
};

// Main-task entry points. A recording is explicitly started and is off at boot.
void fieldTrackBegin();
void fieldTrackLoop();
bool fieldTrackRecording();
bool fieldTrackSetRecording(bool enabled);
FieldTrackStatus fieldTrackStatus();
String fieldTrackStatusJson();
bool fieldTrackStartBacktrack();
void fieldTrackClearBacktrack();
// Main-task display points, sampled over the entire file, at most 600 points.
const OfflineRoutePoint *fieldTrackBacktrack(int &count);

// A streaming reader reserves the source against replacement for its lifetime.
// Sink returns false when a client disconnects. No complete GPX lives in RAM.
using FieldTrackSink = bool (*)(const char *data, size_t length, void *context);
bool fieldTrackExport(FieldTrackSink sink, void *context, uint32_t expectedRevision = UINT32_MAX);
