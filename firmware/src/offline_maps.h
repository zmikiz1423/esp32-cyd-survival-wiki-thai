#pragma once
#include <Arduino.h>

struct OfflineRoutePoint { float lat, lon; bool start; };
struct OfflineDestination { bool valid; double lat, lon; char name[96]; uint32_t seq; };
// Routes are deliberately bounded to 600 display points, sampled across the
// entire file. Keep the original GPX on SD for the full-resolution track.
const OfflineRoutePoint *offlineRoute(int &count);
bool offlineRouteRefresh();  // main task only; returns true when selection changed
OfflineDestination offlineDestination();
bool offlineSetDestination(double lat, double lon, const String &name);
String offlineStatusJson();
String offlinePoisJson(const String &query, uint32_t offset);
struct OfflinePoi { double lat, lon; String name, category; };
// UINT32_MAX marks end of file. A scan is bounded even for an absent query.
int offlineFindPois(const String &query, uint32_t offset, OfflinePoi *items, int capacity, uint32_t &next);
String offlineRoutesJson();
bool offlineSelectRoute(const String &name);
String offlineRouteJson();
