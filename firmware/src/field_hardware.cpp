#include "field_hardware.h"
#include "field_hardware_logic.h"
#include "config.h"
#include <SD.h>
#include <HardwareSerial.h>

static constexpr int BATTERY_PIN=35, AUDIO_PIN=26, AUDIO_CHANNEL=7;
static constexpr int RADIO_RX=17, RADIO_TX=22;
static constexpr uint32_t TX_INTERVAL=30000;
static FieldConfig settings;
static bool mounted=false, radioStarted=false;
static FieldLogic::BatteryFilter filter;
static FieldBattery battery={false,false,0,-1,0};
static portMUX_TYPE stateMux=portMUX_INITIALIZER_UNLOCKED;
static HardwareSerial radio(1);
static FieldLoRaState radioState=FieldLoRaState::Off;
static uint32_t radioBoot=0,lastTx=0,seq=0;
static bool hasTx=false,newRadio=false;
static FieldRadioMessage messages[8]={};
static int messageCount=0;
static char rxLine[384]={};
static unsigned rxLength=0;
static bool rxDiscard=false;
static uint64_t seen[16]={};
static unsigned seenAt=0;
static uint32_t senderId=0;
static uint32_t nextBattery=0,batteryInvalidSince=0;
static bool lowAlerted=false;
static uint32_t audioDeadline=0,audioNext=0;
static uint8_t audioRemaining=0;
static bool audioOn=false;
// E220-900T22D manual v1.2, registers 00H..07H. All peers use address
// 0000, 9600 8N1 / 2.4kbps, 200-byte packet,13dBm,CH72,transparent+LBT.
static const uint8_t profile[]={0x00,0x00,0x62,0x02,0x48,0x10,0x00,0x00};
static uint8_t configReply[11]={};
static unsigned replyLength=0;
static uint8_t configStep=0;
static uint32_t configDeadline=0,readAt=0;

FieldConfig fieldConfig(){portENTER_CRITICAL(&stateMux);FieldConfig c=settings;portEXIT_CRITICAL(&stateMux);return c;}
FieldBattery fieldBattery(){portENTER_CRITICAL(&stateMux);FieldBattery b=battery;portEXIT_CRITICAL(&stateMux);return b;}
FieldLoRaState fieldLoRaState(){return radioState;}

static bool supported() {
#if defined(BOARD_CYD_28)
  return true;
#else
  return false;
#endif
}
static void stopAudio() {
  if(supported())ledcWrite(AUDIO_CHANNEL,0);
  audioOn=false;audioRemaining=0;
}
static void applySettings(const FieldConfig &c) {
  FieldConfig old=fieldConfig();
  portENTER_CRITICAL(&stateMux);settings=c;portEXIT_CRITICAL(&stateMux);
  if(old.batteryEnabled!=c.batteryEnabled||old.batteryCalibration!=c.batteryCalibration){
    filter.reset();lowAlerted=false;nextBattery=0;
    portENTER_CRITICAL(&stateMux);battery={c.batteryEnabled,false,0,-1,battery.revision+1};portEXIT_CRITICAL(&stateMux);
  }
  if(!c.soundEnabled)stopAudio();
  if(c.loraEnabled&&!radioStarted&&supported()) {
    // GPS UART2 is RX-only. IO22 is exclusively UART1 TX in this mode.
    radio.setRxBufferSize(1024);
    radio.setTxBufferSize(512);
    radio.begin(9600,SERIAL_8N1,RADIO_RX,RADIO_TX);
    radioStarted=true;radioBoot=millis();radioState=FieldLoRaState::NeedsConfiguration;
    rxLength=0;configStep=0;hasTx=false;
  } else if(!c.loraEnabled&&radioStarted) {
    radio.end();radioStarted=false;radioState=FieldLoRaState::Off;configStep=0;
    pinMode(RADIO_RX,INPUT); // External module TX may remain connected while disabled
    pinMode(RADIO_TX,INPUT);
  }
}
static bool parseConfig(const char *path,FieldConfig &out) {
  File f=SD.open(path,FILE_READ);
  if(!f||f.isDirectory()||f.size()>256){if(f)f.close();return false;}
  String s=f.readString();f.close();
  int b,a,l;float cal;int end=0;
  if(sscanf(s.c_str(),"version=1\nbattery=%d\nsound=%d\nlora=%d\ncalibration=%f\n%n",&b,&a,&l,&cal,&end)!=4)return false;
  if(end!=(int)s.length()||(b!=0&&b!=1)||(a!=0&&a!=1)||(l!=0&&l!=1)||!isfinite(cal)||cal<0.8f||cal>1.2f)return false;
  out.batteryEnabled=b!=0;out.soundEnabled=a!=0;out.loraEnabled=l!=0;out.batteryCalibration=cal;return true;
}
bool fieldSaveConfig(const FieldConfig &c) {
  if(!supported()||!mounted||!isfinite(c.batteryCalibration)||c.batteryCalibration<0.8f||c.batteryCalibration>1.2f)return false;
  const char *path="/field-config.txt",*tmp="/field-config.tmp",*bak="/field-config.bak";
  if(SD.exists(tmp)&&!SD.remove(tmp))return false;
  char text[128];int n=snprintf(text,sizeof(text),"version=1\nbattery=%d\nsound=%d\nlora=%d\ncalibration=%.6f\n",c.batteryEnabled,c.soundEnabled,c.loraEnabled,c.batteryCalibration);
  File f=SD.open(tmp,FILE_WRITE);if(!f)return false;
  bool ok=f.write((const uint8_t*)text,n)==(size_t)n;f.close();
  FieldConfig verified;if(!ok||!parseConfig(tmp,verified)){SD.remove(tmp);return false;}
  bool existed=SD.exists(path);
  if(existed){if(SD.exists(bak)&&!SD.remove(bak))return false;if(!SD.rename(path,bak))return false;}
  if(!SD.rename(tmp,path)){if(existed)SD.rename(bak,path);return false;}
  if(SD.exists(bak))SD.remove(bak);
  applySettings(verified);return true;
}
void fieldHardwareBegin(bool sdMounted) {
  mounted=sdMounted;
  if(supported()) {
    pinMode(BATTERY_PIN,INPUT);
    analogSetPinAttenuation(BATTERY_PIN,ADC_11db);
    ledcSetup(AUDIO_CHANNEL,2000,8);ledcAttachPin(AUDIO_PIN,AUDIO_CHANNEL);ledcWrite(AUDIO_CHANNEL,0);
  }
  senderId=(uint32_t)(ESP.getEfuseMac()^(ESP.getEfuseMac()>>32));
  FieldConfig c;
  if(mounted&&!parseConfig("/field-config.txt",c))parseConfig("/field-config.bak",c);
  applySettings(c);
}
void fieldAlert(FieldAlert alert) {
  if(!supported()||!fieldConfig().soundEnabled)return;
  // Dedicated LEDC channel: hardware produces the waveform while the main
  // loop services GPS, touch and SD. No busy delay or per-sample DAC loop.
  audioRemaining=alert==FieldAlert::Sunset?6:alert==FieldAlert::LowBattery?3:alert==FieldAlert::Message?1:2;
  audioOn=false;audioNext=millis();audioDeadline=0;
}
static void audioLoop(uint32_t now) {
  if(audioOn&&(int32_t)(now-audioDeadline)>=0){ledcWrite(AUDIO_CHANNEL,0);audioOn=false;audioNext=now+110;}
  if(!audioOn&&audioRemaining&&(int32_t)(now-audioNext)>=0){
    ledcWriteTone(AUDIO_CHANNEL,1800);ledcWrite(AUDIO_CHANNEL,64);
    audioOn=true;audioDeadline=now+80;audioRemaining--;
  }
}
static void batteryLoop(uint32_t now) {
  FieldConfig c=fieldConfig();
  if(!supported()||!c.batteryEnabled||(int32_t)(now-nextBattery)<0)return;
  nextBattery=now+250;
  uint32_t samples[9];
  for(int i=0;i<9;i++)samples[i]=analogReadMilliVolts(BATTERY_PIN);
  for(int i=1;i<9;i++){uint32_t v=samples[i];int j=i-1;while(j>=0&&samples[j]>v){samples[j+1]=samples[j];j--;}samples[j+1]=v;}
  float v=samples[4]*0.002f*c.batteryCalibration;
  bool valid=filter.update(v,now);
  if(!valid){
    if(!batteryInvalidSince)batteryInvalidSince=now|1;
    if(now-batteryInvalidSince<3000)return;
  }else batteryInvalidSince=0;
  FieldBattery next={true,valid,valid?filter.voltage:0,valid?filter.shown:-1,0};
  portENTER_CRITICAL(&stateMux);
  next.revision=battery.revision+(next.valid!=battery.valid||next.percent!=battery.percent);
  battery=next;portEXIT_CRITICAL(&stateMux);
  if(valid&&filter.shown<=10&&!lowAlerted){lowAlerted=true;fieldAlert(FieldAlert::LowBattery);}
  if(valid&&filter.shown>=20)lowAlerted=false;
}
static bool sameReply() {
  return replyLength==sizeof(configReply)&&configReply[0]==0xc1&&configReply[1]==0&&configReply[2]==sizeof(profile)&&!memcmp(configReply+3,profile,sizeof(profile));
}
bool fieldLoRaConfigure() {
  if(!radioStarted||radioState==FieldLoRaState::Configuring||millis()-radioBoot<1000)return false;
  // User has physically selected M0=M1=HIGH. A command is not sent on boot.
  // Verify the persistent profile with an independent C1 read after write ACK.
  while(radio.available())radio.read();
  uint8_t command[11]={0xc0,0x00,sizeof(profile)};
  memcpy(command+3,profile,sizeof(profile));
  radioState=FieldLoRaState::Configuring;configStep=1;replyLength=0;
  configDeadline=millis()+1800;
  radio.write(command,sizeof(command));return true;
}
bool fieldLoRaNormalMode() {
  if(radioState!=FieldLoRaState::SwitchToNormal)return false;
  while(radio.available())radio.read();
  configStep=4;configDeadline=millis()+1000;
  radioState=FieldLoRaState::Configuring;return true;
}
String fieldLoRaStateText() {
  switch(radioState){
    case FieldLoRaState::Off:return "LoRa ปิด";
    case FieldLoRaState::NeedsConfiguration:return "ยก M0/M1 แล้วตั้งค่า";
    case FieldLoRaState::Configuring:return "กำลังตรวจโมดูล";
    case FieldLoRaState::SwitchToNormal:return "ต่อ M0/M1 ลง GND แล้วใช้งาน";
    case FieldLoRaState::Ready:return "922.125 MHz พร้อม";
    default:return "ตั้งค่าไม่สำเร็จ ตรวจสาย";
  }
}
static bool readHex32(const char *p,uint32_t &out) {
  out=0;for(int i=0;i<8;i++){int v=FieldLogic::unhex(p[i]);if(v<0)return false;out=(out<<4)|(uint32_t)v;}return true;
}
static void addMessage(uint32_t sender,const char *text,bool mine,uint32_t now) {
  FieldRadioMessage message={};snprintf(message.from,sizeof(message.from),"%08lx",(unsigned long)sender);
  strlcpy(message.text,text,sizeof(message.text));message.mine=mine;message.ms=now;
  portENTER_CRITICAL(&stateMux);
  message.seq=++seq;
  if(messageCount==8){memmove(messages,messages+1,7*sizeof(FieldRadioMessage));messageCount--;}
  messages[messageCount++]=message;
  if(!mine)newRadio=true;
  portEXIT_CRITICAL(&stateMux);
  if(!mine)fieldAlert(FieldAlert::Message);
}
static void receiveLine(uint32_t now) {
  // SWL1|sender8|id8|UTF8-as-hex|crc8. CRC protects accidental corruption;
  // it provides no authentication or confidentiality.
  if(rxLength<33||strncmp(rxLine,"SWL1|",5)||rxLine[13]!='|'||rxLine[22]!='|')return;
  char *last=strrchr(rxLine,'|');if(!last||strlen(last+1)!=8)return;
  uint32_t sender,id,crc;
  if(!readHex32(rxLine+5,sender)||!readHex32(rxLine+14,id)||!readHex32(last+1,crc))return;
  if(FieldLogic::crc32((const uint8_t*)rxLine,last-rxLine)!=crc)return;
  int bytes=(last-(rxLine+23))/2;
  if(bytes<1||bytes>160||(last-(rxLine+23))%2)return;
  char text[161];for(int i=0;i<bytes;i++){int a=FieldLogic::unhex(rxLine[23+i*2]),b=FieldLogic::unhex(rxLine[24+i*2]);if(a<0||b<0)return;text[i]=(char)((a<<4)|b);}text[bytes]=0;
  if(!FieldLogic::utf8Text(text,bytes))return;
  uint64_t key=((uint64_t)sender<<32)|id;
  for(uint64_t old:seen)if(old==key)return;
  seen[seenAt++%16]=key;
  if(sender!=senderId)addMessage(sender,text,false,now);
}
bool fieldLoRaSend(const String &text) {
  uint32_t now=millis();
  if(radioState!=FieldLoRaState::Ready||!text.length()||text.length()>160||!FieldLogic::utf8Text(text.c_str(),text.length()))return false;
  if(hasTx&&now-lastTx<TX_INTERVAL)return false;
  char frame[384];uint32_t id=esp_random();
  int n=snprintf(frame,sizeof(frame),"SWL1|%08lx|%08lx|",(unsigned long)senderId,(unsigned long)id);
  static const char hex[]="0123456789abcdef";
  for(size_t i=0;i<text.length();i++){uint8_t b=text[i];frame[n++]=hex[b>>4];frame[n++]=hex[b&15];}
  uint32_t crc=FieldLogic::crc32((const uint8_t*)frame,n);
  n+=snprintf(frame+n,sizeof(frame)-n,"|%08lx\n",(unsigned long)crc);
  if(radio.availableForWrite()<n)return false;
  if(radio.write((const uint8_t*)frame,n)!=(size_t)n)return false;
  hasTx=true;lastTx=now;addMessage(senderId,text.c_str(),true,now);return true;
}
static void radioLoop(uint32_t now) {
  if(!radioStarted)return;
  if(radioState==FieldLoRaState::Configuring){
    if(configStep==4){if((int32_t)(now-configDeadline)>=0){radioState=FieldLoRaState::Ready;configStep=0;rxLength=0;}return;}
    if(configStep==2&&(int32_t)(now-readAt)>=0){
      const uint8_t command[]={0xc1,0x00,sizeof(profile)};replyLength=0;radio.write(command,sizeof(command));configStep=3;configDeadline=now+1800;
    }
    while(radio.available()){
      int c=radio.read();
      if(replyLength<sizeof(configReply))configReply[replyLength++]=(uint8_t)c;
      else{radioState=FieldLoRaState::Error;configStep=0;return;}
    }
    if(replyLength==sizeof(configReply)){
      if(!sameReply()){radioState=FieldLoRaState::Error;configStep=0;return;}
      if(configStep==1){configStep=2;replyLength=0;readAt=now+100;}
      else if(configStep==3){configStep=0;radioState=FieldLoRaState::SwitchToNormal;}
    }
    if(configStep&&(int32_t)(now-configDeadline)>=0){radioState=FieldLoRaState::Error;configStep=0;}
    return;
  }
  unsigned work=0;
  while(radio.available()&&work++<512){
    char c=radio.read();
    if(c=='\n'){
      if(!rxDiscard&&rxLength){rxLine[rxLength]=0;receiveLine(now);}
      rxLength=0;rxDiscard=false;
    } else if(c=='\r')continue;
    else if((unsigned char)c>=32&&(unsigned char)c<127&&rxLength<sizeof(rxLine)-1&&!rxDiscard)rxLine[rxLength++]=c;
    else rxDiscard=true;
  }
}
int fieldLoRaSnapshot(FieldRadioMessage *items,int capacity,uint32_t *latest) {
  if(!items||capacity<=0)return 0;
  portENTER_CRITICAL(&stateMux);
  int n=min(messageCount,capacity);
  memcpy(items,messages+messageCount-n,n*sizeof(FieldRadioMessage));if(latest)*latest=seq;
  portEXIT_CRITICAL(&stateMux);return n;
}
bool fieldLoRaTakeNew(){portENTER_CRITICAL(&stateMux);bool yes=newRadio;newRadio=false;portEXIT_CRITICAL(&stateMux);return yes;}
void fieldHardwareLoop(uint32_t now){audioLoop(now);batteryLoop(now);radioLoop(now);}
String fieldHardwareStatusJson(){
  FieldBattery b=fieldBattery();FieldConfig c=fieldConfig();
  String j="{\"batteryEnabled\":"+String(c.batteryEnabled?"true":"false")+",\"batteryValid\":"+String(b.valid?"true":"false");
  j+=",\"volts\":"+String(b.volts,3)+",\"percent\":"+String(b.percent)+",\"sound\":"+String(c.soundEnabled?"true":"false");
  j+=",\"loraEnabled\":"+String(c.loraEnabled?"true":"false")+",\"loraReady\":"+String(radioState==FieldLoRaState::Ready?"true":"false")+"}";return j;
}
