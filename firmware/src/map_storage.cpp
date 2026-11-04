#include "map_storage.h"
#include <SD.h>
#include <math.h>
#include <ctype.h>

static portMUX_TYPE storageMux = portMUX_INITIALIZER_UNLOCKED;
static char activeRoot[81] = "/maps";
static char activeLabel[96] = "แผนที่เดิม";
static uint32_t revision = 0;

static bool safeRoot(const String &root) {
  if (root == "/maps") return true;
  if (!root.startsWith("/maps-") || root.length() <= 6 || root.length() >= sizeof(activeRoot)) return false;
  for (size_t i=6; i<root.length(); ++i) {
    char c=root[i];
    if (!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_')) return false;
  }
  return true;
}
static String line(File &f, size_t max) {
  String s; bool overflow=false;
  while (f.available()) { char c=f.read(); if (c=='\n') break; if (c=='\r') continue; if(s.length()<max)s+=c;else overflow=true; }
  return overflow ? String() : s;
}
// Parse the full centre record rather than accepting sscanf's valid prefix.
static bool validCenter(const String &root) {
  if (!safeRoot(root)) return false;
  File dir=SD.open(root); bool directory=dir&&dir.isDirectory(); if(dir)dir.close(); if(!directory)return false;
  File f=SD.open(root+"/center.txt", FILE_READ); if(!f||f.isDirectory()){if(f)f.close();return false;}
  if(f.size()>160){f.close();return false;}
  String s; while(f.available()){int c=f.read();if(c==0){f.close();return false;}s+=(char)c;} f.close();
  const char *p=s.c_str(); double values[5];
  for(int i=0;i<5;++i){while(isspace((unsigned char)*p))++p; char *end=nullptr; values[i]=strtod(p,&end);if(end==p||!isfinite(values[i])||(i<4&&!isspace((unsigned char)*end)))return false;p=end;}
  while(isspace((unsigned char)*p))++p; if(*p)return false;
  if(fabs(values[0])>85.05112878||fabs(values[1])>180)return false;
  for(int i=2;i<5;++i)if(values[i]!=floor(values[i])||values[i]<0||values[i]>18)return false;
  return values[3]<=values[2]&&values[2]<=values[4];
}
static String labelFor(const String &root) {
  File f=SD.open(root+"/name.txt",FILE_READ);String name=f&&!f.isDirectory()?line(f,95):String();if(f)f.close();
  name.trim();
  for(size_t i=0;i<name.length();++i)if((uint8_t)name[i]<32||name[i]==127){name="";break;}
  if(!name.length())name=root=="/maps"?String("แผนที่เดิม"):root.substring(6);
  return name;
}
MapStorageSnapshot mapStorageSnapshot() {
  char root[sizeof(activeRoot)], name[sizeof(activeLabel)];uint32_t seq;
  portENTER_CRITICAL(&storageMux);memcpy(root,activeRoot,sizeof(root));memcpy(name,activeLabel,sizeof(name));seq=revision;portEXIT_CRITICAL(&storageMux);
  return {String(root),String(name),seq};
}
String mapStorageRoot(){return mapStorageSnapshot().root;}
String mapStorageName(){return mapStorageSnapshot().name;}
uint32_t mapStorageRevision(){portENTER_CRITICAL(&storageMux);uint32_t n=revision;portEXIT_CRITICAL(&storageMux);return n;}
String mapStoragePath(const String &relative){return mapStorageRoot()+(relative.startsWith("/")?String():String("/"))+relative;}
static void activate(const String &root) {
  String name=labelFor(root);
  portENTER_CRITICAL(&storageMux);strlcpy(activeRoot,root.c_str(),sizeof(activeRoot));strlcpy(activeLabel,name.c_str(),sizeof(activeLabel));++revision;portEXIT_CRITICAL(&storageMux);
}
static String readSelection(const char *path) {
  File f=SD.open(path,FILE_READ);String root=f&&!f.isDirectory()?line(f,80):String();if(f)f.close();root.trim();return safeRoot(root)?root:String();
}
// Write and verify a temporary record before replacing the previous selection.
// The backup also makes a power loss between FAT renames recoverable on boot.
static bool saveSelection(const String &root) {
  const char *tmp="/active-map.tmp", *saved="/active-map.txt", *backup="/active-map.bak";
  if(SD.exists(tmp)&&!SD.remove(tmp))return false;
  File f=SD.open(tmp,FILE_WRITE);if(!f)return false;size_t written=f.print(root+"\n");f.close();
  if(written!=root.length()+1||readSelection(tmp)!=root){SD.remove(tmp);return false;}
  bool hadSaved=SD.exists(saved);
  if(hadSaved){if(SD.exists(backup)&&!SD.remove(backup)){SD.remove(tmp);return false;}if(!SD.rename(saved,backup)){SD.remove(tmp);return false;}}
  if(!SD.rename(tmp,saved)){if(hadSaved)SD.rename(backup,saved);return false;}
  if(SD.exists(backup))SD.remove(backup);
  return true;
}
int mapStorageList(uint32_t offset,MapSetEntry *items,int capacity,uint32_t &next) {
  next=UINT32_MAX;if(!items||capacity<=0||offset==UINT32_MAX)return 0;
  File dir=SD.open("/");if(!dir||!dir.isDirectory()){if(dir)dir.close();return 0;}
  uint32_t seen=0,work=0;int count=0;
  while(true){
    File f=dir.openNextFile();if(!f)break;
    String base=f.name();int slash=base.lastIndexOf('/');if(slash>=0)base=base.substring(slash+1);
    String root="/"+base;bool candidate=f.isDirectory()&&safeRoot(root);f.close();
    if(candidate&&validCenter(root)){
      if(seen++>=offset){
        if(count==capacity){next=offset+(uint32_t)count;break;}
        items[count++]={root,labelFor(root)};
      }
    }
    if((++work&15)==0)yield();
  }
  dir.close();return count;
}
void mapStorageBegin() {
  String root=readSelection("/active-map.txt");
  if(!root.length()||!validCenter(root))root=readSelection("/active-map.bak");
  if(!root.length()||!validCenter(root)){
    root="/maps";
    if(!validCenter(root)){MapSetEntry entry;uint32_t next;if(mapStorageList(0,&entry,1,next))root=entry.path;}
  }
  activate(root);
}
bool mapStorageSelect(const String &root) {
  if(!validCenter(root))return false;
  if(root==mapStorageRoot())return true;
  if(!saveSelection(root))return false;
  activate(root);return true;
}
