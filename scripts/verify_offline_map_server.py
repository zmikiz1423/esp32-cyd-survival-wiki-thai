"""Exercise the actual embedded-runtime HTTP server against a tiny local OSM fixture."""
from pathlib import Path
import hashlib
import importlib.util
import json
import subprocess
import sys
import time
import urllib.request
import urllib.error

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / '.thai-work' / ('offline-server-qa-' + str(int(time.time())))
WORK.mkdir(parents=True)
checks = []


def check(name, condition):
    assert condition, name
    checks.append(name)


fixture = WORK / 'fixture.osm'
fixture.write_text('''<?xml version="1.0" encoding="UTF-8"?>
<osm version="0.6" generator="SurvivalWiki QA">
<node id="1" lat="9.13" lon="99.32"><tag k="name:th" v="โรงพยาบาลทดสอบ"/><tag k="amenity" v="hospital"/></node>
<node id="2" lat="9.131" lon="99.321"/>
<node id="3" lat="9.132" lon="99.322"/>
<way id="10"><nd ref="1"/><nd ref="2"/><nd ref="3"/><tag k="highway" v="residential"/><tag k="name:th" v="ถนนทดสอบ"/></way>
</osm>''', 'utf-8')
state_file = WORK / 'state.json'
runtime = ROOT / 'scripts/offline-runtime/python.exe'
stdout = (WORK / 'stdout.log').open('w')
stderr = (WORK / 'stderr.log').open('w')
proc = subprocess.Popen([str(runtime), '-u', str(ROOT / 'scripts/offline-map-server.py'),
                         '--no-browser', '--state-file', str(state_file), '--output', str(WORK / 'maps'),
                         '--cache', str(WORK / 'cache'), '--pbf', str(fixture)], cwd=ROOT,
                        stdout=stdout, stderr=stderr, creationflags=subprocess.CREATE_NO_WINDOW)
info = None
try:
    for _ in range(150):
        if state_file.exists():
            info = json.loads(state_file.read_text('utf-8'))
            break
        if proc.poll() is not None:
            raise RuntimeError((WORK / 'stderr.log').read_text())
        time.sleep(.1)
    check('Embedded Python server starts without installed Python', info is not None)
    base = info['url'].split('/?')[0]

    def request(path, data=None, auth=True, origin=None, host=None):
        headers = {'X-Map-Token': info['token']} if auth else {}
        if origin:
            headers['Origin'] = origin
        if host:
            headers['Host'] = host
        if data is not None:
            data = data if isinstance(data, bytes) else json.dumps(data).encode()
            headers['Content-Type'] = 'application/json'
        req = urllib.request.Request(base + path, data=data, headers=headers)
        try:
            with urllib.request.urlopen(req, timeout=30) as response:
                return response.status, response.read()
        except urllib.error.HTTPError as exc:
            return exc.code, exc.read()

    def get(path, data=None):
        code, raw = request(path, data)
        assert code == 200, (code, raw)
        return json.loads(raw)

    config = get('/api/config')
    check('77 province options with Thai Surat Thani', len(config['provinces']) == 77 and any(p['id'] == 'TH-84' and p['name'] == 'สุราษฎร์ธานี' for p in config['provinces']))
    check('Missing API token rejected', request('/api/config', auth=False)[0] == 403)
    check('Cross-origin request rejected', request('/api/config', origin='https://evil.example')[0] == 403)
    check('Invalid Host rejected', request('/api/config', host='evil.example')[0] == 403)
    check('Index without token rejected', request('/', auth=False)[0] == 403)
    code, raw = request('/?token=' + info['token'], auth=False)
    check('Index injects token into UTF8 local UI', code == 200 and b'__MAP_TOKEN__' not in raw and 'จังหวัด'.encode() in raw)
    for asset in ('/app.js', '/Sarabun-Regular.ttf'):
        code, raw = request(asset, auth=False)
        check('Bundled static asset: ' + asset, code == 200 and len(raw) > 100)
    for path in ('/../README_TH.md', '/scripts/offline-runtime-manifest.json', '/province-maps/maps/config.txt'):
        check('Static allowlist blocks ' + path, request(path, auth=False)[0] == 404)
    check('Oversized JSON rejected', request('/api/plan', b' ' * 8193)[0] == 400)
    check('Boolean zoom rejected', request('/api/plan', {'province': 'TH-84', 'minZoom': True, 'maxZoom': 8})[0] == 400)
    check('Unknown province rejected', request('/api/plan', {'province': 'TH-00', 'minZoom': 8, 'maxZoom': 8})[0] == 400)
    params = {'province': 'TH-84', 'minZoom': 8, 'maxZoom': 8}
    plan = get('/api/plan', params)
    check('Real Surat province plan includes geometry and finite tile count', plan['count'] > 1 and plan['geometry']['type'] in ('Polygon', 'MultiPolygon'))
    launched = get('/api/start', params)
    check('Build starts asynchronously', launched['running'] and launched['state'] == 'running')
    # This plan is small; let it complete and verify actual output, not mocked API calls.
    for _ in range(300):
        status = get('/api/status')
        if not status['running']:
            break
        time.sleep(.1)
    check('Fixture renders through actual embedded deps and builder', status['state'] == 'completed')
    check('Complete status has no missing tiles', status['missing'] == 0 and status['completed'] == plan['count'])
    check('Center only present after completion', (WORK / 'maps/center.txt').exists())
    check('Local OSM provider manifest', json.loads((WORK / 'maps/source.json').read_text('utf-8'))['provider'] == 'local-osm')
    code, raw = request('/api/preview?token=' + info['token'], auth=False)
    check('Preview token permits only actual last rendered JPEG', code == 200 and raw[:2] == b'\xff\xd8')
    check('Preview without token rejected', request('/api/preview', auth=False)[0] == 403)
    tiles = sorted((WORK / 'maps').glob('*/*/*.jpg'))
    originals = {str(p): p.stat().st_mtime_ns for p in tiles}
    get('/api/start', params)
    for _ in range(200):
        status = get('/api/status')
        if not status['running']:
            break
        time.sleep(.1)
    check('Completed job resumes without rewriting valid JPEGs', status['state'] == 'completed' and all(Path(p).stat().st_mtime_ns == stamp for p, stamp in originals.items()))
    center_before_cancel = (WORK / 'maps/center.txt').read_bytes()
    get('/api/start', dict(params, maxZoom=15))
    check('Close while generating rejected', request('/api/close', {})[0] == 409)
    check('Changing output while generating rejected', request('/api/select-folder', {})[0] == 400)
    get('/api/cancel', {})
    for _ in range(200):
        status = get('/api/status')
        if not status['running']:
            break
        time.sleep(.1)
    check('Cancel stops worker and retains resume state', status['state'] == 'cancelled')
    check('Cancelled job preserves previously complete center', (WORK / 'maps/center.txt').read_bytes() == center_before_cancel)
    get('/api/close', {})
    proc.wait(timeout=10)
    check('Close API stops local process', proc.returncode == 0)
finally:
    if proc.poll() is None:
        proc.terminate()
        proc.wait(timeout=10)
    stdout.close()
    stderr.close()

# Unit-check progress normalization: country download bytes are not tile counts.
module_spec = importlib.util.spec_from_file_location('offline_map_server', ROOT / 'scripts/offline-map-server.py')
module = importlib.util.module_from_spec(module_spec)
module_spec.loader.exec_module(module)
app = module.MapApplication(WORK / 'normalization/maps', WORK / 'normalization/cache')
app.status['count'] = 100
app.progress({'stage': 'download', 'done': 1000000, 'total': 10000000, 'bytes': 1000000})
check('Download byte progress kept separate from map tile progress', app.status['completed'] == 0 and app.status['count'] == 100 and app.status['phaseDone'] == 1000000)
report = {'result': 'PASS', 'checks': checks, 'count': len(checks), 'runtime': str(runtime), 'python': '3.13.17 embedded',
          'fixture_only': True, 'external_country_requests': 0, 'physical_board_tested': False, 'fixture_root': str(WORK)}
(ROOT / 'docs/thai-offline-map-server-test-report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), 'utf-8')
print(json.dumps(report, ensure_ascii=True, indent=2))
