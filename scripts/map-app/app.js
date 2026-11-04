import {decodeTile,renderTile,jpegBlob} from './renderer.js';

const $=id=>document.getElementById(id),token=window.MAP_TOKEN;
const presets=[['กรุงเทพมหานคร',13.7563,100.5018],['เชียงใหม่',18.7883,98.9853],['เชียงราย',19.9105,99.8406],['แม่ฮ่องสอน',19.3020,97.9654],['ปาย',19.3582,98.4404],['ลำปาง',18.2888,99.4909],['น่าน',18.7756,100.7730],['พิษณุโลก',16.8211,100.2659],['สุโขทัย',17.0078,99.8230],['นครสวรรค์',15.7047,100.1372],['อุดรธานี',17.4138,102.7870],['หนองคาย',17.8783,102.7413],['ขอนแก่น',16.4322,102.8236],['เลย',17.4860,101.7223],['นครราชสีมา',14.9799,102.0978],['อุบลราชธานี',15.2287,104.8564],['สุรินทร์',14.8818,103.4936],['บุรีรัมย์',14.9951,103.1029],['พระนครศรีอยุธยา',14.3692,100.5877],['ลพบุรี',14.7995,100.6534],['นครนายก',14.2069,101.2131],['ปากช่อง',14.7081,101.4161],['กาญจนบุรี',14.0228,99.5328],['ราชบุรี',13.5283,99.8134],['เพชรบุรี',13.1119,99.9447],['หัวหิน',12.5684,99.9577],['ชลบุรี',13.3611,100.9847],['พัทยา',12.9236,100.8825],['ระยอง',12.6814,101.2816],['จันทบุรี',12.6112,102.1038],['ตราด',12.2428,102.5175],['ชุมพร',10.4930,99.1800],['สุราษฎร์ธานี',9.1382,99.3215],['นครศรีธรรมราช',8.4304,99.9631],['กระบี่',8.0863,98.9063],['ภูเก็ต',7.8804,98.3923],['ตรัง',7.5594,99.6114],['หาดใหญ่',7.0084,100.4747],['สงขลา',7.1898,100.5954],['ปัตตานี',6.8695,101.2501],['ยะลา',6.5411,101.2804],['นราธิวาส',6.4255,101.8253]];
// Approximate provincial-capital starting points, not administrative polygons.
// Exact destinations can be found with the search button or coordinate paste.
presets.push(['อำนาจเจริญ',15.8585,104.6280],['อ่างทอง',14.5896,100.4549],['บึงกาฬ',18.3609,103.6466],['ฉะเชิงเทรา',13.6904,101.0780],['ชัยนาท',15.1852,100.1251],['ชัยภูมิ',15.8068,102.0315],['กาฬสินธุ์',16.4328,103.5066],['กำแพงเพชร',16.4828,99.5227],['ลำพูน',18.5745,99.0087],['มหาสารคาม',16.1851,103.3007],['มุกดาหาร',16.5436,104.7234],['นครปฐม',13.8199,100.0622],['นครพนม',17.3920,104.7696],['หนองบัวลำภู',17.2045,102.4407],['นนทบุรี',13.8621,100.5144],['ปทุมธานี',14.0208,100.5250],['พังงา',8.4501,98.5255],['พัทลุง',7.6167,100.0740],['พะเยา',19.1665,99.9019],['เพชรบูรณ์',16.4189,101.1591],['พิจิตร',16.4419,100.3488],['แพร่',18.1446,100.1403],['ปราจีนบุรี',14.0509,101.3717],['ประจวบคีรีขันธ์',11.8124,99.7973],['ระนอง',9.9658,98.6348],['ร้อยเอ็ด',16.0538,103.6520],['สระแก้ว',13.8240,102.0646],['สกลนคร',17.1611,104.1479],['สมุทรปราการ',13.5991,100.5998],['สมุทรสาคร',13.5475,100.2744],['สมุทรสงคราม',13.4098,100.0023],['สระบุรี',14.5289,100.9101],['สตูล',6.6238,100.0674],['สิงห์บุรี',14.8936,100.3967],['ศรีสะเกษ',15.1186,104.3220],['สุพรรณบุรี',14.4745,100.1177],['ตาก',16.8839,99.1258],['อุทัยธานี',15.3835,100.0246],['อุตรดิตถ์',17.6201,100.0993],['ยโสธร',15.7926,104.1453]);
let prepared=null,running=false,cancelled=false,config=null;
const cache=new Map();
async function api(path,options={}){
  const headers=new Headers(options.headers||{});headers.set('X-Map-Token',token);
  const response=await fetch(path,{...options,headers,cache:'no-store'});
  if(!response.ok){let message='';try{const data=await response.json();message=data.error||data.message||''}catch{}throw new Error(message||`เชื่อมต่อไม่สำเร็จ (${response.status})`)}
  return response;
}
async function json(path,body){return (await api(path,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body||{})})).json()}
function status(message,error=false){$('status').textContent=message;$('status').className=error?'error':''}
function numbers(){
  const text=$('coordinates').value.trim().replace(/[()]/g,'');const values=text.split(/[,\s]+/).filter(Boolean).map(Number);
  if(values.length!==2||!values.every(Number.isFinite))throw new Error('กรอกพิกัดเป็นตัวเลขสองค่า เช่น 13.7563, 100.5018');
  const [lat,lon]=values;if(lat<5||lat>21||lon<97||lon>106.5)throw new Error('กรุณาเลือกพิกัดในประเทศไทยหรือบริเวณชายแดน (ละติจูด 5–21 ลองจิจูด 97–106.5)');
  const [minZoom,maxZoom]=$('profile').value.split(',').map(Number);return {lat,lon,radius:Number($('radius').value),minZoom,maxZoom};
}
function tx(lon,z){return Math.floor((lon+180)/360*2**z)}
function ty(lat,z){const r=lat*Math.PI/180;return Math.floor((1-Math.log(Math.tan(r)+1/Math.cos(r))/Math.PI)/2*2**z)}
function estimate(p){
  const dy=p.radius/110.574,dx=p.radius/(111.32*Math.cos(p.lat*Math.PI/180));let count=0;
  for(let z=p.minZoom;z<=p.maxZoom;z++)count+=(tx(p.lon+dx,z)-tx(p.lon-dx,z)+1)*(ty(p.lat-dy,z)-ty(p.lat+dy,z)+1);
  return count;
}
function refresh(){
  if(running)return;prepared=null;$('download').disabled=true;
  try{const p=numbers(),count=estimate(p);$('count').textContent=`ประมาณ ${count.toLocaleString('th-TH')} แผ่น`;$('estimateNote').textContent=`ใช้พื้นที่ SD โดยประมาณ ${Math.max(1,Math.ceil(count*.025))} MB (ขนาดจริงขึ้นกับพื้นที่)${count>3000?' · เกินขนาดต่อครั้ง ให้ลดพื้นที่หรือระดับซูม':''}`;$('estimate').className=count>3000?'estimate bad':'estimate';$('prepare').disabled=count>3000;$('placeTitle').textContent='กดตรวจพื้นที่เพื่ออัปเดตตัวอย่าง'}catch(e){$('count').textContent=e.message;$('estimateNote').textContent='';$('estimate').className='estimate bad';$('prepare').disabled=true}
}
function busy(value){running=value;$('settings').classList.toggle('busy',value);$('download').disabled=value||!prepared;$('cancel').disabled=!value;$('openFolder').disabled=value;$('closeApp').disabled=value;$('prepare').disabled=value;for(const el of ['preset','query','coordinates','radius','profile','results','search','pickFolder'])$(el).disabled=value}
async function getVector(job,tile){
  const sz=Math.min(tile.z,14),f=2**(tile.z-sz),key=`${sz}/${Math.floor(tile.x/f)}/${Math.floor(tile.y/f)}`;
  if(cache.has(key)){const value=cache.get(key);cache.delete(key);cache.set(key,value);return value}
  const r=await api(`/api/tile?job=${encodeURIComponent(job)}&z=${tile.z}&x=${tile.x}&y=${tile.y}`);const parsed=decodeTile(await r.arrayBuffer());cache.set(key,parsed);if(cache.size>24)cache.delete(cache.keys().next().value);return parsed;
}
async function drawPreview(plan,p){
  const z=Math.min(p.maxZoom,14),x=tx(p.lon,z),y=ty(p.lat,z);let tile=plan.tiles.find(t=>t.z===z&&t.x===x&&t.y===y);
  if(!tile)tile=plan.tiles.find(t=>t.z===p.maxZoom)||plan.tiles[0];
  if(tile.exists){const r=await api(`/api/saved?job=${encodeURIComponent(plan.job)}&z=${tile.z}&x=${tile.x}&y=${tile.y}`);const bitmap=await createImageBitmap(await r.blob());const ctx=$('preview').getContext('2d');ctx.drawImage(bitmap,0,0,256,256);bitmap.close()}
  else await renderTile(await getVector(plan.job,tile),tile.z,tile.x,tile.y,$('preview'));
  $('placeTitle').textContent=`ตัวอย่างหนึ่งแผ่นใกล้จุดกลาง · ซูม ${tile.z}`;
}
$('preset').innerHTML=presets.map((p,i)=>`<option value="${i}">${p[0]}</option>`).join('');
$('preset').onchange=()=>{const p=presets[Number($('preset').value)];$('coordinates').value=`${p[1]}, ${p[2]}`;refresh()};
for(const id of ['coordinates','radius','profile'])$(id).addEventListener('input',refresh);
$('pickFolder').onclick=async()=>{try{status('เลือกไดรฟ์ SD หรือโฟลเดอร์ในหน้าต่างที่เปิดขึ้น...');const result=await json('/api/select-folder');if(result.output){$('folder').value=result.output;refresh();status('เลือกโฟลเดอร์แล้ว กดตรวจพื้นที่เพื่อเตรียมดาวน์โหลด')}}catch(e){status(e.message,true)}};
$('search').onclick=async()=>{
  const query=$('query').value.trim();if(!query)return;$('search').disabled=true;status('กำลังค้นหาสถานที่...');
  try{const results=await(await api('/api/search?q='+encodeURIComponent(query))).json();const list=Array.isArray(results)?results:(results.results||[]);$('results').replaceChildren();
    for(const place of list){const option=document.createElement('option');option.textContent=place.display_name||place.name;option.value=`${place.lat}, ${place.lon}`;$('results').appendChild(option)}
    $('results').style.display=list.length?'block':'none';$('resultLabel').hidden=!list.length;
    if(list.length){$('results').selectedIndex=0;$('coordinates').value=$('results').value;refresh();status(`พบ ${list.length} รายการ เลือกสถานที่แล้วตรวจพิกัดให้ตรงพื้นที่ที่ต้องการ`)}else status('ไม่พบสถานที่ ลองชื่ออำเภอ/จังหวัด หรือวางตัวเลขพิกัดแทน',true);
  }catch(e){status(e.message,true)}finally{$('search').disabled=false}
};$('query').addEventListener('keydown',e=>{if(e.key==='Enter'){e.preventDefault();$('search').click()}});
$('results').onchange=()=>{$('coordinates').value=$('results').value;refresh()};
$('prepare').onclick=async()=>{
  if(running)return;try{const p=numbers();busy(true);$('cancel').disabled=true;status('กำลังตรวจพื้นที่และแผ่นที่บันทึกแล้ว...');cache.clear();const plan=await json('/api/start',p);prepared=plan;
    await drawPreview(plan,p);status(`พร้อมดาวน์โหลด ${plan.count||plan.tiles.length} แผ่น\nมีอยู่แล้ว ${plan.tiles.filter(t=>t.exists).length} แผ่น · ${plan.output||$('folder').value}`);$('progress').max=plan.tiles.length;$('progress').value=0;
  }catch(e){prepared=null;status(e.message,true)}finally{busy(false);$('cancel').disabled=true}
};
$('download').onclick=async()=>{
  if(!prepared||running)return;const plan=prepared;cancelled=false;busy(true);let done=0,skipped=0,failed=[];
  try{
    for(const tile of plan.tiles){
      if(cancelled)break;
      if(tile.exists){skipped++;done++}else{
        try{const canvas=await renderTile(await getVector(plan.job,tile),tile.z,tile.x,tile.y);const blob=await jpegBlob(canvas);await api(`/api/save?job=${encodeURIComponent(plan.job)}&z=${tile.z}&x=${tile.x}&y=${tile.y}`,{method:'POST',headers:{'Content-Type':'image/jpeg'},body:blob});tile.exists=true;done++}
        catch(e){failed.push({tile,message:e.message});if(/50\s*MB|quota|ขนาด|จำกัด|429|403|budget|limit|ดาวน์โหลดไม่สำเร็จ|HTTP 0|timeout|Failed to fetch|NetworkError/i.test(e.message))throw e}
      }
      $('progress').value=done+failed.length;status(`บันทึกแล้ว ${done.toLocaleString('th-TH')} / ${plan.tiles.length.toLocaleString('th-TH')} แผ่น\n${skipped} แผ่นมีอยู่แล้ว · ${failed.length} แผ่นยังไม่สำเร็จ${cancelled?' · กำลังหยุด':''}`);
      await new Promise(resolve=>setTimeout(resolve,0));
    }
    if(cancelled){status(`หยุดแล้ว บันทึกไว้ ${done} แผ่น\nกดดาวน์โหลดอีกครั้งเพื่อทำต่อได้ จุดเปิดแผนที่จะเปลี่ยนเมื่อดาวน์โหลดครบ`);return}
    if(failed.length){status(`ยังขาด ${failed.length} แผ่น กดดาวน์โหลดอีกครั้งเพื่อทำต่อ\n${failed[0].message}`,true);return}
    const result=await json('/api/finish',{job:plan.job});$('progress').value=plan.tiles.length;
    status(`เสร็จครบ ${result.count||done} แผ่น${result.bytes?` · ${(result.bytes/1048576).toFixed(1)} MB`:''}\n${result.output||plan.output}\nนำโฟลเดอร์ maps ไปไว้ที่ราก SD แล้วรีเซตบอร์ด`);
  }catch(e){status(`หยุดดาวน์โหลด: ${e.message}\nแผ่นที่บันทึกสำเร็จยังอยู่ กดตรวจพื้นที่เพื่อเตรียมทำต่อ`,true)}finally{busy(false);$('cancel').disabled=true}
};
$('cancel').onclick=()=>{cancelled=true;$('cancel').disabled=true;status('กำลังหยุดหลังบันทึกแผ่นปัจจุบัน กรุณารอสักครู่...')};
$('openFolder').onclick=async()=>{try{await json('/api/open-folder')}catch(e){status(e.message,true)}};
$('closeApp').onclick=async()=>{if(running)return;try{await json('/api/close');for(const element of document.querySelectorAll('button,input,select'))element.disabled=true;status('ปิดโปรแกรมแผนที่แล้ว ปิดแท็บนี้ได้เลย หากต้องการเปิดใหม่ให้ดับเบิลคลิก map-downloader.bat')}catch(e){status(e.message,true)}};
try{config=await(await api('/api/config')).json();$('folder').value=config.output||'';if(config.initial){const p=config.initial;if(Number.isFinite(p.lat)&&Number.isFinite(p.lon))$('coordinates').value=`${p.lat}, ${p.lon}`;if(Number.isFinite(p.radius)&&p.radius>0){const radius=String(p.radius);if(!Array.from($('radius').options).some(o=>o.value===radius))$('radius').add(new Option(`${radius} กม. · กำหนดเอง`,radius));$('radius').value=radius}if(Number.isFinite(p.minZoom)&&Number.isFinite(p.maxZoom)){const profile=`${p.minZoom},${p.maxZoom}`;if(!Array.from($('profile').options).some(o=>o.value===profile))$('profile').add(new Option(`กำหนดเอง · ซูม ${p.minZoom}–${p.maxZoom}`,profile));$('profile').value=profile}}status('พร้อมใช้งาน เลือกพื้นที่และโฟลเดอร์ แล้วกดตรวจพื้นที่')}catch(e){status('เปิดเครื่องมือไม่สำเร็จ: '+e.message,true)}
refresh();
