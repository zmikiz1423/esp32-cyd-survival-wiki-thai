"""Build complete provincial JPEG map packs from a local Geofabrik OSM extract.

The 3000-image / 50-MB batches here are LOCAL write checkpoints. They do not
split or bypass the quotas of any remote tile service. Only the raw country
extract is downloaded. All tile generation happens on the user's computer.
"""
from __future__ import annotations
import argparse,hashlib,json,math,os,sqlite3,time,urllib.request,urllib.error,gc,re,shutil
from pathlib import Path
from functools import lru_cache
from concurrent.futures import ProcessPoolExecutor
from itertools import islice
from io import BytesIO
from contextlib import contextmanager
import osmium
import numpy as np
import shapely
from shapely.geometry import shape,box,Point,LineString,Polygon,mapping
from shapely import wkb
from shapely.ops import transform,unary_union
from shapely.prepared import prep
from PIL import Image,ImageDraw,ImageFilter,ImageChops
import uharfbuzz as hb
import freetype

HERE=Path(__file__).resolve().parent
VERSION=5
INDEX_VERSION=4  # Geometry schema unchanged; reuse the costly country index.
COUNTRY_URL='https://download.geofabrik.de/asia/thailand-latest.osm.pbf'
UA='SurvivalWiki-ThaiMaps/2.0 (local OSM map pack builder)'
NAMES='10:กรุงเทพมหานคร 11:สมุทรปราการ 12:นนทบุรี 13:ปทุมธานี 14:พระนครศรีอยุธยา 15:อ่างทอง 16:ลพบุรี 17:สิงห์บุรี 18:ชัยนาท 19:สระบุรี 20:ชลบุรี 21:ระยอง 22:จันทบุรี 23:ตราด 24:ฉะเชิงเทรา 25:ปราจีนบุรี 26:นครนายก 27:สระแก้ว 30:นครราชสีมา 31:บุรีรัมย์ 32:สุรินทร์ 33:ศรีสะเกษ 34:อุบลราชธานี 35:ยโสธร 36:ชัยภูมิ 37:อำนาจเจริญ 38:บึงกาฬ 39:หนองบัวลำภู 40:ขอนแก่น 41:อุดรธานี 42:เลย 43:หนองคาย 44:มหาสารคาม 45:ร้อยเอ็ด 46:กาฬสินธุ์ 47:นครพนม 48:มุกดาหาร 49:สกลนคร 50:เชียงใหม่ 51:ลำพูน 52:ลำปาง 53:อุตรดิตถ์ 54:แพร่ 55:น่าน 56:พะเยา 57:เชียงราย 58:แม่ฮ่องสอน 60:นครสวรรค์ 61:อุทัยธานี 62:กำแพงเพชร 63:ตาก 64:สุโขทัย 65:พิษณุโลก 66:พิจิตร 67:เพชรบูรณ์ 70:ราชบุรี 71:กาญจนบุรี 72:สุพรรณบุรี 73:นครปฐม 74:สมุทรสาคร 75:สมุทรสงคราม 76:เพชรบุรี 77:ประจวบคีรีขันธ์ 80:นครศรีธรรมราช 81:กระบี่ 82:พังงา 83:ภูเก็ต 84:สุราษฎร์ธานี 85:ระนอง 86:ชุมพร 90:สงขลา 91:สตูล 92:ตรัง 93:พัทลุง 94:ปัตตานี 95:ยะลา 96:นราธิวาส'
THAI=dict(('TH-'+s.split(':')[0],s.split(':')[1]) for s in NAMES.split())
# ISO3166-2:TH: Sakon Nakhon=47, Nakhon Phanom=48, Mukdahan=49.
THAI.update({'TH-47':'สกลนคร','TH-48':'นครพนม','TH-49':'มุกดาหาร'})

class BuildCancelled(Exception): pass

def atomic(path,data):
    path=Path(path);path.parent.mkdir(parents=True,exist_ok=True)
    tmp=path.with_name(path.name+'.part');tmp.write_bytes(data);os.replace(tmp,path)

def write_json(path,value): atomic(path,(json.dumps(value,ensure_ascii=False,indent=2)+'\n').encode())

@lru_cache(maxsize=1)
def _provinces():
    data=json.loads((HERE/'data/provinces.geojson').read_text('utf-8'))
    capitals=json.loads((HERE/'data/capitals.json').read_text('utf-8')) if (HERE/'data/capitals.json').exists() else {}
    result={}
    for f in data['features']:
        p=f['properties'];g=shape(f['geometry']);id=p['shapeISO']
        if not g.is_valid:g=g.buffer(0)
        c=g.representative_point();center=capitals.get(id,[c.y,c.x])
        if not g.buffer(.01).covers(Point(center[1],center[0])):center=[c.y,c.x]
        result[id]={'id':id,'name':THAI.get(id,p['shapeName']),'nameEn':p['shapeName'],'bounds':list(g.bounds),'center':center,'shape':g}
    assert len(result)==77,'The shipped boundary file must contain 77 provinces'
    return result

def list_provinces():return [{k:v for k,v in p.items() if k!='shape'} for p in sorted(_provinces().values(),key=lambda p:p['name'])]

@lru_cache(maxsize=1)
def _country_land():
    return unary_union([p['shape'] for p in _provinces().values()])

@lru_cache(maxsize=1)
def _prepared_country_land():return prep(_country_land())

def _province(id):
    try:return _provinces()[id]
    except KeyError:raise ValueError('ไม่พบจังหวัดที่เลือก')

def tx(lon,z):return int(math.floor((lon+180)/360*(1<<z)))
def ty(lat,z):
    r=math.radians(lat);return int(math.floor((1-math.asinh(math.tan(r))/math.pi)/2*(1<<z)))
def lon(x,z):return x/(1<<z)*360-180
def lat(y,z):return math.degrees(math.atan(math.sinh(math.pi*(1-2*y/(1<<z)))))
def tile_bounds(z,x,y):return lon(x,z),lat(y+1,z),lon(x+1,z),lat(y,z)

def _tiles(g,min_zoom,max_zoom):
    # A 1-km approximate safety margin includes roads just across the boundary.
    region=g.buffer(.01);shapely.prepare(region)
    west,south,east,north=region.bounds
    for z in range(min_zoom,max_zoom+1):
        # Vectorized intersection avoids a slow per-cell Python/GEOS loop for
        # large provinces at zoom 16. Preserve canonical x-then-y ordering.
        xs=np.arange(tx(west,z),tx(east,z)+1,dtype=np.int64)
        ys=np.arange(ty(north,z),ty(south,z)+1,dtype=np.int64)
        for start in range(0,len(xs),64):
            xx=np.repeat(xs[start:start+64],len(ys));yy=np.tile(ys,len(xs[start:start+64]));n=1<<z
            boxes=shapely.box(xx/n*360-180,np.degrees(np.arctan(np.sinh(np.pi*(1-2*(yy+1)/n)))),(xx+1)/n*360-180,np.degrees(np.arctan(np.sinh(np.pi*(1-2*yy/n)))))
            for x,y in zip(xx[shapely.intersects(region,boxes)],yy[shapely.intersects(region,boxes)]):yield z,int(x),int(y)

def plan(province_id,min_zoom=8,max_zoom=14):
    if not isinstance(min_zoom,int) or not isinstance(max_zoom,int) or not 6<=min_zoom<=max_zoom<=16:raise ValueError('เลือกระดับซูม 6 ถึง 16')
    p=_province(province_id);counts={z:0 for z in range(min_zoom,max_zoom+1)}
    for z,x,y in _tiles(p['shape'],min_zoom,max_zoom):counts[z]+=1
    count=sum(counts.values())
    return {'province':{k:v for k,v in p.items() if k!='shape'},'count':count,'estimatedBytes':count*28000,'bounds':p['bounds'],'center':p['center'],'minZoom':min_zoom,'maxZoom':max_zoom,'zoomCounts':counts,'batches':max(math.ceil(count/3000),math.ceil(count*28000/50000000)),'geometry':mapping(p['shape'].simplify(.003,preserve_topology=True)),'boundaryYear':2017,'marginKmApprox':1,'provider':'local-osm'}

def download_country(cache,progress,cancelled):
    """Resumable raw-file download; snapshot ETag + official MD5 verification."""
    cache=Path(cache);cache.mkdir(parents=True,exist_ok=True)
    path=cache/'thailand-latest.osm.pbf';meta=cache/'country-source.json'
    if path.exists() and meta.exists():
        saved=json.loads(meta.read_text('utf-8'))
        if saved.get('verified') and saved.get('size')==path.stat().st_size and saved.get('mtimeNs')==path.stat().st_mtime_ns:
            return path # A verified snapshot is usable even without Internet.
    def fetch(request):return urllib.request.urlopen(request,timeout=45)
    headers={'User-Agent':UA}
    # Some transparent proxies remove Content-Length from HEAD. A one-byte
    # range gives the total snapshot size without requesting the entire file.
    download_url=COUNTRY_URL+'?download=1'
    with fetch(urllib.request.Request(download_url,headers={**headers,'Range':'bytes=0-0'})) as r:
        match=re.fullmatch(r'bytes 0-0/(\d+)',r.headers.get('Content-Range',''))
        if r.status!=206 or not match:raise ValueError('แหล่งข้อมูลไม่รองรับการดาวน์โหลดแบบทำต่อ กรุณาลองใหม่ภายหลัง')
        total=int(match[1]);etag=r.headers.get('ETag','');modified=r.headers.get('Last-Modified','');r.read(1)
    with fetch(urllib.request.Request(COUNTRY_URL+'.md5',headers=headers)) as r:expected=r.read(4096).decode('ascii').split()[0].lower()
    if not re.fullmatch('[0-9a-f]{32}',expected):raise ValueError('รูปแบบ checksum ของข้อมูลต้นทางไม่ถูกต้อง')
    def digest(file):
        h=hashlib.md5()
        with file.open('rb') as f:
            while chunk:=f.read(1024*1024):
                if cancelled():raise BuildCancelled()
                h.update(chunk)
        return h.hexdigest()
    if path.exists() and path.stat().st_size==total:
        if meta.exists():
            saved=json.loads(meta.read_text('utf-8'))
            if saved.get('md5')==expected and saved.get('verified') and saved.get('mtimeNs')==path.stat().st_mtime_ns:return path
        if digest(path)==expected:
            write_json(meta,{'url':COUNTRY_URL,'size':total,'etag':etag,'lastModified':modified,'md5':expected,'verified':True,'mtimeNs':path.stat().st_mtime_ns});return path
    tmp=cache/'thailand-latest.osm.pbf.part';resume=cache/'country-resume.json'
    snapshot={'etag':etag,'size':total,'md5':expected}
    if not resume.exists() or json.loads(resume.read_text('utf8'))!=snapshot:
        if tmp.exists():tmp.unlink()
        write_json(resume,snapshot)
    start=tmp.stat().st_size if tmp.exists() else 0
    if start>total:tmp.unlink();start=0
    with tmp.open('ab') as f:
        while start<total:
            if cancelled():raise BuildCancelled('ยกเลิกแล้ว เก็บข้อมูลไว้เพื่อทำต่อ')
            end=min(start+1024*1024,total)-1;last_error=None
            for attempt in range(3):
                try:
                    req=urllib.request.Request(download_url,headers={**headers,'Range':f'bytes={start}-{end}','If-Range':etag})
                    with fetch(req) as r:
                        if r.status!=206 or r.headers.get('Content-Range')!=f'bytes {start}-{end}/{total}' or (etag and r.headers.get('ETag')!=etag):raise ValueError('ข้อมูลต้นทางเปลี่ยนระหว่างดาวน์โหลด กรุณาเริ่มใหม่')
                        chunk=r.read(end-start+2)
                    if len(chunk)!=end-start+1:raise ValueError('ข้อมูลที่ดาวน์โหลดไม่ครบช่วง')
                    f.write(chunk);f.flush();start+=len(chunk);break
                except (OSError,ValueError) as e:
                    last_error=e
                    if cancelled():raise BuildCancelled()
                    if attempt==2:raise
            progress({'stage':'download','message':'กำลังดาวน์โหลดข้อมูลประเทศไทย (ทำต่อจากส่วนที่มีอยู่แล้วได้)','done':start,'total':total,'bytes':start})
    progress({'stage':'download','message':'กำลังตรวจ checksum ข้อมูลประเทศไทย','done':total,'total':total,'bytes':total})
    if digest(tmp)!=expected:raise ValueError('Checksum ข้อมูลประเทศไทยไม่ตรง กรุณาลบไฟล์ดาวน์โหลดชั่วคราวแล้วเริ่มใหม่')
    os.replace(tmp,path);write_json(meta,{'url':COUNTRY_URL,'size':total,'etag':etag,'lastModified':modified,'md5':expected,'verified':True,'mtimeNs':path.stat().st_mtime_ns})
    return path

KEEP=('name:th','name','name:en','ref','highway','railway','waterway','natural','landuse','leisure','building','amenity','tourism','shop','place','aeroway','route','surface','bridge','tunnel','ele')
def tags_of(obj):return {k:v for k,v in obj.tags if k in KEEP}
def wanted(t):return any(k in t for k in ('highway','railway','waterway','natural','landuse','leisure','building','amenity','tourism','shop','place','aeroway')) or t.get('route')=='ferry'
def name(t):return (t.get('name:th') or t.get('name') or t.get('name:en') or t.get('ref') or '').replace('\t',' ').replace('\n',' ').replace('\r',' ')[:180]
def feature_min_zoom(t,kind):
    if 'building' in t:return 13
    if kind=='point':return 7 if t.get('place') in ('city','town') else 11 if 'place' in t else 14
    if 'highway' in t:return {'motorway':6,'trunk':7,'primary':8,'secondary':9,'tertiary':10,'residential':11,'unclassified':11,'service':13,'track':13,'path':14,'footway':14,'steps':14,'cycleway':13}.get(t['highway'],12)
    if 'waterway' in t:return 8 if t['waterway']=='river' else 11
    if 'railway' in t:return 9
    if kind=='area':return 8 if any(k in t for k in ('natural','landuse')) else 11
    return 11

def make_index(pbf,cache,province,progress,cancelled):
    p=_province(province);g=p['shape'].buffer(.025);pbf=Path(pbf)
    key=hashlib.sha256((province+str(INDEX_VERSION)+str(pbf.stat().st_size)+str(pbf.stat().st_mtime_ns)).encode()).hexdigest()[:20]
    target=Path(cache)/('features-'+key+'.sqlite')
    if target.exists():
        try:
            c=sqlite3.connect(target);ok=c.execute("SELECT value FROM metadata WHERE key='complete'").fetchone();c.close()
            if ok and ok[0]=='1':return target
        except sqlite3.Error:pass
    tmp=target.with_suffix('.sqlite.part')
    if tmp.exists():tmp.unlink()
    con=sqlite3.connect(tmp);con.executescript('PRAGMA journal_mode=OFF; PRAGMA synchronous=OFF; CREATE TABLE features(id INTEGER PRIMARY KEY, osm_id TEXT, geom BLOB, tags TEXT, kind TEXT,min_zoom INTEGER); CREATE VIRTUAL TABLE spatial USING rtree(id,minx,maxx,miny,maxy); CREATE TABLE metadata(key TEXT PRIMARY KEY,value TEXT);')
    west,south,east,north=g.bounds;region=prep(g);factory=osmium.geom.WKBFactory()
    class Handler(osmium.SimpleHandler):
        n=0;added=0;last=0
        def tick(self):
            self.n+=1
            if self.n%2048==0 and cancelled():raise BuildCancelled('หยุดสร้างดัชนีแล้ว เริ่มใหม่ได้โดยไม่ดาวน์โหลดข้อมูลประเทศซ้ำ')
            if self.n%100000==0:
                if cancelled():raise BuildCancelled('หยุดสร้างดัชนีแล้ว เริ่มใหม่ได้โดยไม่ดาวน์โหลดข้อมูลประเทศซ้ำ')
                progress({'stage':'index','message':f'กำลังอ่านข้อมูล OSM และสร้างดัชนี {self.n:,} รายการ · เก็บแล้ว {self.added:,}','done':self.n,'total':0,'features':self.added})
                con.commit()
        def store(self,id,geom,t,kind):
            if geom.is_empty or not geom.intersects(g):return
            b=geom.bounds
            cur=con.execute('INSERT INTO features(osm_id,geom,tags,kind,min_zoom) VALUES(?,?,?,?,?)',(id,sqlite3.Binary(geom.wkb),json.dumps(t,ensure_ascii=False,separators=(',',':')),kind,feature_min_zoom(t,kind)));fid=cur.lastrowid
            con.execute('INSERT INTO spatial VALUES(?,?,?,?,?)',(fid,b[0],b[2],b[1],b[3]));self.added+=1
        def node(self,n):
            self.tick()
            if not n.location.valid():return
            x,y=n.location.lon,n.location.lat
            if not west<=x<=east or not south<=y<=north:return
            t=tags_of(n)
            if t and wanted(t) and region.covers(Point(x,y)):self.store('n'+str(n.id),Point(x,y),t,'point')
        def way(self,w):
            self.tick();t=tags_of(w)
            if not wanted(t):return
            try:
                coords=[(n.lon,n.lat) for n in w.nodes]
                if len(coords)<2:return
                b=LineString(coords).bounds
                if b[2]<west or b[0]>east or b[3]<south or b[1]>north:return
                area=coords[0]==coords[-1] and len(coords)>=4 and not any(k in t for k in ('highway','railway','waterway'))
                geom=Polygon(coords) if area else LineString(coords)
                if not geom.is_valid:geom=geom.buffer(0) if area else geom
                self.store('w'+str(w.id),geom,t,'area' if area else 'line')
            except (osmium.InvalidLocationError,ValueError):pass
        def area(self,a):
            self.tick()
            if a.from_way():return # Closed ways were already indexed.
            t=tags_of(a)
            if not wanted(t):return
            try:
                geom=wkb.loads(factory.create_multipolygon(a),hex=True)
                if not geom.is_valid:geom=geom.buffer(0)
                self.store('r'+str(a.orig_id()),geom,t,'area')
            except (osmium.InvalidLocationError,RuntimeError,ValueError):pass
    handler=Handler();nodefile=Path(cache)/('nodes-'+key+'.idx')
    try:
        progress({'stage':'index','message':'กำลังสร้างดัชนีถนน อาคาร แหล่งน้ำ และสถานที่จากข้อมูลประเทศไทย','done':0,'total':0})
        handler.apply_file(str(pbf),locations=True,idx='sparse_file_array,'+str(nodefile))
        if cancelled():raise BuildCancelled()
        con.execute('INSERT INTO metadata VALUES(?,?)',('complete','1'));con.execute('INSERT INTO metadata VALUES(?,?)',('features',str(handler.added)));con.commit();con.close();os.replace(tmp,target)
    except BaseException:
        con.close();raise
    finally:
        del handler;gc.collect()
        try:
            if nodefile.exists():nodefile.unlink()
        except PermissionError:pass # libosmium can retain the mapping until process exit on Windows.
    return target

class ShapedText:
    """HarfBuzz GSUB/GPOS + FreeType rasterization, including Thai mark stacking."""
    def __init__(self,path):
        self.data=Path(path).read_bytes();self.face=hb.Face(self.data);self.ft=freetype.Face(str(path))
    @lru_cache(maxsize=4096)
    def mask(self,text,size):
        font=hb.Font(self.face);font.scale=(size*64,size*64);hb.ot_font_set_funcs(font)
        buf=hb.Buffer();buf.add_str(text);buf.guess_segment_properties();hb.shape(font,buf)
        self.ft.set_pixel_sizes(0,size);glyphs=[];pen=0;minx=0;miny=-size*2;maxx=0;maxy=size
        for info,pos in zip(buf.glyph_infos,buf.glyph_positions):
            self.ft.load_glyph(info.codepoint,freetype.FT_LOAD_RENDER|freetype.FT_LOAD_TARGET_NORMAL)
            slot=self.ft.glyph;b=slot.bitmap
            x=round((pen+pos.x_offset)/64)+slot.bitmap_left;y=-round(pos.y_offset/64)-slot.bitmap_top
            if b.width and b.rows:
                raw=bytes(b.buffer)
                if b.pitch!=b.width:raw=b''.join(raw[i*b.pitch:i*b.pitch+b.width] for i in range(b.rows))
                glyphs.append((x,y,Image.frombytes('L',(b.width,b.rows),raw)))
                minx=min(minx,x);maxx=max(maxx,x+b.width);miny=min(miny,y);maxy=max(maxy,y+b.rows)
            pen+=pos.x_advance
        maxx=max(maxx,math.ceil(pen/64));img=Image.new('L',(max(1,maxx-minx),max(1,maxy-miny)))
        for x,y,g in glyphs:
            xx,yy=x-minx,y-miny
            img.paste(ImageChops.lighter(img.crop((xx,yy,xx+g.width,yy+g.height)),g),(xx,yy))
        bb=img.getbbox();return img.crop(bb) if bb else Image.new('L',(1,1))

def _xy(lon_,lat_,z,x,y):
    r=math.radians(lat_);return ((lon_+180)/360*(1<<z)-x)*256,((1-math.asinh(math.tan(r))/math.pi)/2*(1<<z)-y)*256

ROAD={'motorway':(6,'#ebba7c'),'trunk':(5,'#edc58c'),'primary':(4.5,'#f1d3a0'),'secondary':(4,'#fff0c8'),'tertiary':(3.3,'#fff7df'),'residential':(2.7,'#ffffff'),'unclassified':(2.5,'#ffffff'),'service':(1.7,'#ffffff'),'track':(1.3,'#a38b60'),'path':(1.1,'#997652'),'footway':(1.1,'#aa795f'),'steps':(1.1,'#aa795f'),'cycleway':(1.3,'#6c9d85')}

def render_tile(con,z,x,y,text,province_land):
    # Supersampling improves road/Thai glyph legibility while preserving baseline JPEG.
    S=2;im=Image.new('RGB',(256*S,256*S),'#a9d3df');draw=ImageDraw.Draw(im)
    def pts(coords):return [tuple(v*S for v in _xy(a,b,z,x,y)) for a,b,*_ in coords]
    def polygon(g,color):
        if g.geom_type=='MultiPolygon':
            for sub in g.geoms:polygon(sub,color)
        elif g.geom_type=='Polygon':
            if not g.interiors:draw.polygon(pts(g.exterior.coords),fill=color)
            else:
                mask=Image.new('L',im.size);md=ImageDraw.Draw(mask)
                md.polygon(pts(g.exterior.coords),fill=255)
                for ring in g.interiors:md.polygon(pts(ring.coords),fill=0)
                im.paste(color,(0,0),mask)
    # Province boundary follows coastlines/islands; outside the mask is water.
    # Features from the 2.5-km index margin still draw nearby roads/landuse.
    tilebox=box(*tile_bounds(z,x,y));land=_prepared_country_land()
    if land.covers(tilebox):draw.rectangle((0,0,512,512),fill='#f0eee2')
    elif land.intersects(tilebox):polygon(province_land.intersection(tilebox),'#f0eee2')
    w,s,e,n=tile_bounds(z,x,y);margin=(e-w)*.10
    rows=con.execute('SELECT f.osm_id,f.geom,f.tags,f.kind FROM spatial s JOIN features f ON f.id=s.id WHERE f.min_zoom<=? AND s.minx<=? AND s.maxx>=? AND s.miny<=? AND s.maxy>=?',(z,e+margin,w-margin,n+margin,s-margin)).fetchall()
    features=[]
    for oid,geom,tags,kind in rows:
        t=json.loads(tags)
        if z<13 and 'building' in t:continue
        if z<11 and t.get('highway') in ('service','path','footway','track','steps'):continue
        if z<10 and t.get('highway') in ('residential','unclassified'):continue
        g=wkb.loads(geom)
        if z<14 and kind!='point':g=g.simplify((e-w)/512,preserve_topology=True)
        features.append((oid,g,t,kind))
    for oid,g,t,kind in features:
        if kind!='area':continue
        natural=t.get('natural');land=t.get('landuse');leisure=t.get('leisure')
        color='#d4ccc0' if 'building' in t else '#a9d3df' if natural in ('water','bay','wetland') or land in ('reservoir','basin') else '#c3d8ad' if natural in ('wood','scrub') or land=='forest' else '#dbe6bf' if land in ('farmland','orchard','plantation','meadow') else '#cce1b6' if leisure in ('park','garden','golf_course','pitch') else '#e4dfd8' if land in ('residential','commercial','industrial','retail') else '#e8e5cd'
        polygon(g,color)
    def lines(g):
        if g.geom_type in ('MultiLineString','GeometryCollection'):
            for sub in g.geoms:yield from lines(sub)
        elif g.geom_type=='LineString':yield pts(g.coords)
        elif g.geom_type=='Polygon':yield pts(g.exterior.coords)
    ordered=sorted(features,key=lambda f:list(ROAD).index(f[2].get('highway')) if f[2].get('highway') in ROAD else 20,reverse=True)
    labels=[]
    for oid,g,t,kind in ordered:
        if kind=='line':
            h=t.get('highway');r=t.get('railway');water=t.get('waterway');ferry=t.get('route')=='ferry'
            width,color=ROAD.get(h,(1.3,'#777b7b')) if not water else (2.2 if water=='river' else 1,'#83bdcf')
            if ferry:color='#5b99ad';width=1
            width=max(1,round(width*max(.6,min(1.5,(z-7)/7))*S))
            for line in lines(g):
                if len(line)<2:continue
                if h and h not in ('track','path','footway','steps','cycleway'):draw.line(line,fill='#c2b9a7',width=width+2,joint='curve')
                draw.line(line,fill=color,width=width,joint='curve')
            if z>=13 and h and name(t) and g.length>(e-w)*.15:labels.append((3,name(t),g.interpolate(.5,normalized=True),10))
        if name(t):
            if 'place' in t:
                place=t['place'];priority=0 if place in ('city','town') else 1
                if z>=11 or priority==0:labels.append((priority,name(t),g.representative_point(),14 if priority==0 else 12))
            elif z>=14 and any(k in t for k in ('amenity','tourism','shop','natural')):
                if t.get('natural') not in ('water','wood','scrub','coastline'):labels.append((2,name(t),g.representative_point(),10))
    occupied=[]
    # Small fixed symbols remain visible even when a POI has no name.
    if z>=14:
        for oid,g,t,kind in features:
            category=t.get('amenity') or t.get('tourism')
            if not category or kind=='line':continue
            p=g.representative_point();px,py=_xy(p.x,p.y,z,x,y);px*=S;py*=S
            if not 3*S<px<253*S or not 3*S<py<239*S:continue
            color='#bf5151' if category in ('hospital','clinic','doctors') else '#477ba8' if category in ('drinking_water','water_point','toilets') else '#87629e' if category=='place_of_worship' else '#6c8a54' if category in ('camp_site','picnic_site') else '#867355'
            draw.ellipse((px-2*S,py-2*S,px+2*S,py+2*S),fill=color,outline='#fffef1',width=S)
    for priority,label,p,size in sorted(labels,key=lambda a:a[0]):
        px,py=_xy(p.x,p.y,z,x,y);px*=S;py*=S
        if not 6*S<px<250*S or not 8*S<py<232*S:continue
        if len(label)>70:label=label[:69]+'…'
        mask=text.mask(label,size*S);tw,th=mask.size
        if tw>238*S:continue
        bx=round(px-tw/2);by=round(py-th/2);bb=(bx-3*S,by-2*S,bx+tw+3*S,by+th+2*S)
        if bb[0]<2*S or bb[2]>254*S or bb[1]<2*S or bb[3]>233*S:continue
        if any(bb[0]<b[2] and bb[2]>b[0] and bb[1]<b[3] and bb[3]>b[1] for b in occupied):continue
        halo=mask.filter(ImageFilter.MaxFilter(5));im.paste('#fffef3',(bx,by),halo);im.paste('#2d423b',(bx,by),mask);occupied.append(bb)
        if priority==2:draw.ellipse((px-2*S,py+th/2+S,px+2*S,py+th/2+5*S),fill='#936748')
    im=im.resize((256,256),Image.Resampling.LANCZOS)
    # Credits belong to the viewer/pack, not a painted stripe replacing map
    # pixels on every tile. Both CYD and phone retain persistent attribution.
    return im

def valid_jpeg(path):
    try:
        with Image.open(path) as im:
            if im.format!='JPEG' or im.size!=(256,256) or im.info.get('progressive'):return False
            im.load();return True
    except (OSError,ValueError):return False

def _init_renderer(index,font):
    global _render_con,_render_text
    _render_con=sqlite3.connect(index);_render_text=ShapedText(font)

def _render_worker(z,x,y):
    image=render_tile(_render_con,z,x,y,_render_text,_country_land());out=BytesIO()
    image.save(out,'JPEG',quality=88,progressive=False,optimize=True,comment=b'(c) OpenStreetMap contributors; ODbL; https://www.openstreetmap.org/copyright')
    return out.getvalue()

def prepared_images(rows,index,font,output,cancelled):
    """Keep at most eight tiles in flight; never queue a whole province in RAM.

    Workers read independent SQLite connections. Only the coordinator writes
    files and commits checkpoints, so cancelling cannot activate partial maps.
    """
    workers=min(4,max(1,(os.cpu_count() or 2)//2))
    pool=ProcessPoolExecutor(max_workers=workers,initializer=_init_renderer,initargs=(str(index),str(font)))
    iterator=iter(rows)
    try:
        while group:=list(islice(iterator,workers*2)):
            if cancelled():raise BuildCancelled('หยุดแล้ว เก็บภาพที่สมบูรณ์ไว้เพื่อทำต่อ')
            futures={}
            for z,x,y,_ in group:
                path=output/str(z)/str(x)/(str(y)+'.jpg')
                if not valid_jpeg(path):futures[z,x,y]=pool.submit(_render_worker,z,x,y)
            for z,x,y,_ in group:
                if cancelled():raise BuildCancelled('หยุดแล้ว เก็บภาพที่สมบูรณ์ไว้เพื่อทำต่อ')
                path=output/str(z)/str(x)/(str(y)+'.jpg')
                if (z,x,y) in futures:
                    data=futures[z,x,y].result()
                    if cancelled():raise BuildCancelled()
                    atomic(path,data)
                yield z,x,y,path.stat().st_size
    finally:pool.shutdown(wait=True,cancel_futures=True)

@contextmanager
def exclusive_build(cache,output):
    handles=[]
    try:
        for folder,name_ in ((Path(cache),'build.lock'),(Path(output),'.map-builder.lock')):
            folder.mkdir(parents=True,exist_ok=True);f=(folder/name_).open('a+b')
            if f.seek(0,2)==0:f.write(b'0');f.flush()
            f.seek(0)
            try:
                if os.name=='nt':
                    import msvcrt
                    msvcrt.locking(f.fileno(),msvcrt.LK_NBLCK,1)
                else:
                    import fcntl
                    fcntl.flock(f.fileno(),fcntl.LOCK_EX|fcntl.LOCK_NB)
            except OSError:
                f.close();raise ValueError('มีงานอื่นกำลังใช้แคชหรือโฟลเดอร์นี้อยู่ กรุณารอให้งานนั้นเสร็จหรือหยุดก่อน') from None
            handles.append(f)
        yield
    finally:
        for f in reversed(handles):
            f.seek(0)
            if os.name=='nt':
                import msvcrt
                msvcrt.locking(f.fileno(),msvcrt.LK_UNLCK,1)
            else:
                import fcntl
                fcntl.flock(f.fileno(),fcntl.LOCK_UN)
            f.close()

def build(province_id,output,cache_dir,min_zoom=8,max_zoom=14,progress=None,cancelled=None,pbf_path=None,font_path=None):
    with exclusive_build(cache_dir,output):
        return _build(province_id,output,cache_dir,min_zoom,max_zoom,progress,cancelled,pbf_path,font_path)

def _build(province_id,output,cache_dir,min_zoom=8,max_zoom=14,progress=None,cancelled=None,pbf_path=None,font_path=None):
    progress=progress or (lambda p:None);cancelled=cancelled or (lambda:False)
    output=Path(output).resolve();cache=Path(cache_dir).resolve();cache.mkdir(parents=True,exist_ok=True)
    p=_province(province_id);spec=plan(province_id,min_zoom,max_zoom)
    if not font_path:font_path=HERE.parents[1]/'sdcard/web/Sarabun-Regular.ttf'
    if not Path(font_path).is_file():raise ValueError('ไม่พบฟอนต์ Sarabun ในชุดโปรแกรม กรุณาแตก ZIP ทั้งหมดก่อน')
    source_file=output/'source.json'
    if output.exists() and any(output.iterdir()):
        if source_file.exists():
            source=json.loads(source_file.read_text('utf-8-sig'))
            if source.get('provider')!='local-osm' or source.get('rendererVersion')!=VERSION:raise ValueError('โฟลเดอร์นี้มีแผนที่คนละรูปแบบ กรุณาเลือกโฟลเดอร์ใหม่เพื่อรักษาแผนที่เดิม')
            if source.get('province')!=province_id:raise ValueError('โฟลเดอร์นี้เป็นชุดแผนที่ของจังหวัดอื่น กรุณาเลือกโฟลเดอร์ใหม่สำหรับจังหวัดนี้ เพื่อไม่ให้ภาพซ้อนที่มีรายละเอียดไม่ครบปนกัน')
        elif any(output.rglob('*.jpg')):raise ValueError('โฟลเดอร์นี้มีแผนที่เดิมที่ไม่ระบุแหล่ง กรุณาเลือกโฟลเดอร์ใหม่')
    output.mkdir(parents=True,exist_ok=True)
    if shutil.disk_usage(output).free<spec['estimatedBytes']*1.2+10000000:raise ValueError('พื้นที่ปลายทางอาจไม่พอสำหรับชุดแผนที่ กรุณาเพิ่มพื้นที่ว่างหรือเลือกระดับซูมต่ำลง')
    if not pbf_path and shutil.disk_usage(cache).free<3*1024**3:raise ValueError('การเตรียมข้อมูลประเทศและดัชนีต้องมีพื้นที่ว่างในไดรฟ์แคชอย่างน้อย 3 GB')
    progress({'stage':'plan','message':'ตรวจพื้นที่จังหวัดครบทุกส่วน รวมเกาะและขอบพื้นที่','done':0,'total':spec['count']})
    country=Path(pbf_path) if pbf_path else download_country(cache,progress,cancelled)
    progress({'stage':'verify-source','message':'ตรวจลายนิ้วมือข้อมูลต้นทางและฟอนต์ เพื่อป้องกันภาพคนละชุดปนกัน','done':0,'total':0})
    digest=hashlib.sha256()
    with country.open('rb') as file:
        while chunk:=file.read(2*1024*1024):
            if cancelled():raise BuildCancelled()
            digest.update(chunk)
    dataset_hash=digest.hexdigest();font_hash=hashlib.sha256(Path(font_path).read_bytes()).hexdigest()
    boundary_hash=hashlib.sha256((HERE/'data/provinces.geojson').read_bytes()).hexdigest()
    fingerprint=hashlib.sha256(f'{VERSION}-{dataset_hash}-{font_hash}-{boundary_hash}'.encode()).hexdigest()
    if source_file.exists():
        previous=json.loads(source_file.read_text('utf-8-sig'))
        if previous.get('fingerprint')!=fingerprint:raise ValueError('ข้อมูลแผนที่หรือฟอนต์เปลี่ยนจากชุดที่บันทึกไว้ กรุณาเลือกโฟลเดอร์ใหม่เพื่อไม่ให้ภาพเก่าและใหม่ปนกัน')
    index=make_index(country,cache,province_id,progress,cancelled)
    if cancelled():raise BuildCancelled()
    text=ShapedText(font_path);con=sqlite3.connect(index)
    source={'provider':'local-osm','rendererVersion':VERSION,'fingerprint':fingerprint,'datasetSha256':dataset_hash,'fontSha256':font_hash,'boundarySha256':boundary_hash,'attribution':'© OpenStreetMap contributors','data':'https://www.openstreetmap.org/copyright','service':'https://download.geofabrik.de/asia/thailand.html','boundarySource':'geoBoundaries gbOpen THA ADM1 / OSM Wambacher (2017)','province':province_id,'countryBytes':country.stat().st_size,'createdUtc':time.time(),'complete':False}
    if not source_file.exists():write_json(source_file,source)
    jobid=hashlib.sha256(f'{province_id}-{min_zoom}-{max_zoom}-{VERSION}'.encode()).hexdigest()[:20]
    ledger_path=output/('job-'+jobid+'.sqlite');ledger=sqlite3.connect(ledger_path)
    write_json(output/'working-job.json',{'job':jobid,'province':province_id,'complete':False,'fingerprint':fingerprint})
    ledger.executescript('CREATE TABLE IF NOT EXISTS tiles(z INTEGER,x INTEGER,y INTEGER,bytes INTEGER DEFAULT 0,PRIMARY KEY(z,x,y)); CREATE TABLE IF NOT EXISTS batches(id INTEGER PRIMARY KEY,tiles INTEGER,bytes INTEGER);')
    if not ledger.execute('SELECT COUNT(*) FROM tiles').fetchone()[0]:
        ledger.executemany('INSERT INTO tiles(z,x,y) VALUES(?,?,?)',_tiles(p['shape'],min_zoom,max_zoom));ledger.commit()
    done=0;size=0;batch=1;batch_tiles=0;batch_bytes=0;last=0;missing=0
    try:
        # Export names as a bounded-line streaming index consumable by firmware.
        pois=[];seen=set()
        for oid,geom,tags in con.execute("SELECT osm_id,geom,tags FROM features WHERE kind='point' OR kind='area'"):
            t=json.loads(tags);label=name(t)
            if not label or not any(k in t for k in ('amenity','tourism','place','shop')):continue
            pt=wkb.loads(geom).representative_point();category=t.get('amenity') or t.get('tourism') or t.get('place') or t.get('shop') or ''
            key=(label,round(pt.x,4),round(pt.y,4))
            if key in seen:continue
            seen.add(key);pois.append(f'{oid}\t{label}\t{pt.y:.7f}\t{pt.x:.7f}\t{category}\n')
        pending_pois=''.join(pois).encode()
        rows=ledger.execute('SELECT z,x,y,bytes FROM tiles ORDER BY z,x,y')
        for z,x,y,n in prepared_images(rows,index,font_path,output,cancelled):
            if cancelled():raise BuildCancelled('หยุดแล้ว แผนที่ที่สร้างเสร็จยังอยู่ กดเริ่มอีกครั้งเพื่อทำต่อ')
            if batch_tiles and (batch_tiles+1>3000 or batch_bytes+n>50000000):
                ledger.execute('INSERT OR REPLACE INTO batches VALUES(?,?,?)',(batch,batch_tiles,batch_bytes));ledger.commit();batch+=1;batch_tiles=0;batch_bytes=0
            done+=1;size+=n;batch_tiles+=1;batch_bytes+=n
            ledger.execute('UPDATE tiles SET bytes=? WHERE z=? AND x=? AND y=?',(n,z,x,y))
            if batch_tiles>=3000 or batch_bytes>=50000000:
                ledger.execute('INSERT OR REPLACE INTO batches VALUES(?,?,?)',(batch,batch_tiles,batch_bytes));ledger.commit();batch+=1;batch_tiles=0;batch_bytes=0
            now=time.monotonic()
            if now-last>.25 or done==spec['count']:
                ledger.commit();progress({'stage':'render','message':f'สร้างแผนที่ {done:,}/{spec["count"]:,} แผ่น · ชุดที่ {batch}','done':done,'total':spec['count'],'bytes':size,'batch':batch,'lastTile':{'z':z,'x':x,'y':y}});last=now
        progress({'stage':'verify','message':'ตรวจความครบถ้วนทุกแผ่นก่อนตั้งค่าให้บอร์ดเปิดแผนที่','done':done,'total':spec['count'],'bytes':size})
        if batch_tiles:
            ledger.execute('INSERT OR REPLACE INTO batches VALUES(?,?,?)',(batch,batch_tiles,batch_bytes));ledger.commit()
        for z,x,y in ledger.execute('SELECT z,x,y FROM tiles'):
            if cancelled():raise BuildCancelled()
            if not valid_jpeg(output/str(z)/str(x)/(str(y)+'.jpg')):missing+=1
        if missing:raise ValueError(f'แผนที่ยังขาดหรือเสีย {missing} แผ่น กดเริ่มใหม่เพื่อซ่อมส่วนที่ขาด')
        center=p['center'];start_zoom=max(min_zoom,min(12,max_zoom))
        atomic(output/'pois.tsv',pending_pois)
        write_json(output/'coverage.json',{**spec,'complete':True,'completed':done,'bytes':size,'missing':0,'job':jobid,'batchesActual':batch,'attribution':source['attribution']})
        atomic(output/'attribution.txt',('© OpenStreetMap contributors / ODbL\nhttps://www.openstreetmap.org/copyright\nData extract: Geofabrik\nProvince boundaries: geoBoundaries / OSM Wambacher (2017), ODbL\n').encode())
        source['completedUtc']=time.time();source['complete']=True;write_json(source_file,source)
        # Complete-only display label for the CYD map-set selector. Keep a user
        # label untouched when resuming or extending an existing province.
        if not (output/'name.txt').exists():atomic(output/'name.txt',(p['name']+'\n').encode('utf-8'))
        atomic(output/'center.txt',f'{center[0]:.7f} {center[1]:.7f} {start_zoom} {min_zoom} {max_zoom}\n'.encode())
        (output/'working-job.json').unlink(missing_ok=True)
        result={'state':'complete','stage':'complete','message':'สร้างแผนที่ครบทุกแผ่นแล้ว พร้อมคัดลอกลง SD','done':done,'total':spec['count'],'count':spec['count'],'bytes':size,'missing':0,'batches':batch,'output':str(output),'pois':len(pois),'province':province_id}
        progress(result);return result
    finally:ledger.commit();ledger.close();con.close()

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--province',default='TH-84');parser.add_argument('--output',required=True);parser.add_argument('--cache',required=True);parser.add_argument('--min-zoom',type=int,default=8);parser.add_argument('--max-zoom',type=int,default=14);parser.add_argument('--pbf');parser.add_argument('--plan',action='store_true');a=parser.parse_args()
    if a.plan:print(json.dumps(plan(a.province,a.min_zoom,a.max_zoom),ensure_ascii=False))
    else:build(a.province,a.output,a.cache,a.min_zoom,a.max_zoom,pbf_path=a.pbf,progress=lambda p:print(json.dumps(p,ensure_ascii=False),flush=True))
