#include "Arduino.h"
#include "SD.h"
#include "field_tracks.h"
#include "gps.h"
#include "map_storage.h"
#include <cassert>
#include <iostream>
#include <fstream>
#include <cmath>
#include <iterator>
MockSD SD;
uint32_t testNow=1000;
GpsFix fix={};
GpsFix gpsGet(){return fix;}
void gpsLoop(){}
bool gpsUtcNow(uint32_t* out){*out=1795000000;return true;}
static std::string read(const String &p){std::ifstream in(SD.path(p),std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
static bool collect(const char *s,size_t n,void *ctx){static_cast<std::string*>(ctx)->append(s,n);return true;}
int main(int argc,char**argv){
 SD.root=argv[1];fs::create_directories(SD.root);
 SD.put("/maps/center.txt","9 99 12 8 16\n");
 SD.put("/maps-chumphon/center.txt","10 99 12 8 16\n");
 SD.put("/maps/track.csv","8.000000,98.000000\n8.010000,98.010000\n");
 mapStorageBegin();fieldTrackBegin();assert(!fieldTrackRecording());
 std::string gpx;assert(fieldTrackExport(collect,&gpx,mapStorageRevision()));assert(gpx.find("8.0000000")!=std::string::npos);
 assert(!fieldTrackExport(collect,&gpx,mapStorageRevision()+1));
 assert(fieldTrackSetRecording(true));assert(fieldTrackRecording());
 fix.valid=true;fix.lat=9.1;fix.lon=99.1;fix.altM=23.5;fix.fixMs=testNow;
 fieldTrackLoop();assert(fieldTrackStatus().points==1);
 fieldTrackLoop();assert(fieldTrackStatus().points==1);
 testNow+=2000;fix.fixMs=testNow;fix.lat+=0.000001;fieldTrackLoop();assert(fieldTrackStatus().points==1);
 testNow+=2000;fix.fixMs=testNow;fix.lat+=0.001;fieldTrackLoop();assert(fieldTrackStatus().points==2);
 fix.valid=false;fieldTrackLoop();testNow+=3000;fix.valid=true;fix.fixMs=testNow;fix.lat+=0.1;fieldTrackLoop();assert(fieldTrackStatus().points==3);
 assert(fieldTrackSetRecording(false));assert(!fieldTrackRecording());
 auto recorded=read("/maps/recorded-track.csv");assert(recorded.find("23.50")!=std::string::npos);
 assert(fieldTrackStartBacktrack());int count=0;auto pts=fieldTrackBacktrack(count);
 assert(count==3&&fabs(pts[0].lat-fix.lat)<0.00001&&pts[0].start&&pts[1].start&&!pts[2].start);
 gpx="";assert(fieldTrackExport(collect,&gpx));assert(gpx.find("<ele>23.50</ele>")!=std::string::npos&&gpx.find("<time>")!=std::string::npos);
 auto split=gpx.find("</trkseg><trkseg>");assert(split!=std::string::npos);
 auto oldRevision=mapStorageRevision();assert(mapStorageSelect("/maps-chumphon"));fieldTrackLoop();assert(!fieldTrackRecording());
 fieldTrackBacktrack(count);assert(!count);gpx="";assert(!fieldTrackExport(collect,&gpx));assert(read("/maps/recorded-track.csv")==recorded);
 assert(mapStorageSelect("/maps"));fieldTrackLoop();assert(fieldTrackSetRecording(true));assert(read("/maps/last-track.csv")==recorded);
 gpx="";assert(fieldTrackExport(collect,&gpx));assert(gpx.find("9.1000000")!=std::string::npos); // blank current falls back to last
 SD.failWrites=true;testNow+=2000;fix.fixMs=testNow;fieldTrackLoop();assert(!fieldTrackRecording()&&fieldTrackStatus().error.length());SD.failWrites=false;
 std::string longTrack;
 for(int i=0;i<1400;i++)longTrack+="9."+std::to_string(1000000+i)+",99.1000000,12,1795000000,"+(i==700?"1":"0")+"\n";
 SD.put("/maps/recorded-track.csv",longTrack);assert(fieldTrackStartBacktrack());pts=fieldTrackBacktrack(count);assert(count<=600&&count>300&&fabs(pts[0].lat-9.1001399)<0.00001&&fabs(pts[count-1].lat-9.1)<0.00001);
 assert(std::count_if(pts,pts+count,[](const auto &p){return p.start;})>=2);
 gpx="";bool refused=false;
 auto reserve=[](const char *s,size_t n,void *ctx){bool &refused=*static_cast<bool*>(ctx);if(!refused){refused=!fieldTrackSetRecording(true);}return true;};
 assert(fieldTrackExport(reserve,&refused));assert(refused);assert(fieldTrackSetRecording(true));
 assert(fieldTrackSetRecording(false));
 std::cout<<"PASS field track record/jitter/gaps, legacy export, GPX time/elevation, per-map isolation, previous-session fallback, write failure, 1400-point reverse sampling, export reservation\n";
}
