#pragma once
#include <Arduino.h>

struct FieldConfig {
  bool batteryEnabled = false;
  bool soundEnabled = true;
  bool loraEnabled = false;
  float batteryCalibration = 1.0f;
};
struct FieldBattery {
  bool enabled, valid;
  float volts;
  int percent;
  uint32_t revision;
};
enum class FieldAlert : uint8_t { Test, LowBattery, Sunset, Message };
enum class FieldLoRaState : uint8_t { Off, NeedsConfiguration, Configuring, SwitchToNormal, Ready, Error };
struct FieldRadioMessage {
  char from[9];
  char text[161];
  bool mine;
  uint32_t seq, ms;
};

void fieldHardwareBegin(bool sdMounted);
void fieldHardwareLoop(uint32_t now);
FieldConfig fieldConfig();
bool fieldSaveConfig(const FieldConfig &config); // main task only; save before applying
FieldBattery fieldBattery();
void fieldAlert(FieldAlert alert);               // main task only; nonblocking
bool fieldLoRaConfigure();                      // M0=M1=HIGH before this call
bool fieldLoRaNormalMode();                     // M0=M1=LOW after verified configuration
FieldLoRaState fieldLoRaState();
String fieldLoRaStateText();
bool fieldLoRaSend(const String &text);           // true means UART accepted, not delivery confirmed
int fieldLoRaSnapshot(FieldRadioMessage *items, int capacity, uint32_t *latest);
bool fieldLoRaTakeNew();
String fieldHardwareStatusJson();
