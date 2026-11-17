
from pathlib import Path
import subprocess
r=Path('.').resolve();s=(r/'firmware/src/main.cpp').read_text('utf-8')
def function(name):
 start=s.index('static void '+name+'(');brace=s.index('{',start);depth=1;i=brace+1
 while depth:
  if s[i]=='{':depth+=1
  if s[i]=='}':depth-=1
  i+=1
 return s[start:i]
code=r'''
#include "Arduino.h"
#include <vector>
#include <cassert>
#include <iostream>
enum {I_NONE, TL_DATUM};
const int SCR_W=240,HEADER_H=36,STATUS_H=14,CONTENT_Y=50,C_BG=0,C_HEADER=1,C_TEXT=2,C_ACCENT=3,TFT_RED=4;
int NC(int x){return x;}
struct TFT_eSPI {
 int screens=0,rects=0,texts=0;
 void fillScreen(int){screens++;}
 void fillRect(int,int,int,int,int){rects++;}
 void drawRect(int,int,int,int,int){}
 void setTextFont(int){}
 void setTextColor(int,int=0){}
 void setTextDatum(int){}
 void drawString(const String&,int,int){texts++;}
 int lineHeight(){return 24;}
};
TFT_eSPI tft;
struct Btn {int x,y,w,h;uint16_t id;String label;uint8_t icon;bool enabled;};
std::vector<Btn> btns;
bool fieldRefreshOnly=false,lastRecordingIndicator=false,recording=false;
uint32_t lastBatteryRevision=UINT32_MAX;
String fieldLineText[8];uint16_t fieldLineColor[8]={};
struct FieldBattery {bool valid=false;int percent=0;uint32_t revision=1;} battery;
FieldBattery fieldBattery(){return battery;}
bool fieldTrackRecording(){return recording;}
String fitText(const String &s,int){return s;}
int buttonDraws=0,headerDraws=0;
void drawButton(const Btn&,bool){buttonDraws++;}
void drawHeader(const String&,bool){headerDraws++;}
'''+ chr(10).join(function(n) for n in ['addBtn','drawBatteryStatus','fieldFrame','fieldLine'])+r'''
int main(){
 fieldFrame("ready");fieldLine("GPS waiting",0);addBtn(0,0,38,36,1,"");
 addBtn(0,284,80,34,1,"back");drawBatteryStatus();
 int rects=tft.rects,texts=tft.texts,buttons=buttonDraws;
 fieldRefreshOnly=true;
 for(int i=0;i<120;i++){
  fieldFrame("ready");fieldLine("GPS waiting",0);
  addBtn(0,284,80,34,1,"back");drawBatteryStatus(false);
 }
 assert(tft.screens==1&&headerDraws==1&&tft.rects==rects&&tft.texts==texts&&buttonDraws==buttons);
 fieldLine("GPS ready",0);assert(tft.rects==rects+1&&tft.texts==texts+1);
 fieldLine("",0);assert(tft.rects==rects+2);
 addBtn(0,284,80,34,1,"next");
 assert(buttonDraws==buttons+1&&btns.size()==2&&btns[0].label==""&&btns[1].label=="next");
 rects=tft.rects;recording=true;drawBatteryStatus(false);assert(tft.rects>rects);
 rects=tft.rects;drawBatteryStatus(false);assert(tft.rects==rects);
 battery.valid=true;battery.percent=42;battery.revision++;drawBatteryStatus(false);assert(tft.rects>rects);
 fieldRefreshOnly=false;fieldFrame("next screen");assert(tft.screens==2);
 std::cout<<"PASS unchanged120 refreshes draw nothing; changed rows/buttons/REC/battery update; new entry clears once\n";
}
'''
out=r/'.thai-work/field-refresh-native';out.mkdir(exist_ok=True)
(out/'test.cpp').write_text(code,encoding='utf-8')
subprocess.run([str(r/'.thai-work/native-compiler/ziglang/zig.exe'),'c++','-target','x86_64-windows-gnu','-std=c++17','-I'+str(r/'scripts/tests/map_native'),str(out/'test.cpp'),'-o',str(out/'test.exe')],check=True)
subprocess.run([str(out/'test.exe')],check=True)
