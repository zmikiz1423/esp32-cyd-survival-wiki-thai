#include "SD.h"
#include "map_storage.h"
#include "offline_maps.h"
#include "gps.h"
#include <cassert>
#include <set>
#include <iostream>
MockSD SD;
GpsFix gpsGet(){return {}; }uint32_t gpsFixAgeS(){return UINT32_MAX;}void gpsLoop(){}
static void package(const String &root){SD.put(root+"/center.txt","9.1382 99.3215 12 8 16\n");}
static int checks=0;
#define CHECK(x) do{if(!(x)){std::cerr<<"FAIL line "<<__LINE__<<": " #x<<"\n";return 1;}checks++;}while(0)
int main(int argc,char **argv){
 if(argc!=2)return 2;SD.root=argv[1];fs::create_directories(SD.root);
 package("/maps");for(int i=0;i<137;++i){char b[40];snprintf(b,sizeof(b),"/maps-P%03d",i);package(b);}
 SD.put("/maps-P136/name.txt","สุราษฎร์ธานี\n");SD.put("/maps-Bad/center.txt","nan 99 12 8 16");SD.put("/maps-BadRange/center.txt","90 99 12 8 16");SD.put("/maps-BadStart/center.txt","9 99 7 8 16");SD.put("/maps-BadFraction/center.txt","9 99 12.5 8 16");SD.put("/maps-BadTrailing/center.txt","9 99 12 8 16 junk");SD.put("/maps-BadSeparator/center.txt","9+99 12 8 16");SD.put("/maps-BadNull/center.txt",std::string("9 99 12 8 16\0hidden",21));package("/maps.wrong");
 mapStorageBegin();CHECK(mapStorageRoot()=="/maps");CHECK(mapStoragePath("center.txt")=="/maps/center.txt");CHECK(mapStoragePath("/center.txt")=="/maps/center.txt");
 uint32_t offset=0,next;std::set<std::string> found;int pages=0;
 do{MapSetEntry items[4];int n=mapStorageList(offset,items,4,next);CHECK(n>0&&n<=4);for(int i=0;i<n;++i)CHECK(found.insert(items[i].path.c_str()).second);if(next!=UINT32_MAX)CHECK(next==offset+4);offset=next;++pages;}while(next!=UINT32_MAX);
 CHECK(found.size()==138);CHECK(pages==35);
 MapSetEntry previous[4];CHECK(mapStorageList(132,previous,4,next)==4);CHECK(next==136);
 uint32_t rev=mapStorageRevision();CHECK(mapStorageSelect("/maps-P136"));CHECK(mapStorageRoot()=="/maps-P136");CHECK(mapStorageName()=="สุราษฎร์ธานี");CHECK(mapStorageRevision()==rev+1);mapStorageBegin();CHECK(mapStorageRoot()=="/maps-P136");
 for(const char *bad:{"/maps/../maps-P002","/maps-../maps-P002","/maps.wrong","/maps-Bad","/maps-BadRange","/maps-BadStart","/maps-BadFraction","/maps-BadTrailing","/maps-BadSeparator","/maps-BadNull"})CHECK(!mapStorageSelect(bad));
 SD.failWrites=true;CHECK(!mapStorageSelect("/maps-P002"));CHECK(mapStorageRoot()=="/maps-P136");SD.failWrites=false;
 SD.renameCalls=0;SD.failRenameAt=2;CHECK(!mapStorageSelect("/maps-P002"));CHECK(mapStorageRoot()=="/maps-P136");SD.failRenameAt=0;mapStorageBegin();CHECK(mapStorageRoot()=="/maps-P136");
 SD.rename("/active-map.txt","/active-map.bak");mapStorageBegin();CHECK(mapStorageRoot()=="/maps-P136");
 SD.put("/maps-P136/pois.tsv","1\tสถานที่จังหวัดเดิม\t9.1\t99.1\tplace\n");SD.put("/maps-P002/pois.tsv","2\tสถานที่จังหวัดใหม่\t10.2\t99.2\tplace\n");
 SD.put("/maps-P136/routes/old.gpx","<gpx><trk><trkseg><trkpt lat='9.1' lon='99.1'/></trkseg></trk></gpx>");SD.put("/maps-P002/routes/new.gpx","<gpx><trk><trkseg><trkpt lat='10.2' lon='99.2'/></trkseg></trk></gpx>");
 CHECK(offlineSelectRoute("old.gpx"));CHECK(offlineRouteRefresh());int n=0;const OfflineRoutePoint *points=offlineRoute(n);CHECK(n==1&&points[0].lat>9&&points[0].lat<10);
 CHECK(offlineSetDestination(9.1,99.1,"old"));CHECK(offlineDestination().valid);CHECK(mapStorageSelect("/maps-P002"));CHECK(!offlineDestination().valid);CHECK(offlineRouteRefresh());offlineRoute(n);CHECK(n==0);
 OfflinePoi poi[4];CHECK(offlineFindPois("",0,poi,4,next)==1);CHECK(poi[0].name=="สถานที่จังหวัดใหม่");CHECK(offlineSelectRoute("new.gpx"));CHECK(offlineRouteRefresh());points=offlineRoute(n);CHECK(n==1&&points[0].lat>10);CHECK(!offlineSelectRoute("old.gpx"));CHECK(offlineStatusJson().indexOf("\"mapName\":\"P002\"")>=0);
 CHECK(mapStorageSelect("/maps-P136"));CHECK(offlineRouteRefresh());points=offlineRoute(n);CHECK(n==1&&points[0].lat<10);
 uint32_t stale=mapStorageRevision()-1;CHECK(!offlineSetDestination(9,99,"stale",stale));CHECK(!offlineSelectRoute("old.gpx",stale));
 SD.beforeOpen=[](){if(!mapStorageSelect("/maps-P002"))std::abort();};CHECK(!offlineSelectRoute("old.gpx",mapStorageRevision()));CHECK(mapStorageRoot()=="/maps-P002");CHECK(offlineRoutesJson().indexOf("new.gpx")>=0);CHECK(offlineRoutesJson().indexOf("old.gpx")<0);
 CHECK(mapStorageSelect("/maps-P136"));
 fs::remove_all(SD.path("/maps-P136"));mapStorageBegin();CHECK(mapStorageRoot()=="/maps");fs::remove_all(SD.path("/maps"));mapStorageBegin();CHECK(mapStorageRoot()=="/maps-P000");
 std::cout<<"PASS "<<checks<<" checks: 138-package pagination, validation, persistence failures/recovery, POI/route/destination isolation\n";
}
