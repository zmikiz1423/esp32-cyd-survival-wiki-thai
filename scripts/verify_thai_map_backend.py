"""Offline backend security/resume/quota QA on real Windows PowerShell 5.1.

Requires Python/Pillow only for developer QA. End-user downloader does not.
All fixtures live beneath .thai-work/backend-final-qa; no external map fetches.
"""
from pathlib import Path
import io,json,subprocess,time,urllib.request,urllib.error,hashlib,shutil
from PIL import Image

ROOT=Path(__file__).resolve().parents[1]
WORK=ROOT/'.thai-work/backend-final-qa'/str(int(time.time()))
WORK.mkdir(parents=True)
checks=[];servers=[]

def check(name,condition):
    assert condition,name
    checks.append(name)

def jpeg(progressive=False):
    out=io.BytesIO();Image.new('RGB',(256,256),'#d6e5cd').save(out,'JPEG',progressive=progressive);return out.getvalue()

def launch(label):
    folder=WORK/label;folder.mkdir();cache=folder/'cache';cache.mkdir();output=folder/'maps';output.mkdir()
    # Static fixture: backend must not contact tile provider during these tests.
    tilejson={'tiles':['https://tiles.openfreemap.org/planet/20261025_080012_pt/{z}/{x}/{y}.pbf'],'maxzoom':14}
    (cache/'tilejson.json').write_text(json.dumps(tilejson),'utf-8')
    state=folder/'state.json'
    command=[shutil.which('powershell'),'-NoProfile','-ExecutionPolicy','Bypass','-STA','-File',str(ROOT/'scripts/thailand-map-server.ps1'),'-NoBrowser','-StateFile',str(state),'-Output',str(output),'-CacheDir',str(cache)]
    stdout=(folder/'stdout.log').open('w');stderr=(folder/'stderr.log').open('w')
    proc=subprocess.Popen(command,cwd=ROOT,stdout=stdout,stderr=stderr,creationflags=subprocess.CREATE_NO_WINDOW)
    info=None
    for _ in range(150):
        if state.exists():
            try:info=json.loads(state.read_text('utf-8-sig'));break
            except ValueError:pass
        if proc.poll() is not None:raise RuntimeError((folder/'stderr.log').read_text())
        time.sleep(.1)
    assert info,'server startup timed out'
    server={'proc':proc,'out':output,'cache':cache,'base':info['url'].split('/?')[0],'token':info['token'],'stdout':stdout,'stderr':stderr}
    servers.append(server);return server

def request(s,path,body=None,auth=True,origin=None,token=None,content='application/json'):
    headers={}
    if auth:headers['X-Map-Token']=token or s['token']
    if origin is not None:headers['Origin']=origin
    if body is not None:
        headers['Content-Type']=content
        if not isinstance(body,bytes):body=json.dumps(body).encode()
    req=urllib.request.Request(s['base']+path,data=body,headers=headers,method='POST' if body is not None else 'GET')
    try:
        with urllib.request.urlopen(req,timeout=15) as r:return r.status,r.read(),dict(r.headers)
    except urllib.error.HTTPError as e:return e.code,e.read(),dict(e.headers)

def json_request(s,path,body=None,expect=200,**kw):
    code,data,_=request(s,path,body,**kw);assert code==expect,(path,code,data)
    return json.loads(data)

def key_query(plan,tile=None):
    t=tile or plan['tiles'][0]
    return f'?job={plan["job"]}&z={t["z"]}&x={t["x"]}&y={t["y"]}'

def cache_fixture(server,tile,body=b'\x1a\x00'):
    factor=2**max(0,tile['z']-14);z=min(tile['z'],14);x=tile['x']//factor;y=tile['y']//factor
    path=server['cache']/f'pbf/20261025_080012_pt/{z}/{x}/{y}.pbf';path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(body)
    return body

try:
    s=launch('main');plan_data={'lat':13.7563,'lon':100.5018,'radius':1,'minZoom':6,'maxZoom':6}
    config=json_request(s,'/api/config');check('PowerShell51 server starts without Node/Python runtime',config['initial']['radius']==2)
    check('Unauthenticated API blocked',request(s,'/api/config',auth=False)[0]==403)
    check('Wrong token blocked',request(s,'/api/config',token='not-the-token')[0]==403)
    check('Cross-origin API blocked',request(s,'/api/config',origin='https://evil.example')[0]==403)
    check('Index requires token',request(s,'/',auth=False)[0]==403)
    code,index,headers=request(s,'/?token='+s['token'],auth=False)
    check('Index token substitution and anti-sniff headers',code==200 and b'__MAP_TOKEN__' not in index and s['token'].encode() in index and headers.get('X-Content-Type-Options')=='nosniff')
    for url in ['/vendor/../../README_TH.md','/firmware/src/main.cpp','/Sarabun-Regular.ttf/../../config.txt','/vendor/config.json']:
        check('Static path blocked: '+url,request(s,url,auth=False)[0] in (400,403,404))
    for name,body in [('tile cap',dict(plan_data,radius=50,maxZoom=16)),('invalid zoom',dict(plan_data,minZoom=17,maxZoom=17)),('dateline',dict(plan_data,lon=179.99,radius=50))]:
        check('Plan validation: '+name,request(s,'/api/start',body)[0]==400)
    check('Oversize JSON rejected',request(s,'/api/start',b' '*8193)[0]==400)
    searchdir=s['cache']/'search';searchdir.mkdir()
    for query,result in [('empty-test-query',[]),('เชียงใหม่',[{'display_name':'เชียงใหม่ ประเทศไทย','lat':'18.7883','lon':'98.9853'}])]:
        cache=searchdir/(hashlib.sha256(query.lower().encode()).hexdigest()+'.json');cache.write_text(json.dumps(result,ensure_ascii=False),'utf-8')
        value=json_request(s,'/api/search?q='+urllib.parse.quote(query))
        check('Search array/UTF8 cache preserved: '+query,value==result)
    for name,text in [('waypoints.txt','WP1\t13.7\t100.5\n'),('track.csv','13.7,100.5\n')]:(s['out']/name).write_text(text,'utf-8')
    originals={n:(s['out']/n).read_bytes() for n in ('waypoints.txt','track.csv')}
    plan=json_request(s,'/api/start',plan_data);check('Small Thailand plan',plan['count']==1 and plan['tiles'][0]['exists'] is False)
    check('No center committed by start',not (s['out']/'center.txt').exists())
    check('Unplanned tile rejected',request(s,f'/api/tile?job={plan["job"]}&z=6&x=0&y=0')[0]==400)
    check('Unknown job rejected',request(s,'/api/tile?job=bad&z=6&x=49&y=29')[0]==400)
    check('Incomplete finish rejected',request(s,'/api/finish',{'job':plan['job']})[0]==400 and not (s['out']/'center.txt').exists())
    raw=cache_fixture(s,plan['tiles'][0]);code,pbf,_=request(s,'/api/tile'+key_query(plan))
    check('Cached vector returned unchanged',code==200 and pbf==raw)
    ledger_path=next((s['cache']/'plans').glob('*.json'));ledger=json.loads(ledger_path.read_text('utf-8-sig'))
    check('Cached payload counts toward unique included budget',ledger['includedBytes']==len(raw) and ledger['networkBytes']==0)
    png=io.BytesIO();Image.new('RGB',(256,256)).save(png,'PNG')
    for name,body in [('PNG',png.getvalue()),('progressive JPEG',jpeg(True)),('malformed JPEG',b'\xff\xd8'+b'x'*200)]:
        check(name+' rejected',request(s,'/api/save'+key_query(plan),body,content='image/jpeg')[0]==400)
    good=jpeg();json_request(s,'/api/save'+key_query(plan),good,content='image/jpeg')
    finished=json_request(s,'/api/finish',{'job':plan['job']})
    center=(s['out']/'center.txt').read_bytes();check('Complete finish commits center',center.decode().strip()=='13.7563 100.5018 6 6 6')
    check('Complete finish writes source and area metadata',finished['count']==1 and json.loads((s['out']/'source.json').read_text())['provider']=='openfreemap' and (s['out']/'area.json').exists() and (s['out']/'attribution.txt').exists())
    again=json_request(s,'/api/start',plan_data);check('Resume recognizes valid saved JPEG',again['tiles'][0]['exists'] is True)
    incomplete=json_request(s,'/api/start',dict(plan_data,minZoom=7,maxZoom=7))
    check('Failed finish preserves prior working center',request(s,'/api/finish',{'job':incomplete['job']})[0]==400 and (s['out']/'center.txt').read_bytes()==center)
    # Simulate the original area's exhausted persistent network budget without
    # actually transferring 50 MB. Remove only this test's generated JPEG.
    tile=plan['tiles'][0];tile_file=s['out']/f'{tile["z"]}/{tile["x"]}/{tile["y"]}.jpg'
    assert tile_file.resolve().is_relative_to(WORK.resolve());tile_file.unlink()
    ledger['networkBytes']=50000000;ledger_path.write_text(json.dumps(ledger),'utf-8')
    check('Same-area restart cannot reset network quota',request(s,'/api/start',plan_data)[0]==400)
    reduced=json_request(s,'/api/start',dict(plan_data,radius=.5));check('Reduced area has a separate bounded plan',reduced['count']==1)
    cache_fixture(s,reduced['tiles'][0]);request(s,'/api/tile'+key_query(reduced))
    included_ledgers=[json.loads(p.read_text('utf-8-sig')) for p in (s['cache']/'plans').glob('*.json')]
    check('Prior quota ledger retained',any(x['networkBytes']==50000000 for x in included_ledgers))
    check('Waypoints and GPS track preserved',all((s['out']/n).read_bytes()==b for n,b in originals.items()))
    legacy=launch('legacy');(legacy['out']/'waypoints.txt').write_text('keep me','utf-8')
    old=legacy['out']/'6/49/29.jpg';old.parent.mkdir(parents=True);old.write_bytes(good)
    check('Unlabelled legacy tile folder refused',request(legacy,'/api/start',plan_data)[0]==400)
    check('Legacy files preserved',old.read_bytes()==good and (legacy['out']/'waypoints.txt').read_text()=='keep me')
    (legacy['out']/'source.json').write_text(json.dumps({'provider':'usgs','rendererVersion':1}),'utf-8')
    check('Other-provider folder refused',request(legacy,'/api/start',plan_data)[0]==400)
    report={'result':'PASS','runtime':'Windows PowerShell 5.1 / real loopback HttpListener','checks':checks,'check_count':len(checks),'external_network_requests':0,'map_provider_responses':'Local fixture PBF, cached TileJSON; no 50 MB transfer performed','fixture_root':str(WORK.relative_to(ROOT)),'physical_sd_or_board_tested':False}
    (ROOT/'docs/thai-map-backend-test-report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),'utf-8')
    print(json.dumps(report,ensure_ascii=True,indent=2))
finally:
    for server in servers:
        try:request(server,'/api/close',{})
        except Exception:pass
        try:server['proc'].wait(timeout=5)
        except subprocess.TimeoutExpired:server['proc'].terminate();server['proc'].wait(timeout=5)
        server['stdout'].close();server['stderr'].close()
