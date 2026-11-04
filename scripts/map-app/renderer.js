import {VectorTile,Pbf} from './vendor/vector.js';

const palette={paper:'#f2efe3',wood:'#c8d8b4',grass:'#dfE8c6',crop:'#e7e6c8',sand:'#f0dfb5',rock:'#deddd7',urban:'#e6e1d7',industrial:'#e2dce0',building:'#d4ccc0',water:'#a8d2e2',waterLine:'#7fbacc',rail:'#99958c',boundary:'#b4a5b1',ink:'#263e32'};
export function decodeTile(bytes){return new VectorTile(new Pbf(new Uint8Array(bytes)))}
function selectedName(p){return String(p['name:th']||p.name||p.name_nonlatin||p['name:nonlatin']||p.name_en||p['name:en']||p.name_latin||'')}
function features(tile,name){const layer=tile.layers[name];if(!layer)return[];const result=[];for(let i=0;i<layer.length;i++){try{result.push(layer.feature(i))}catch{}}return result}
function labelFit(ctx,name,max){if(ctx.measureText(name).width<=max)return name;let a=Array.from(name);while(a.length&&ctx.measureText(a.join('')+'…').width>max)a.pop();while(a.length&&/[\u0e31\u0e34-\u0e3a\u0e47-\u0e4e]/u.test(a[a.length-1]))a.pop();return a.join('')+'…'}

export async function renderTile(tile,z,x,y,canvas=document.createElement('canvas')){
  await document.fonts.load('12px Sarabun');canvas.width=256;canvas.height=256;
  const ctx=canvas.getContext('2d',{alpha:false});ctx.fillStyle=palette.paper;ctx.fillRect(0,0,256,256);
  const sourceZ=Math.min(z,14),factor=2**(z-sourceZ),childX=x%factor,childY=y%factor;
  const transform=(p,extent)=>({x:p.x/extent*256*factor-childX*256,y:p.y/extent*256*factor-childY*256});
  function geometry(feature){return feature.loadGeometry().map(ring=>ring.map(p=>transform(p,feature.extent)))}
  function path(rings){ctx.beginPath();for(const ring of rings){if(!ring.length)continue;ctx.moveTo(ring[0].x,ring[0].y);for(let i=1;i<ring.length;i++)ctx.lineTo(ring[i].x,ring[i].y)}}
  function polygons(layer,color,filter=()=>true){for(const f of features(tile,layer)){if(f.type!==3||!filter(f.properties))continue;path(geometry(f));ctx.fillStyle=typeof color==='function'?color(f.properties):color;ctx.fill('evenodd')}}
  polygons('landcover',p=>p.class==='wood'?palette.wood:p.class==='grass'||p.class==='scrub'?palette.grass:p.class==='crop'||p.class==='farmland'?palette.crop:p.class==='sand'?palette.sand:p.class==='ice'?'#fafafa':palette.rock);
  polygons('landuse',p=>['park','cemetery','recreation_ground','village_green','grass','garden','forest','wood','pitch','golf_course'].includes(p.class)?palette.grass:['industrial','commercial','retail','railway','military'].includes(p.class)?palette.industrial:palette.urban);
  polygons('park',palette.grass);polygons('water',palette.water);
  ctx.lineCap='round';ctx.lineJoin='round';ctx.strokeStyle=palette.waterLine;
  for(const f of features(tile,'waterway')){path(geometry(f));ctx.lineWidth=f.properties.class==='river'?Math.max(1.6,(z-10)*.45):.7;ctx.stroke()}
  if(z>=13)polygons('building',palette.building);
  ctx.save();ctx.strokeStyle=palette.boundary;ctx.lineWidth=.8;ctx.setLineDash([4,3]);
  for(const f of features(tile,'boundary')){if(Number(f.properties.admin_level)>6)continue;path(geometry(f));ctx.stroke()}ctx.restore();
  const roadKinds={motorway:{w:5,color:'#ecc48c',edge:'#ceab7d',priority:9},trunk:{w:4.7,color:'#f0d29e',edge:'#c8ac84',priority:8},primary:{w:4.2,color:'#f2dfb5',edge:'#bdb09a',priority:7},secondary:{w:3.6,color:'#fff8df',edge:'#c2bba7',priority:6},tertiary:{w:3,color:'#fffcef',edge:'#c3bfb0',priority:5},minor:{w:2.4,color:'#fffdf3',edge:'#ccc5b5',priority:4},service:{w:1.7,color:'#fffdf4',edge:'#d6d0c4',priority:3},track:{w:1.1,color:'#a7936c',edge:null,priority:2,dash:[3,2]},path:{w:1,color:'#aa9477',edge:null,priority:1,dash:[2,2]},rail:{w:1,color:'#8c8d87',edge:null,priority:0,dash:[5,3]}};
  const roads=features(tile,'transportation').filter(f=>f.type===2).map(f=>({f,s:roadKinds[f.properties.class]||roadKinds.service})).sort((a,b)=>a.s.priority-b.s.priority);
  for(const {f,s} of roads){const g=geometry(f);let width=s.w*Math.min(1.35,Math.max(.5,.45+(z-10)*.13));
    if(f.properties.brunnel==='tunnel')ctx.globalAlpha=.55;
    if(s.edge){ctx.setLineDash([]);ctx.strokeStyle=s.edge;ctx.lineWidth=width+1.15;path(g);ctx.stroke()}
    ctx.setLineDash(s.dash||[]);ctx.strokeStyle=s.color;ctx.lineWidth=width;path(g);ctx.stroke();ctx.globalAlpha=1;
  }ctx.setLineDash([]);
  const occupied=[];
  function free(box){return !occupied.some(b=>box.x<b.x+b.w&&box.x+box.w>b.x&&box.y<b.y+b.h&&box.y+box.h>b.y)}
  function write(name,px,py,size=12,strong=false,angle=0){
    if(!name||px<7||px>249||py<10||py>228)return false;
    ctx.font=`${strong?'600 ':''}${size}px Sarabun, Tahoma, sans-serif`;name=labelFit(ctx,name,Math.min(224,2*Math.min(px-4,252-px)));
    if(!name||name==='…')return false;
    const tw=ctx.measureText(name).width;const h=size*1.6;
    const ww=Math.abs(Math.cos(angle))*tw+Math.abs(Math.sin(angle))*h;
    const hh=Math.abs(Math.sin(angle))*tw+Math.abs(Math.cos(angle))*h;
    const box={x:px-ww/2-3,y:py-hh/2-2,w:ww+6,h:hh+4};
    if(box.x<2||box.y<2||box.x+box.w>254||box.y+box.h>232||!free(box))return false;
    ctx.save();ctx.translate(px,py);ctx.rotate(angle);ctx.textAlign='center';ctx.textBaseline='middle';ctx.lineJoin='round';ctx.lineWidth=3;ctx.strokeStyle='#fffef5';ctx.strokeText(name,0,0);ctx.fillStyle=palette.ink;ctx.fillText(name,0,0);ctx.restore();occupied.push(box);return true;
  }
  const places=features(tile,'place').sort((a,b)=>Number(a.properties.rank||9)-Number(b.properties.rank||9));
  for(const f of places){const p=f.properties;if(!['city','town','state'].includes(p.class))continue;for(const ring of geometry(f))for(const pt of ring)write(selectedName(p),pt.x,pt.y,14,true)}
  if(z>=13){
    for(const f of features(tile,'transportation_name').sort((a,b)=>(roadKinds[b.properties.class]?.priority||0)-(roadKinds[a.properties.class]?.priority||0))){
      let name=selectedName(f.properties)||String(f.properties.ref||'');if(!name)continue;
      for(const ring of geometry(f)){
        let best=null;for(let i=1;i<ring.length;i++){const a=ring[i-1],b=ring[i],len=Math.hypot(b.x-a.x,b.y-a.y);if(len<34)continue;const x=(a.x+b.x)/2,y=(a.y+b.y)/2;if(x<8||x>248||y<12||y>226)continue;if(!best||len>best.len)best={a,b,len,x,y}}
        if(best){let angle=Math.atan2(best.b.y-best.a.y,best.b.x-best.a.x);if(angle>Math.PI/2)angle-=Math.PI;if(angle<-Math.PI/2)angle+=Math.PI;write(name,best.x,best.y,11,false,angle)}
      }
    }
  }
  for(const f of places){const p=f.properties;const cls=p.class||'';if(['city','town','state'].includes(cls))continue;if(z<11&&['neighbourhood','suburb','quarter','hamlet','isolated_dwelling'].includes(cls))continue;for(const ring of geometry(f))for(const pt of ring)write(selectedName(p),pt.x,pt.y,12)}
  if(z>=14){for(const f of features(tile,'poi')){const p=f.properties;if(!['hospital','clinic','police','fire_station','town_hall','water','drinking_water','railway','station','bus','fuel','supermarket','camp_site','peak'].includes(p.class)&&!['hospital','clinic','police','fire_station','drinking_water','camp_site'].includes(p.subclass))continue;for(const ring of geometry(f))for(const pt of ring)write(selectedName(p),pt.x,pt.y,10)}}
  // Every stored image carries credits, even when viewed outside this app.
  ctx.fillStyle='#fffdf0ed';ctx.fillRect(0,233,256,23);ctx.fillStyle='#344c3d';ctx.font='8px Sarabun, Tahoma, sans-serif';ctx.textAlign='left';ctx.textBaseline='alphabetic';ctx.fillText('© OpenStreetMap contributors',4,243);ctx.fillText('© OpenMapTiles · OpenFreeMap',4,253);
  return canvas;
}
export function jpegBlob(canvas){return new Promise((resolve,reject)=>canvas.toBlob(b=>b?resolve(b):reject(new Error('สร้างภาพ JPEG ไม่สำเร็จ')),'image/jpeg',.88))}
