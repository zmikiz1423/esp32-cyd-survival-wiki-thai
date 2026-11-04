// Phone map page, served at /map while the hotspot is on. Tiles come from the SD card at /maps/{z}/{x}/{y}.jpg.
#pragma once
#include <pgmspace.h>

static const char MAP_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="th"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<title>แผนที่</title>
<style>
@font-face{font-family:Sarabun;src:url('/web/Sarabun-Regular.ttf') format('truetype');font-display:swap}
:root{--bg:#0b0f17;--card:#131b26;--line:#233142;--text:#d5dfea;--muted:#8da0b4;--acc:#ff8a1f}
*{box-sizing:border-box}
html,body{margin:0;height:100%;background:var(--bg);color:var(--text);font:15px "Sarabun","Noto Sans Thai",Tahoma,system-ui,sans-serif;overscroll-behavior:none}
body{display:flex;flex-direction:column;height:100dvh}
header{padding:10px 14px;border-bottom:1px solid var(--line);display:flex;align-items:center;gap:12px;background:var(--card)}
header b{font-size:1.05rem}header a{color:var(--acc);text-decoration:none;font-size:.9rem}
header a.r{margin-left:auto}
#view{flex:1;position:relative;overflow:hidden;touch-action:none;background:#2a323d}
#layer{position:absolute;left:0;top:0;width:100%;height:100%;will-change:transform}
#layer img{position:absolute;width:256px;height:256px;user-select:none;-webkit-user-drag:none}
.me{position:absolute;width:18px;height:18px;margin:-9px 0 0 -9px;border-radius:50%;background:#2563eb;border:3px solid #fff;box-shadow:0 0 0 6px #2563eb55}
#cross{position:absolute;left:50%;top:50%;width:22px;height:22px;margin:-11px 0 0 -11px;pointer-events:none;opacity:.7}
#cross:before,#cross:after{content:"";position:absolute;background:#e11d48}
#cross:before{left:10px;top:0;width:2px;height:22px}#cross:after{top:10px;left:0;height:2px;width:22px}
#info{position:absolute;left:8px;bottom:8px;background:#0b0f17cc;padding:3px 8px;border-radius:8px;font-size:.78rem;pointer-events:none}
#msg{position:absolute;left:8px;right:8px;top:8px;background:#7c2d12;color:#ffedd5;padding:8px 10px;border-radius:10px;display:none;font-size:.88rem}
footer{display:flex;gap:6px;padding:8px 10px;border-top:1px solid var(--line);background:var(--card)}
footer button{flex:1;background:var(--bg);color:var(--text);border:1px solid var(--line);border-radius:10px;padding:10px 4px;font-size:.95rem}
footer button.on{background:var(--acc);color:#111;border-color:var(--acc)}
.attribution{flex-shrink:0;padding:3px 8px;background:var(--card);font-size:12px;line-height:1.6;border-top:1px solid var(--line)}
.attribution[hidden]{display:none}.attribution a{color:var(--text);white-space:nowrap}
body{font-family:"Sarabun","Noto Sans Thai",Tahoma,system-ui,sans-serif;line-height:1.65;overflow-wrap:anywhere}
header{flex-wrap:wrap}header>*{min-width:0}button{min-width:0;font-family:inherit;line-height:1.55;overflow-wrap:anywhere}
footer{flex-wrap:wrap}footer button{flex:1 1 52px}#msg{line-height:1.65}
#overlay{position:absolute;inset:0;pointer-events:none;z-index:2}#panel{position:absolute;z-index:5;right:8px;top:8px;width:min(360px,calc(100% - 16px));max-height:70%;overflow:auto;background:var(--card);border:1px solid var(--line);border-radius:10px;padding:10px}#panel{touch-action:pan-y}#panel[hidden]{display:none}#panel input,#panel select{width:100%;padding:8px;background:var(--bg);color:var(--text);border:1px solid var(--line);font:inherit;margin:4px 0}#panel button{padding:7px;border:1px solid var(--line);background:var(--bg);color:var(--text);font:inherit;border-radius:6px}#poiResults button{display:block;width:100%;text-align:left;margin-top:5px}#panel p{font-size:13px;color:var(--muted);margin:6px 0}#gpsState{padding:3px 10px;font-size:13px;line-height:1.6;background:var(--card)}#info{z-index:3;max-width:calc(100% - 16px)}#msg{z-index:4}
</style></head><body>
<header><b>&#128506; แผนที่</b><a href="/mesh">แชต</a><a href="/tools">เครื่องมือ</a><a class="r" href="/">คู่มือ</a></header>
<div id="view"><div id="layer"></div><canvas id="overlay"></canvas><section id="panel" hidden>
<b>ค้นหาและเส้นทางออฟไลน์</b><button id="closePanel" style="float:right">ปิด</button>
<form id="poiForm"><input id="poiQuery" maxlength="100" placeholder="ชื่อสถานที่ เช่น วัด โรงพยาบาล" aria-label="ชื่อสถานที่"><button type="submit">ค้นหาใน SD</button></form><div id="poiResults"></div><p id="poiNote"></p><button id="poiMore" hidden>ค้นหาต่อในข้อมูลส่วนถัดไป</button>
<label for="routes">เส้นทาง GPX ใน SD</label><select id="routes"><option value="">ไม่แสดงเส้นทาง</option></select><button id="loadRoute">แสดงเส้นทาง</button><p>เพิ่มไฟล์ .gpx ใน maps/routes บน SD แล้วเปิดหน้านี้ใหม่ เส้นทางยาวจะย่อจำนวนจุดเพื่อแสดงผล ระยะไปจุดหมายเป็นเส้นตรง</p></section><div id="cross"></div><div id="info"></div><div id="msg"></div></div>
<div id="attribution" class="attribution" hidden><a href="https://www.openstreetmap.org/copyright" target="_blank" rel="noopener">© OpenStreetMap contributors</a> | <a href="https://www.openmaptiles.org/" target="_blank" rel="noopener">© OpenMapTiles</a> | <a href="https://openfreemap.org/" target="_blank" rel="noopener">OpenFreeMap</a></div>
<div id="gpsState">กำลังอ่าน GPS จากบอร์ด…</div>
<footer>
<button id="find">ค้นหา/เส้นทาง</button><button id="follow">ตาม GPS บอร์ด</button>
<button id="zo">&minus;</button><button id="zi">+</button>
<button id="loc">&#128205; พิกัดฉัน</button><button id="set">ระบุจุด</button><button id="share">&#128225; แชร์ GPS</button>
</footer>
<script>
var T=256,z=13,zmin=8,zmax=16,cx=0,cy=0,me=null,setting=false,followBoard=false,routePoints=[],destination=null,lastGps=null,missingTiles=0;
var view=document.getElementById('view'),layer=document.getElementById('layer'),info=document.getElementById('info'),msg=document.getElementById('msg');
function wp(lat,lon,zz){var n=T*Math.pow(2,zz),r=lat*Math.PI/180;return{x:(lon+180)/360*n,y:(1-Math.log(Math.tan(r)+1/Math.cos(r))/Math.PI)/2*n}}
function ll(x,y,zz){var n=T*Math.pow(2,zz);return{lon:x/n*360-180,lat:Math.atan(Math.sinh(Math.PI*(1-2*y/n)))*180/Math.PI}}
function say(t){msg.textContent=t;msg.style.display=t?'block':'none';if(t)setTimeout(function(){msg.style.display='none'},7000)}
// The board serves only a few connections at a time, so load tiles three at a time and retry any that fail.
var queue=[],gen=0,busy=0,mapRevision=String(Date.now());
function fetchTile(im,g,tries){
  busy++;var done=false;
  function fin(ok){if(done)return;done=true;if(g!==gen)return;busy--;
    if(!ok&&tries<4&&g===gen){setTimeout(function(){if(g===gen)fetchTile(im,g,tries+1)},500*(tries+1))}
    else if(!ok){im.style.visibility='hidden';missingTiles++;say('บางแผ่นยังไม่มีใน SD หรืออ่านไม่ได้ ตรวจพื้นที่และซูมที่เตรียมไว้')}
    pump(g)}
  im.onload=function(){fin(true)};im.onerror=function(){fin(false)};
  im.src=im.dataset.src+'?v='+encodeURIComponent(mapRevision)+(tries?'&r='+tries:'')}
function pump(g){while(g===gen&&busy<3&&queue.length)fetchTile(queue.shift(),g,0)}
function draw(){
  queue=[];busy=0;missingTiles=0;
  var W=view.clientWidth,H=view.clientHeight,n=Math.pow(2,z),left=cx-W/2,top=cy-H/2;
  layer.innerHTML='';layer.style.transform='translate(0,0)';
  for(var ty=Math.floor(top/T);ty<=Math.floor((top+H-1)/T);ty++)for(var tx=Math.floor(left/T);tx<=Math.floor((left+W-1)/T);tx++){
    if(tx<0||ty<0||tx>=n||ty>=n)continue;
    var im=new Image();im.draggable=false;im.dataset.src='/maps/'+z+'/'+tx+'/'+ty+'.jpg';
    im.style.left=(tx*T-left)+'px';im.style.top=(ty*T-top)+'px';layer.appendChild(im);queue.push(im)}
  gen++;pump(gen);
  if(me){var p=wp(me.lat,me.lon,z),d=document.createElement('div');d.className='me';d.style.left=(p.x-left)+'px';d.style.top=(p.y-top)+'px';layer.appendChild(d)}
  var c=ll(cx,cy,z);info.textContent=c.lat.toFixed(4)+', '+c.lon.toFixed(4)+'  z'+z;paintOverlay();
}
function zoom(dz){var nz=Math.max(zmin,Math.min(zmax,z+dz));if(nz===z)return;var f=Math.pow(2,nz-z);cx*=f;cy*=f;z=nz;draw()}
document.getElementById('zi').onclick=function(){zoom(1)};document.getElementById('zo').onclick=function(){zoom(-1)};
var ptrs={},sx,sy,moved=0,startDist=0;
view.onpointerdown=function(e){if(e.target.closest('#panel'))return;view.setPointerCapture(e.pointerId);ptrs[e.pointerId]={x:e.clientX,y:e.clientY};sx=e.clientX;sy=e.clientY;moved=0;
  var k=Object.keys(ptrs);if(k.length===2){var a=ptrs[k[0]],b=ptrs[k[1]];startDist=Math.hypot(a.x-b.x,a.y-b.y)}};
view.onpointermove=function(e){if(!ptrs[e.pointerId])return;ptrs[e.pointerId]={x:e.clientX,y:e.clientY};
  var k=Object.keys(ptrs);
  if(k.length===2){var a=ptrs[k[0]],b=ptrs[k[1]],d=Math.hypot(a.x-b.x,a.y-b.y);
    if(startDist&&d/startDist>1.6){zoom(1);startDist=d}else if(startDist&&d/startDist<0.62){zoom(-1);startDist=d}return}
  var dx=e.clientX-sx,dy=e.clientY-sy;moved=Math.max(moved,Math.abs(dx)+Math.abs(dy));layer.style.transform='translate('+dx+'px,'+dy+'px)'};
view.onpointerup=view.onpointercancel=function(e){
  if(!ptrs[e.pointerId])return;var multi=Object.keys(ptrs).length>1;delete ptrs[e.pointerId];
  if(multi){draw();return}
  var dx=e.clientX-sx,dy=e.clientY-sy;
  if(moved<8&&setting){var r=view.getBoundingClientRect();var p=ll(cx-view.clientWidth/2+(e.clientX-r.left),cy-view.clientHeight/2+(e.clientY-r.top),z);
    me={lat:p.lat,lon:p.lon};setting=false;document.getElementById('set').className='';say('ระบุตำแหน่งของคุณแล้ว')}
  else{if(moved>=8){followBoard=false;document.getElementById('follow').className=''}cx-=dx;cy-=dy}
  draw()};
document.getElementById('set').onclick=function(){setting=!setting;this.className=setting?'on':'';if(setting)say('แตะจุดที่คุณอยู่บนแผนที่')};
document.getElementById('loc').onclick=function(){
  if(location.protocol==='http:'){   // phones only share their location on https pages
    say('กำลังเปิดหน้าเว็บแบบปลอดภัย โทรศัพท์จะแจ้งเตือนใบรับรอง ให้เลือก ขั้นสูง (Advanced) แล้วเลือกดำเนินการต่อ (Continue) โดยทำเพียงครั้งแรก');
    setTimeout(function(){location.href='https://'+location.hostname+'/map?gps=1'},2500);return}
  if(!navigator.geolocation){say('เบราว์เซอร์นี้แจ้งพิกัดไม่ได้ ใช้ปุ่ม "ระบุจุด" แทน');return}
  say('กำลังขอพิกัดจากโทรศัพท์...');
  navigator.geolocation.getCurrentPosition(function(p){me={lat:p.coords.latitude,lon:p.coords.longitude};var w=wp(me.lat,me.lon,z);cx=w.x;cy=w.y;say('');draw()},
    function(e){say('โทรศัพท์ไม่อนุญาตให้ใช้พิกัด ('+(e&&e.message?e.message:'ถูกปิดกั้น')+'). โปรดอนุญาตให้เบราว์เซอร์ใช้ตำแหน่ง หรือกด "ระบุจุด" แล้วแตะตำแหน่งบนแผนที่')},
    {enableHighAccuracy:true,timeout:15000})};
fetch('/maps/center.txt',{cache:'no-store'}).then(function(r){if(!r.ok)throw 0;return r.text()}).then(function(t){
  var a=t.trim().split(/\s+/).map(Number);zmin=a[3];zmax=a[4];z=Math.max(zmin,Math.min(zmax,a[2]));var w=wp(a[0],a[1],z);cx=w.x;cy=w.y;draw();
  if(location.search.indexOf('gps=1')>=0)document.getElementById('loc').onclick()   // we were sent here to ask for the location
}).catch(function(){info.textContent='ไม่มีแผนที่ในการ์ด SD';say('ไม่พบแผนที่ เปิด map-downloader.bat หรือ ดาวน์โหลดแผนที่ไทย.bat บนคอมพิวเตอร์ เลือกพื้นที่ แล้วบันทึกโฟลเดอร์ maps ลง SD')});
fetch('/maps/source.json',{cache:'no-store'}).then(function(r){if(!r.ok)throw 0;return r.json()}).then(function(s){
  if(s&&(s.provider==='openfreemap'||s.provider==='local-osm')){mapRevision=String(s.fingerprint||s.dataset||s.completedUtc||'th3').slice(0,12);var a=document.getElementById('attribution');if(s.provider==='local-osm'){while(a.childNodes.length>1)a.removeChild(a.lastChild)}a.hidden=false;draw()}
}).catch(function(){}); // Legacy USGS map folders have no source manifest.
// Share this phone's GPS with the board: the board then uses it for its own map dot, radar and mesh position sharing.
var watchId=null,lastSend=0,lastPos=null,lastSentTimestamp=0,keep=null,wake=null;
function sendPos(p,force){
  var now=Date.now();if(!p||!Number.isFinite(p.timestamp)||Math.abs(now-p.timestamp)>15000)return;
  lastPos=p;if((!force&&now-lastSend<4000)||p.timestamp<=lastSentTimestamp)return;lastSend=now;lastSentTimestamp=p.timestamp;
  fetch('/api/pos',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body:'lat='+p.coords.latitude.toFixed(6)+'&lon='+p.coords.longitude.toFixed(6)+'&acc='+Math.round(p.coords.accuracy||0)}).catch(function(){})}
function toggleShare(){
  var b=document.getElementById('share');
  if(watchId!==null){navigator.geolocation.clearWatch(watchId);watchId=null;clearInterval(keep);keep=null;lastPos=null;lastSentTimestamp=0;lastSend=0;if(wake){try{wake.release()}catch(e){}wake=null}
    b.className='';b.innerHTML='&#128225; แชร์ GPS';say('หยุดแชร์พิกัดแล้ว');return}
  if(location.protocol==='http:'){
    say('กำลังเปิดหน้าเว็บแบบปลอดภัย โทรศัพท์จะแจ้งเตือนใบรับรอง ให้เลือก ขั้นสูง (Advanced) แล้วเลือกดำเนินการต่อ (Continue) โดยทำเพียงครั้งแรก');
    setTimeout(function(){location.href='https://'+location.hostname+'/map?share=1'},2500);return}
  if(!navigator.geolocation){say('เบราว์เซอร์นี้แจ้งพิกัดไม่ได้');return}
  say('กำลังขอพิกัดจากโทรศัพท์ โปรดเปิดหน้านี้ไว้ตลอดเวลาที่แชร์');
  watchId=navigator.geolocation.watchPosition(function(p){
    me={lat:p.coords.latitude,lon:p.coords.longitude};sendPos(p,false);
    var b2=document.getElementById('share');b2.className='on';b2.innerHTML='&#128225; กำลังแชร์';
    say('');draw()},
    function(e){say('โทรศัพท์ไม่อนุญาตให้ใช้พิกัด ('+(e&&e.message?e.message:'ถูกปิดกั้น')+').');if(watchId!==null){navigator.geolocation.clearWatch(watchId);watchId=null}clearInterval(keep);keep=null;lastPos=null;lastSentTimestamp=0;lastSend=0;if(wake){try{wake.release()}catch(ignore){}wake=null}var b=document.getElementById('share');b.className='';b.innerHTML='&#128225; แชร์ GPS'},
    {enableHighAccuracy:true,maximumAge:5000,timeout:20000});
  keep=setInterval(function(){if(lastPos)sendPos(lastPos,true)},15000);   // the board forgets a position after 45 s
  if(navigator.wakeLock){navigator.wakeLock.request('screen').then(function(w){wake=w}).catch(function(){})}
}
document.getElementById('share').onclick=toggleShare;
if(location.search.indexOf('share=1')>=0)setTimeout(toggleShare,400);
// GPS, POI search and route selection use only the board and its SD card.
function distanceBearing(a,b){var r=Math.PI/180,p=a.lat*r,q=b.lat*r,d=(b.lon-a.lon)*r,dl=q-p,h=Math.sin(dl/2)**2+Math.cos(p)*Math.cos(q)*Math.sin(d/2)**2;return{m:6371000*2*Math.atan2(Math.sqrt(h),Math.sqrt(Math.max(0,1-h))),deg:(Math.atan2(Math.sin(d)*Math.cos(q),Math.cos(p)*Math.sin(q)-Math.sin(p)*Math.cos(q)*Math.cos(d))/r+360)%360}}
function paintOverlay(){
  var canvas=document.getElementById('overlay'),W=view.clientWidth,H=view.clientHeight;canvas.width=W;canvas.height=H;var c=canvas.getContext('2d'),left=cx-W/2,top=cy-H/2;
  c.strokeStyle='#ff9a24';c.lineWidth=3;c.beginPath();routePoints.forEach(function(p,i){var w=wp(p[0],p[1],z),x=w.x-left,y=w.y-top;if(i===0||p[2])c.moveTo(x,y);else c.lineTo(x,y)});c.stroke();
  if(destination){var d=wp(destination.lat,destination.lon,z);c.strokeStyle='#e879f9';c.lineWidth=3;c.beginPath();c.arc(d.x-left,d.y-top,10,0,Math.PI*2);c.stroke()}
  var lat=ll(cx,cy,z).lat,mpp=156543.033928*Math.cos(lat*Math.PI/180)/2**z,wanted=mpp*70,base=10**Math.floor(Math.log10(wanted)),m=(wanted/base>=5?5:wanted/base>=2?2:1)*base,len=m/mpp;
  c.fillStyle='#0b0f17dd';c.fillRect(8,8,Math.max(95,len+16),43);c.fillStyle='#fff';c.font='12px Sarabun,Tahoma,sans-serif';c.fillText((m>=1000?(m/1000)+' กม.':m+' ม.')+' · N ↑',14,25);c.strokeStyle='#fff';c.lineWidth=2;c.beginPath();c.moveTo(14,34);c.lineTo(14,41);c.moveTo(14,38);c.lineTo(14+len,38);c.moveTo(14+len,34);c.lineTo(14+len,41);c.stroke();
  var center=ll(cx,cy,z),text=center.lat.toFixed(4)+', '+center.lon.toFixed(4)+'  z'+z;
  if(lastGps&&lastGps.valid&&destination){var db=distanceBearing(lastGps,destination);text+=' → '+destination.name+' '+(db.m<1000?Math.round(db.m)+' ม.':(db.m/1000).toFixed(1)+' กม.')+' '+Math.round(db.deg)+'°'}info.textContent=text;
}
async function mapJson(path,body){var options={cache:'no-store'};if(body){options.method='POST';options.headers={'Content-Type':'application/x-www-form-urlencoded'};options.body=new URLSearchParams(body).toString()}var r=await fetch('/api/map/'+path,options);if(!r.ok)throw Error('อ่านข้อมูลจากบอร์ดไม่ได้');return r.json()}
function updateMe(){var old=layer.querySelector('.me');if(old)old.remove();if(me){var w=wp(me.lat,me.lon,z),dot=document.createElement('div');dot.className='me';dot.style.left=(w.x-cx+view.clientWidth/2)+'px';dot.style.top=(w.y-cy+view.clientHeight/2)+'px';layer.appendChild(dot)}paintOverlay()}
var gpsBusy=false;
async function pollGps(){if(gpsBusy||document.hidden)return;gpsBusy=true;try{var g=await mapJson('status');lastGps=g;destination=g.destination;var label=document.getElementById('gpsState');label.textContent=g.valid?(g.phone?'GPS จากโทรศัพท์':'GPS '+g.satellites+' ดาวเทียม')+' · '+Math.round(g.speed)+' กม./ชม. · '+Math.round(g.altitude)+' ม.':(g.age<4294967295?'GPS รอตำแหน่ง · ตำแหน่งล่าสุด '+g.age+' วินาทีก่อน':'GPS รอตำแหน่ง');if(g.valid){me={lat:g.lat,lon:g.lon};var w=wp(me.lat,me.lon,z);if(followBoard&&(Math.abs(w.x-cx)>12||Math.abs(w.y-cy)>12)){cx=w.x;cy=w.y;draw()}else updateMe()}else paintOverlay()}catch(e){document.getElementById('gpsState').textContent='ยังอ่านสถานะ GPS จากบอร์ดไม่ได้'}finally{gpsBusy=false}}
document.getElementById('follow').onclick=function(){followBoard=!followBoard;this.className=followBoard?'on':'';if(followBoard){if(lastGps&&lastGps.valid){var w=wp(lastGps.lat,lastGps.lon,z);cx=w.x;cy=w.y;draw()}else say('เปิดโหมดติดตามแล้ว กำลังรอตำแหน่ง GPS จากบอร์ด')}};
var poiOffset=0,poiQuery='';
async function searchPois(reset){var note=document.getElementById('poiNote'),more=document.getElementById('poiMore');if(reset){poiOffset=0;poiQuery=document.getElementById('poiQuery').value.trim();document.getElementById('poiResults').replaceChildren()}more.disabled=true;note.textContent='กำลังค้นหาใน SD…';try{var data=await mapJson('pois?q='+encodeURIComponent(poiQuery)+'&offset='+poiOffset);data.items.forEach(function(p){var b=document.createElement('button');b.textContent=p.name+' · '+p.category;b.onclick=async function(){try{var ok=await mapJson('destination',{name:p.name,lat:p.lat,lon:p.lon});if(!ok.ok)throw 0;destination=p;followBoard=false;document.getElementById('follow').className='';var w=wp(p.lat,p.lon,z);cx=w.x;cy=w.y;draw();document.getElementById('panel').hidden=true;say('เลือกจุดหมายแล้ว ระยะที่แสดงเป็นเส้นตรง')}catch(e){say('กำหนดจุดหมายไม่สำเร็จ')}};document.getElementById('poiResults').appendChild(b)});poiOffset=data.next;more.hidden=data.next===null;note.textContent=data.missing?'ยังไม่มี maps/pois.tsv ให้เตรียมชุดจังหวัดก่อน':data.next!==null?'ยังมีข้อมูลส่วนถัดไป กดค้นหาต่อได้':'ค้นหาถึงท้ายข้อมูลแล้ว'}catch(e){note.textContent=e.message}finally{more.disabled=false}}
document.getElementById('poiForm').onsubmit=function(e){e.preventDefault();searchPois(true)};document.getElementById('poiMore').onclick=function(){searchPois(false)};
async function loadRoutes(){document.getElementById('routes').disabled=true;document.getElementById('loadRoute').disabled=true;try{var data=await mapJson('routes'),select=document.getElementById('routes');select.replaceChildren(new Option('ไม่แสดงเส้นทาง',''));data.items.forEach(function(n){select.add(new Option(n,n))});select.value=data.selected||'';var route=await mapJson('route');routePoints=route.points||[];paintOverlay()}catch(e){say('อ่านรายการเส้นทางไม่ได้')}finally{document.getElementById('routes').disabled=false;document.getElementById('loadRoute').disabled=false}}
document.getElementById('find').onclick=function(){document.getElementById('panel').hidden=!document.getElementById('panel').hidden;if(!document.getElementById('panel').hidden)loadRoutes()};document.getElementById('closePanel').onclick=function(){document.getElementById('panel').hidden=true};
document.getElementById('loadRoute').onclick=async function(){try{var result=await mapJson('route',{name:document.getElementById('routes').value});if(!result.ok)throw 0;var r=await mapJson('route');routePoints=r.points||[];paintOverlay();say(routePoints.length?'แสดงเส้นทางแล้ว'+(r.sampled?' (ย่อจำนวนจุดสำหรับแสดงผล)':''):'ปิดเส้นทางแล้ว')}catch(e){say('เปิดเส้นทางไม่สำเร็จ ตรวจไฟล์ GPX ขนาดไม่เกิน 4 MB')}};
setInterval(pollGps,2000);pollGps();loadRoutes();
window.onresize=draw;
</script></body></html>)HTML";
