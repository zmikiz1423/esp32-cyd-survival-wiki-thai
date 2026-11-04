#include "offline_maps.h"
#include "gps.h"
#include <SD.h>
#include <math.h>

static portMUX_TYPE navMux = portMUX_INITIALIZER_UNLOCKED;
static OfflineDestination destination = {};
static volatile uint32_t selectionSeq = 1;
static uint32_t loadedSeq = 0;
static OfflineRoutePoint routePts[600];
static int routeN = 0;
static bool routeTruncated = false;
static char activeName[100] = "";

static String esc(const String &s) {
  String o; for (size_t i=0;i<s.length();i++) { uint8_t c=s[i]; if(c=='"'||c=='\\')o+='\\'; if(c>=32)o+=(char)c; } return o;
}
static String boundedLine(File &f, unsigned max=512) {
  String s; s.reserve(min<unsigned>(max,128)); bool longLine=false;
  while(f.available()){char c=f.read();if(c=='\n')break;if(c=='\r')continue;if(s.length()<max)s+=c;else longLine=true;}
  return longLine ? String() : s;
}
static bool coordinate(const String &s,double &n,double max){char *end=nullptr;n=strtod(s.c_str(),&end);return end!=s.c_str()&&!*end&&isfinite(n)&&fabs(n)<=max;}
OfflineDestination offlineDestination(){portENTER_CRITICAL(&navMux);OfflineDestination d=destination;portEXIT_CRITICAL(&navMux);return d;}
bool offlineSetDestination(double lat,double lon,const String &name){
  if(!isfinite(lat)||!isfinite(lon)||fabs(lat)>90||fabs(lon)>180)return false;
  OfflineDestination d={};d.valid=true;d.lat=lat;d.lon=lon;
  size_t n=min<size_t>(name.length(),sizeof(d.name)-1);while(n&&((uint8_t)name[n]&0xc0)==0x80)n--;
  memcpy(d.name,name.c_str(),n);d.name[n]=0;
  portENTER_CRITICAL(&navMux);d.seq=destination.seq+1;destination=d;portEXIT_CRITICAL(&navMux);return true;
}
String offlineStatusJson(){
  GpsFix g=gpsGet();OfflineDestination d=offlineDestination();
  String j="{\"valid\":"+String(g.valid?"true":"false")+",\"lat\":"+String(g.lat,6)+",\"lon\":"+String(g.lon,6);
  j+=",\"speed\":"+String(g.speedKmh,1)+",\"course\":"+String(g.courseDeg,1)+",\"altitude\":"+String(g.altM,1)+",\"hdop\":"+String(g.hdop,1)+",\"satellites\":"+String(g.sats)+",\"phone\":"+String(g.phone?"true":"false")+",\"age\":"+String(gpsFixAgeS());
  j+=",\"destination\":";if(d.valid)j+="{\"name\":\""+esc(d.name)+"\",\"lat\":"+String(d.lat,6)+",\"lon\":"+String(d.lon,6)+"}";else j+="null";
  return j+"}";
}
String offlinePoisJson(const String &input,uint32_t offset){
  String query=input.substring(0,160);query.toLowerCase();query.trim();
  File f=SD.open("/maps/pois.tsv",FILE_READ);if(!f||f.isDirectory())return "{\"items\":[],\"next\":null,\"missing\":true}";
  if(offset>f.size()){f.close();return "{\"items\":[],\"next\":null}";}f.seek(offset);
  String j="{\"items\":[";int found=0,scanned=0;uint32_t started=millis();
  // Each page scans at most 1500 lines / 500 ms; the phone explicitly offers
  // the next page. This avoids loading or scanning a province in one request.
  while(f.available()&&found<16&&scanned++<1500&&millis()-started<500){
    String s=boundedLine(f);int a=s.indexOf('\t'),b=s.indexOf('\t',a+1),c=s.indexOf('\t',b+1),e=s.indexOf('\t',c+1);
    if(a<=0||b<=a||c<=b||e<=c)continue;
    String name=s.substring(a+1,b),cat=s.substring(e+1),match=name+" "+cat;match.toLowerCase();if(query.length()&&match.indexOf(query)<0)continue;
    double lat,lon;if(!coordinate(s.substring(b+1,c),lat,90)||!coordinate(s.substring(c+1,e),lon,180))continue;
    if(found++)j+=',';j+="{\"name\":\""+esc(name)+"\",\"lat\":"+String(lat,6)+",\"lon\":"+String(lon,6)+",\"category\":\""+esc(cat)+"\"}";
  }
  j+="],\"next\":";j+=f.available()?String(f.position()):String("null");f.close();return j+"}";
}
int offlineFindPois(const String &input,uint32_t offset,OfflinePoi *items,int capacity,uint32_t &next){
  next=UINT32_MAX;String query=input.substring(0,160);query.trim();query.toLowerCase();
  File f=SD.open("/maps/pois.tsv",FILE_READ);if(!f||f.isDirectory())return 0;
  if(offset>f.size()){f.close();return 0;}f.seek(offset);
  int count=0,scanned=0;uint32_t started=millis();
  while(f.available()&&count<capacity&&scanned++<1500&&millis()-started<500){
    String s=boundedLine(f);int a=s.indexOf('\t'),b=s.indexOf('\t',a+1),c=s.indexOf('\t',b+1),e=s.indexOf('\t',c+1);
    if(a<=0||b<=a||c<=b||e<=c)continue;
    String name=s.substring(a+1,b),category=s.substring(e+1),match=name+" "+category;match.toLowerCase();
    if(query.length()&&match.indexOf(query)<0)continue;
    double lat,lon;if(!coordinate(s.substring(b+1,c),lat,90)||!coordinate(s.substring(c+1,e),lon,180))continue;
    items[count++]={lat,lon,name,category};
  }
  if(f.available())next=f.position();f.close();return count;
}
static bool safeRoute(const String &name){return name.length()>4&&name.length()<96&&name.endsWith(".gpx")&&name.indexOf('/')<0&&name.indexOf('\\')<0&&name.indexOf("..")<0&&name.indexOf('\n')<0&&name.indexOf('\r')<0;}
static String selected(){File f=SD.open("/maps/active-route.txt",FILE_READ);String n=f?boundedLine(f,96):String();if(f)f.close();n.trim();return safeRoute(n)?n:String();}
String offlineRoutesJson(){
  String j="{\"selected\":\""+esc(selected())+"\",\"items\":[";File dir=SD.open("/maps/routes");int n=0;
  if(dir&&dir.isDirectory()){for(File f=dir.openNextFile();f&&n<40;f=dir.openNextFile()){String name=f.name();int slash=name.lastIndexOf('/');if(slash>=0)name=name.substring(slash+1);if(!f.isDirectory()&&safeRoute(name)&&f.size()<=4194304){if(n++)j+=',';j+='"';j+=esc(name);j+='"';}f.close();}dir.close();}
  return j+"]}";
}
bool offlineSelectRoute(const String &name){
  if(name.length()&&!safeRoute(name))return false;
  if(name.length()){File f=SD.open("/maps/routes/"+name);bool ok=f&&!f.isDirectory()&&f.size()<=4194304;if(f)f.close();if(!ok)return false;}
  File f=SD.open("/maps/active-route.txt",FILE_WRITE);if(!f)return false;f.print(name);f.close();
  portENTER_CRITICAL(&navMux);selectionSeq++;portEXIT_CRITICAL(&navMux);return true;
}
static bool attr(const String &s,const char *key,double &n,double max){
  int p=s.indexOf(String(key));if(p<0||(p&& !isspace((unsigned char)s[p-1])))return false;p+=strlen(key);
  while(p<(int)s.length()&&isspace((unsigned char)s[p]))p++;
  if(p>=(int)s.length()||s[p++]!='=')return false;
  while(p<(int)s.length()&&isspace((unsigned char)s[p]))p++;
  if(p>=(int)s.length()||(s[p]!='\''&&s[p]!='"'))return false;char q=s[p++];int end=s.indexOf(q,p);return end>p&&coordinate(s.substring(p,end),n,max);
}
// Bounded XML token reader: no entities, external resources, or unbounded DOM.
static bool nextPoint(File &f,OfflineRoutePoint &pt,bool &segment){
  unsigned work=0;
  while(f.available()){if((++work&1023)==0)yield();if(f.read()!='<')continue;String tag;tag.reserve(96);bool tooLong=false;while(f.available()){if((++work&1023)==0)yield();char c=f.read();if(c=='>')break;if(tag.length()<256)tag+=c;else tooLong=true;}if(tooLong)continue;
    tag.replace('\n',' ');tag.replace('\r',' ');tag.replace('\t',' ');
    if(tag=="trkseg"||tag.startsWith("trkseg ")||tag=="rte"||tag.startsWith("rte "))segment=true;
    bool point=tag.startsWith("trkpt ")||tag.startsWith("rtept ");double lat,lon;
    if(point&&attr(tag,"lat",lat,90)&&attr(tag,"lon",lon,180)){pt={(float)lat,(float)lon,segment};segment=false;return true;}
  }return false;
}
bool offlineRouteRefresh(){
  uint32_t seq;portENTER_CRITICAL(&navMux);seq=selectionSeq;portEXIT_CRITICAL(&navMux);if(seq==loadedSeq)return false;loadedSeq=seq;routeN=0;routeTruncated=false;
  String name=selected();strlcpy(activeName,name.c_str(),sizeof(activeName));if(!name.length())return true;
  File f=SD.open("/maps/routes/"+name);if(!f||f.size()>4194304){if(f)f.close();return true;}
  OfflineRoutePoint p,last={};bool seg=true;uint32_t total=0;while(nextPoint(f,p,seg)){total++;if((total&255)==0){gpsLoop();yield();}}
  f.seek(0);seg=true;uint32_t step=total<=600?1:max<uint32_t>(1,(total+597)/598),i=0,limit=total<=600?600:599;bool pendingBreak=false;
  while(nextPoint(f,p,seg)){pendingBreak|=p.start;last=p;if((i++%step)==0&&routeN<(int)limit){p.start=pendingBreak;routePts[routeN++]=p;pendingBreak=false;}if((i&255)==0){gpsLoop();yield();}}
  if(total&&routeN<600&&(routeN==0||last.lat!=routePts[routeN-1].lat||last.lon!=routePts[routeN-1].lon)){last.start=pendingBreak;routePts[routeN++]=last;}
  routeTruncated=total>600;f.close();return true;
}
const OfflineRoutePoint *offlineRoute(int &count){count=routeN;return routePts;}
String offlineRouteJson(){
  // Read the selected file independently so HTTPS never races main-task buffers.
  String name=selected();File f=name.length()?SD.open("/maps/routes/"+name):File();String j="{\"name\":\""+esc(name)+"\",\"points\":[";
  if(!f||f.size()>4194304){if(f)f.close();return j+"]}";}
  OfflineRoutePoint p,last={},saved={};bool seg=true;uint32_t n=0;while(nextPoint(f,p,seg)){n++;if((n&255)==0)yield();}f.seek(0);seg=true;uint32_t step=n<=300?1:max<uint32_t>(1,(n+297)/298),i=0,kept=0,limit=n<=300?300:299;bool pending=false;
  while(nextPoint(f,p,seg)){pending|=p.start;last=p;if((i++%step)==0&&kept<limit){if(kept++)j+=',';j+="["+String(p.lat,6)+","+String(p.lon,6)+","+String(pending?1:0)+"]";pending=false;saved=p;}if((i&255)==0)yield();}
  if(n&&kept<300&&(kept==0||last.lat!=saved.lat||last.lon!=saved.lon)){if(kept)j+=',';j+="["+String(last.lat,6)+","+String(last.lon,6)+","+String(pending?1:0)+"]";}
  f.close();return j+"],\"sampled\":"+String(n>300?"true":"false")+"}";
}
