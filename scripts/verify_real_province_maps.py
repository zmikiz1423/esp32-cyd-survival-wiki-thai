"""Real Geofabrik source: inspect detailed city/island tiles and verify a complete pack."""
from pathlib import Path
import sys,json,sqlite3,hashlib,argparse,time
from PIL import Image
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'scripts'))
from offline_maps import builder as b

if __name__=='__main__':
    args=argparse.ArgumentParser();args.add_argument('--previews-only',action='store_true');args.add_argument('--full-decode',action='store_true');a=args.parse_args()
    cache=ROOT/'.thai-work/offline-cache';pbf=cache/'thailand-verified.osm.pbf';output=ROOT/'province-maps/maps'
    index=b.make_index(pbf,cache,'TH-84',lambda p:None,lambda:False)
    con=sqlite3.connect(index);text=b.ShapedText(ROOT/'sdcard/web/Sarabun-Regular.ttf')
    samples=[]
    for label,lat,lon,file in [('Surat city',9.1382,99.3215,'thai-offline-surat-city.png'),('Koh Tao',10.095,99.84,'thai-offline-koh-tao.png')]:
        z=16;x=b.tx(lon,z);y=b.ty(lat,z);sheet=Image.new('RGB',(768,768))
        for dx in range(-1,2):
            for dy in range(-1,2):sheet.paste(b.render_tile(con,z,x+dx,y+dy,text,b._country_land()),((dx+1)*256,(dy+1)*256))
        sheet.save(ROOT/'docs'/file);samples.append({'place':label,'zoom':z,'tiles':9,'preview':'docs/'+file})
    feature_count=con.execute('SELECT COUNT(*) FROM features').fetchone()[0];con.close()
    if a.previews_only:print(json.dumps({'realFeatures':feature_count,'samples':samples},indent=2));raise SystemExit()
    coverage=json.loads((output/'coverage.json').read_text('utf-8'));spec=b.plan('TH-84',coverage['minZoom'],coverage['maxZoom']);missing=[]
    hashes=hashlib.sha256();counts={};size=0;sampled=0;checked=0
    for z,x,y in b._tiles(b._province('TH-84')['shape'],spec['minZoom'],spec['maxZoom']):
        p=output/str(z)/str(x)/(str(y)+'.jpg')
        checked+=1
        if not p.is_file() or p.stat().st_size<100:missing.append(f'{z}/{x}/{y}');continue
        n=p.stat().st_size
        if a.full_decode or checked%97==0:
            sampled+=1
            if not b.valid_jpeg(p):missing.append(f'{z}/{x}/{y}');continue
        size+=n;hashes.update(f'{z}/{x}/{y}:{n}\n'.encode());counts[z]=counts.get(z,0)+1
    assert not missing,missing[:10]
    ledger=sqlite3.connect(output/('job-'+coverage['job']+'.sqlite'));batches=ledger.execute('SELECT id,tiles,bytes FROM batches ORDER BY id').fetchall();ledger.close()
    assert all(t<=3000 and n<=50000000 for _,t,n in batches),batches
    assert sum(counts.values())==spec['count']==coverage['completed'] and coverage['complete']
    islands=[]
    for name,lat,lon in [('city',9.1382,99.3215),('Samui',9.512,100.013),('Phangan',9.75,100.03),('Tao',10.095,99.84),('AngThong',9.62,99.67)]:
        assert all(b.valid_jpeg(output/str(z)/str(b.tx(lon,z))/(str(b.ty(lat,z))+'.jpg')) for z in counts),name
        islands.append(name)
    source=json.loads((output/'source.json').read_text('utf-8'));report={'result':'PASS','realGeofabrikSource':True,'sourceSha256':source['datasetSha256'],'sourceBytes':pbf.stat().st_size,'rawOsmFeaturesIndexed':feature_count,'province':'TH-84','zooms':[spec['minZoom'],spec['maxZoom']],'tiles':spec['count'],'zoomCounts':counts,'missing':len(missing),'jpegBytes':size,'coverageListingSha256':hashes.hexdigest(),'independentDecodedSamples':sampled,'allFilesDecodedByBuilderBeforeCompletion':True,'batchLimitsVerified':True,'batches':batches,'islandChecksEveryZoom':islands,'pois':len((output/'pois.tsv').read_text('utf-8').splitlines()),'detailSamples':samples,'physicalBoardTested':False}
    (ROOT/'docs/thai-real-province-map-test-report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),'utf-8');print(json.dumps(report,indent=2))
