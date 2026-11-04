"""Developer checks for province coverage, complete/resume/cancel, and source safety."""
import json,sqlite3,tempfile,time,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from offline_maps import builder as b
from shapely.geometry import Point
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[2]

def run():
 checks=[]
 def check(name,ok):
  assert ok,name
  checks.append(name)
 provinces=b.list_provinces();check('77 unique ISO provinces',len(provinces)==77 and len({p['id'] for p in provinces})==77)
 surat=b._province('TH-84');g=surat['shape'];spec=b.plan('TH-84',8,14)
 check('Province plan includes true polygon and all zooms',spec['count']==sum(spec['zoomCounts'].values()) and spec['count']>3000 and spec['geometry']['type']=='MultiPolygon')
 tiles=set(b._tiles(g,8,14))
 for name,lat,lon in [('Surat city',9.1382,99.3215),('Samui',9.512,100.013),('Phangan',9.75,100.03),('Tao',10.095,99.84),('AngThong',9.62,99.67)]:
  check(name+' covered at every requested zoom',all((z,b.tx(lon,z),b.ty(lat,z)) in tiles for z in range(8,15)))
 check('Landmask includes neighboring province land',b._country_land().covers(Point(99.96,8.43)))
 text=b.ShapedText(ROOT/'sdcard/web/Sarabun-Regular.ttf');mask=text.mask('สุราษฎร์ธานี น้ำดื่ม เกาะพะงัน',24)
 check('Native HarfBuzz FreeType Thai shaped mask',mask.getbbox() is not None and mask.size[1]>24)
 # Regression: attribution must never overwrite a strip of geographic pixels.
 c=sqlite3.connect(':memory:');c.executescript('CREATE TABLE features(id INTEGER PRIMARY KEY,osm_id TEXT,geom BLOB,tags TEXT,kind TEXT,min_zoom INTEGER);CREATE VIRTUAL TABLE spatial USING rtree(id,minx,maxx,miny,maxy);')
 z=16;x=b.tx(99.3215,z);y=b.ty(9.1382,z);im=b.render_tile(c,z,x,y,text,b._country_land())
 check('Tile bottom contains map pixels without a painted credit stripe',im.getpixel((128,250))==im.getpixel((128,230)))
 from shapely.geometry import Polygon
 w,s,e,n=b.tile_bounds(z,x,y)
 def ring(a,d):return [(w+(e-w)*a,s+(n-s)*a),(w+(e-w)*d,s+(n-s)*a),(w+(e-w)*d,s+(n-s)*d),(w+(e-w)*a,s+(n-s)*d),(w+(e-w)*a,s+(n-s)*a)]
 geom=Polygon(ring(.1,.9),[ring(.35,.65)])
 c.execute('INSERT INTO features VALUES(?,?,?,?,?,?)',(1,'test',sqlite3.Binary(geom.wkb),'{"natural":"wood"}','area',8));c.execute('INSERT INTO spatial VALUES(?,?,?,?,?)',(1,w,e,s,n))
 forest=b.render_tile(c,z,x,y,text,b._country_land())
 check('Forest polygon clearing preserves ground instead of inventing water',forest.getpixel((128,128))==im.getpixel((128,128)))
 c.close()
 work=ROOT/'.thai-work'/('province-builder-qa-'+str(time.time_ns()));work.mkdir(parents=True)
 source=work/'fixture.osm';source.write_text('''<?xml version="1.0" encoding="UTF-8"?><osm version="0.6" generator="QA"><node id="1" lat="9.1382" lon="99.3215"><tag k="name:th" v="โรงพยาบาลสุราษฎร์ธานี"/><tag k="amenity" v="hospital"/></node><node id="2" lat="9.1384" lon="99.3218"/><node id="3" lat="9.1384" lon="99.3220"/><node id="4" lat="9.1380" lon="99.3220"/><node id="5" lat="9.1380" lon="99.3218"/><way id="10"><nd ref="1"/><nd ref="2"/><tag k="highway" v="residential"/><tag k="name:th" v="ถนนทดสอบ"/></way><way id="11"><nd ref="2"/><nd ref="3"/><nd ref="4"/><nd ref="5"/><nd ref="2"/><tag k="building" v="yes"/><tag k="name:th" v="อาคารทดสอบ"/></way></osm>''','utf8')
 out=work/'maps';cache=work/'cache';events=[]
 result=b.build('TH-84',out,cache,8,8,pbf_path=source,progress=events.append)
 check('Complete build creates all validated baseline JPEGs',result['count']==5 and all(b.valid_jpeg(p) for p in out.rglob('*.jpg')))
 check('POI TSV has firmware-compatible Thai name and coordinates','โรงพยาบาลสุราษฎร์ธานี' in (out/'pois.tsv').read_text('utf8'))
 check('Coverage complete and initial zoom within bounds',json.loads((out/'coverage.json').read_text('utf8'))['complete'] and (out/'center.txt').read_text().strip().endswith('8 8 8'))
 check('Source dataset/font/boundary hashes recorded',all(k in json.loads((out/'source.json').read_text('utf8')) for k in ['datasetSha256','fontSha256','boundarySha256','fingerprint']))
 old={p:p.stat().st_mtime_ns for p in out.rglob('*.jpg')};center=(out/'center.txt').read_bytes();pois=(out/'pois.tsv').read_bytes()
 (out/'waypoints.txt').write_text('my waypoint','utf8');(out/'track.csv').write_text('my track','utf8')
 b.build('TH-84',out,cache,8,8,pbf_path=source)
 check('Resume keeps every valid JPEG without rewriting',all(p.stat().st_mtime_ns==stamp for p,stamp in old.items()))
 victim=next(iter(old));victim.write_bytes(b'broken');b.build('TH-84',out,cache,8,8,pbf_path=source)
 check('Resume repairs corrupt JPEG',b.valid_jpeg(victim))
 def cancel_event(p):
  if p.get('stage')=='render':state[0]=True
 state=[False]
 try:b.build('TH-84',out,cache,8,9,pbf_path=source,progress=cancel_event,cancelled=lambda:state[0])
 except b.BuildCancelled:pass
 check('Cancellation preserves previous center and POI index',(out/'center.txt').read_bytes()==center and (out/'pois.tsv').read_bytes()==pois)
 check('Cancellation leaves working job marker',(out/'working-job.json').exists())
 check('Existing waypoints and tracks preserved',(out/'waypoints.txt').read_text()=='my waypoint' and (out/'track.csv').read_text()=='my track')
 # Changing source even while all JPEGs validate must never silently reuse them.
 source.write_text(source.read_text('utf8').replace('อาคารทดสอบ','อาคารทดสอบใหม่'),'utf8')
 try:b.build('TH-84',out,cache,8,8,pbf_path=source);rejected=False
 except ValueError:rejected=True
 check('Changed raw source refused to prevent mixed old/new tiles',rejected)
 legacy=work/'legacy';legacy.mkdir();(legacy/'old.jpg').write_bytes(b'old')
 try:b.build('TH-84',legacy,cache,8,8,pbf_path=source);rejected=False
 except ValueError:rejected=True
 check('Legacy unknown map folder refused and preserved',rejected and (legacy/'old.jpg').read_bytes()==b'old')
 report={'result':'PASS','checks':checks,'count':len(checks),'suratZoomCounts':spec['zoomCounts'],'fixtureOnly':True,'hardwareTested':False,'work':str(work)}
 (ROOT/'docs/thai-province-builder-test-report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),'utf8')
 print(json.dumps(report,ensure_ascii=False,indent=2))
 return report
if __name__=='__main__':run()
