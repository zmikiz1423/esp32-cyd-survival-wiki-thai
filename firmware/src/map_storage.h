#pragma once
#include <Arduino.h>

// Each map package is independent: /maps (legacy) or /maps-<ASCII slug>.
// Only a page of entries lives in RAM, so there is no configured package count.
struct MapSetEntry { String path, name; };
struct MapStorageSnapshot { String root, name; uint32_t revision; };
void mapStorageBegin();                         // call after mounting SD, main task
MapStorageSnapshot mapStorageSnapshot();       // coherent copy for an HTTP request
String mapStorageRoot();
String mapStoragePath(const String &relative);  // accepts a relative path with or without leading '/'
String mapStorageName();
uint32_t mapStorageRevision();
// offset/next count valid packages; UINT32_MAX marks the end.
int mapStorageList(uint32_t offset, MapSetEntry *items, int capacity, uint32_t &next);
bool mapStorageSelect(const String &root);     // main task, persists before changing
