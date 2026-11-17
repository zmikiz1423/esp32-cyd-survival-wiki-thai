#pragma once
#include <math.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

namespace FieldLogic {
inline int batteryPercent(float volts) {
  static const float v[] = {3.20f,3.40f,3.60f,3.70f,3.80f,3.90f,4.00f,4.10f,4.20f};
  static const int p[] = {0,3,10,25,45,65,80,90,100};
  if (volts <= v[0]) return 0;
  for (int i=1;i<9;i++) {
    if (volts <= v[i]) return (int)lroundf(p[i-1]+(volts-v[i-1])*(p[i]-p[i-1])/(v[i]-v[i-1]));
  }
  return 100;
}
struct BatteryFilter {
  bool initialized = false;
  float voltage = 0;
  int shown = 0;
  uint32_t lastDisplay = 0;
  void reset() { initialized=false; voltage=0; shown=0; lastDisplay=0; }
  bool update(float sample, uint32_t now) {
    if (!isfinite(sample) || sample<2.50f || sample>4.45f) return false;
    if (!initialized) {
      initialized=true; voltage=sample; shown=batteryPercent(sample); lastDisplay=now;
      return true;
    }
    voltage += 0.08f*(sample-voltage);
    int target=batteryPercent(voltage);
    // Keep small ripple inside a +/-1 percentage-point dead band. Larger
    // changes walk one point at a time, at most once every two seconds.
    if (now-lastDisplay>=2000 && abs(target-shown)>=2) {
      shown += target>shown ? 1 : -1;
      lastDisplay=now;
    }
    return true;
  }
};
inline uint32_t crc32(const uint8_t *bytes,size_t length) {
  uint32_t crc=0xffffffff;
  for(size_t i=0;i<length;i++) {
    crc^=bytes[i];
    for(int b=0;b<8;b++) crc=(crc>>1)^((crc&1)?0xedb88320u:0u);
  }
  return ~crc;
}
inline int unhex(char c) {
  if(c>='0'&&c<='9')return c-'0';
  if(c>='a'&&c<='f')return c-'a'+10;
  if(c>='A'&&c<='F')return c-'A'+10;
  return -1;
}
inline bool utf8Text(const char *text,size_t length) {
  size_t i=0;
  while(i<length) {
    uint8_t c=(uint8_t)text[i++];
    if(c<0x80){if(c<32||c==127)return false;continue;}
    int n=c>=0xc2&&c<=0xdf?1:c>=0xe0&&c<=0xef?2:c>=0xf0&&c<=0xf4?3:0;
    if(!n||i+n>length)return false;
    uint32_t cp=c&((1<<(6-n))-1);
    for(int j=0;j<n;j++){uint8_t d=(uint8_t)text[i++];if((d&0xc0)!=0x80)return false;cp=(cp<<6)|(d&63);}
    if((n==2&&cp<0x800)||(n==3&&cp<0x10000)||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))return false;
  }
  return true;
}
}
