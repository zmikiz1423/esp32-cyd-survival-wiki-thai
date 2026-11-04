// Wi-Fi hotspot + captive portal + web server (runs in its own task on core 0)
#pragma once
#include <Arduino.h>

struct WifiConfig {
  String ssid = "SurvivalWiki";
  String password = "";      // empty = open network; otherwise 8+ characters
  uint8_t channel = 6;
};

void webStart(const WifiConfig &cfg);
void webStop();
bool webRunning();
int  webClientCount();
String webIP();
String webQRText(const WifiConfig &cfg);   // "WIFI:..." string for the join QR code
