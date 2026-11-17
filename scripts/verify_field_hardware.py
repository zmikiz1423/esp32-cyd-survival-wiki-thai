"""Compile actual battery, audio and E220 firmware code with mocked peripherals."""
from pathlib import Path
import json, re, subprocess, time

ROOT=Path(__file__).resolve().parents[1]
work=ROOT/'.thai-work'/('field-hardware-'+str(time.time_ns()))
work.mkdir(parents=True)
arduino=(ROOT/'scripts/tests/map_native/Arduino.h').read_text('utf-8')
arduino=re.sub(r'inline uint32_t millis\(\).*?\}\n', 'extern uint32_t mockMillis;\ninline uint32_t millis(){return mockMillis;}\n', arduino)
arduino+='''
#include <cmath>
#include <deque>
#include <vector>
using std::isfinite;
extern uint32_t adcBase, pwmDuty;
#define INPUT 0
#define OUTPUT 1
#define HIGH 1
#define ADC_11db 3
#define SERIAL_8N1 0
inline void pinMode(int,int){}
inline void digitalWrite(int,int){}
inline void analogSetPinAttenuation(int,int){}
inline uint32_t analogReadMilliVolts(int){static int n=0;int jitter[]={-2,3,0,1,-1,2,0,1,350};return adcBase+jitter[(n++)%9];}
inline void ledcSetup(int,int,int){}
inline void ledcAttachPin(int,int){}
inline void ledcWrite(int,uint32_t duty){pwmDuty=duty;}
inline void ledcWriteTone(int,int){}
inline uint32_t esp_random(){static uint32_t n=100;return ++n;}
struct EspMock{uint64_t getEfuseMac(){return 0x123456789abcdef0ULL;}};
inline EspMock ESP;
'''
(work/'Arduino.h').write_text(arduino,'utf-8')
sd=(ROOT/'scripts/tests/map_native/SD.h').read_text('utf-8')
sd=sd.replace(' size_t print(',''' String readString(){String s;while(available())s+=(char)read();return s;}
 size_t write(const uint8_t *p,size_t n){if(!node||!node->open)return 0;node->file.write((const char*)p,n);node->file.flush();return node->file.fail()?0:n;}
 size_t print(''')
(work/'SD.h').write_text(sd,'utf-8')
(work/'HardwareSerial.h').write_text('''#pragma once
#include "Arduino.h"
class HardwareSerial {
public:
 std::deque<uint8_t> rx;std::vector<uint8_t> tx,registers;bool corrupt=false,respond=true;int writes=0;
 HardwareSerial(int){}
 void setRxBufferSize(int){}void setTxBufferSize(int){}void begin(int,int,int,int){}void end(){}
 int available(){return rx.size();}int read(){int c=rx.front();rx.pop_front();return c;}int availableForWrite(){return 512;}
 size_t write(const uint8_t *p,size_t n){writes++;tx.assign(p,p+n);
  if(respond&&n==11&&p[0]==0xc0){registers.assign(p+3,p+11);rx.push_back(0xc1);rx.push_back(0);rx.push_back(8);for(auto x:registers)rx.push_back(x);}
  if(respond&&n==3&&p[0]==0xc1){rx.push_back(0xc1);rx.push_back(0);rx.push_back(8);for(auto x:registers)rx.push_back(corrupt?0:x);}
  return n;
 }
};
''','utf-8')
harness='''
#include <cassert>
#include <iostream>
#include <filesystem>
#include "SD.h"
MockSD SD;
uint32_t mockMillis=0,adcBase=1900,pwmDuty=0;
#define BOARD_CYD_28 1
#include "field_hardware.cpp"
int main(int argc,char**argv){
 SD.root=argv[1];std::filesystem::create_directories(SD.root);
 assert(FieldLogic::crc32((const uint8_t*)"123456789",9)==0xcbf43926);
 assert(FieldLogic::utf8Text("น้ำ",strlen("น้ำ")));
 assert(!FieldLogic::utf8Text("\\xc0\\xaf",2));
 assert(!FieldLogic::utf8Text("\\xed\\xa0\\x80",3));
 assert(!FieldLogic::utf8Text("\\xf4\\x90\\x80\\x80",4));
 FieldLogic::BatteryFilter f;assert(!f.update(NAN,0));assert(!f.update(5,0));assert(f.update(3.8f,0));
 for(int i=1;i<2000;i++)assert(f.update(3.8f+(i%2?0.004f:-0.004f),i*250));
 assert(f.shown==45);int previous=f.shown;
 for(int i=2000;i<2100;i++){f.update(3.4f,i*250);assert(previous-f.shown<=1);previous=f.shown;}
 assert(f.shown<45&&f.shown>0);
 assert(FieldLogic::batteryPercent(3.2f)==0&&FieldLogic::batteryPercent(4.2f)==100);
 fieldHardwareBegin(true);assert(fieldConfig().soundEnabled&&!fieldConfig().loraEnabled);
 assert(!fieldLoRaSend("hello")&&radio.writes==0);
 FieldConfig c=fieldConfig();c.batteryEnabled=true;c.loraEnabled=true;assert(fieldSaveConfig(c));
 fieldHardwareLoop(0);assert(fieldBattery().valid&&fieldBattery().percent>=44&&fieldBattery().percent<=46);
 for(int i=1;i<100;i++){mockMillis=i*250;fieldHardwareLoop(mockMillis);}
 assert(fieldBattery().percent==45);assert(radio.writes==0);
 assert(fieldLoRaState()==FieldLoRaState::NeedsConfiguration&&!fieldLoRaSend("hello"));
 mockMillis+=1000;assert(fieldLoRaConfigure());fieldHardwareLoop(mockMillis);
 mockMillis+=100;fieldHardwareLoop(mockMillis);
 assert(fieldLoRaState()==FieldLoRaState::SwitchToNormal);
 assert(!fieldLoRaSend("hello"));assert(fieldLoRaNormalMode());
 mockMillis+=1000;fieldHardwareLoop(mockMillis);assert(fieldLoRaState()==FieldLoRaState::Ready);
 assert(fieldLoRaSend("สวัสดี"));auto frame=radio.tx;
 assert(!fieldLoRaSend("duplicate too soon"));
 FieldRadioMessage items[8];uint32_t latest;assert(fieldLoRaSnapshot(items,8,&latest)==1&&items[0].mine);
 // Rewrite sender, repair CRC, then feed the actual bounded parser.
 std::string incoming(frame.begin(),frame.end());incoming.replace(5,8,"76543210");
 size_t end=incoming.rfind('|');char crc[9];snprintf(crc,sizeof(crc),"%08lx",(unsigned long)FieldLogic::crc32((const uint8_t*)incoming.data(),end));incoming.replace(end+1,8,crc);
 for(char v:incoming)radio.rx.push_back(v);fieldHardwareLoop(++mockMillis);
 assert(fieldLoRaSnapshot(items,8,&latest)==2&&!items[1].mine&&String(items[1].text)=="สวัสดี");assert(fieldLoRaTakeNew());
 for(char v:incoming)radio.rx.push_back(v);fieldHardwareLoop(++mockMillis);assert(fieldLoRaSnapshot(items,8,&latest)==2);
 incoming[23]='f';for(char v:incoming)radio.rx.push_back(v);fieldHardwareLoop(++mockMillis);assert(fieldLoRaSnapshot(items,8,&latest)==2);
 fieldAlert(FieldAlert::Test);fieldHardwareLoop(mockMillis);assert(pwmDuty==64);fieldHardwareLoop(mockMillis+80);assert(pwmDuty==0);
 FieldConfig old=fieldConfig();SD.failWrites=true;c.batteryEnabled=false;assert(!fieldSaveConfig(c));assert(fieldConfig().batteryEnabled==old.batteryEnabled);SD.failWrites=false;
 // Independent C1 mismatch cannot reach ready; a retry in the wrong mode times out.
 mockMillis+=30000;radio.corrupt=true;assert(fieldLoRaConfigure());fieldHardwareLoop(mockMillis);mockMillis+=100;fieldHardwareLoop(mockMillis);assert(fieldLoRaState()==FieldLoRaState::Error&&!fieldLoRaSend("bad profile"));
 radio.corrupt=false;radio.respond=false;assert(fieldLoRaConfigure());mockMillis+=2000;fieldHardwareLoop(mockMillis);assert(fieldLoRaState()==FieldLoRaState::Error);
 c=fieldConfig();c.soundEnabled=false;c.loraEnabled=false;assert(fieldSaveConfig(c));assert(fieldLoRaState()==FieldLoRaState::Off);fieldAlert(FieldAlert::Test);fieldHardwareLoop(++mockMillis);assert(pwmDuty==0);
 std::cout<<"PASS actual hardware service: filtered noisy ADC, percentage hysteresis, CRC/UTF8, atomic settings failure, E220 write/readback gate, UART timeout, rate limit, dedup, speaker PWM lifecycle\\n";
}
'''
(work/'test.cpp').write_text(harness,'utf-8')
zig=ROOT/'.thai-work/native-compiler/ziglang/zig.exe'
binary=work/'test.exe'
subprocess.run([str(zig),'c++','-target','x86_64-windows-gnu','-std=c++17','-O0','-I'+str(work),'-I'+str(ROOT/'firmware/src'),str(work/'test.cpp'),'-o',str(binary)],check=True)
result=subprocess.run([str(binary),str(work/'sd')],check=True,capture_output=True,text=True,encoding='utf-8')
report={'result':'PASS','implementation':'Actual field_hardware.cpp with mocked ADC, PWM, SD and E220 UART','details':result.stdout.strip(),'physicalHardwareTested':False,'fixturePath':str(work)}
(ROOT/'docs/thai-field-hardware-test-report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n','utf-8')
print(result.stdout.strip())
