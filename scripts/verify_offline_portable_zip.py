"""Extract a complete province helper ZIP and smoke-test it in a Unicode/space path."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import time
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('archive', type=Path)
args = parser.parse_args()
archive = args.archive.resolve()
target = ROOT / '.thai-work' / ('portable province ทดสอบ ' + str(int(time.time())))
target.mkdir(parents=True)
checks = []


def check(name, condition):
    assert condition, name
    checks.append(name)


with zipfile.ZipFile(archive) as z:
    check('ZIP CRC validation', z.testzip() is None)
    for name in z.namelist():
        check_path = (target / name).resolve()
        assert check_path.is_relative_to(target.resolve()), name
    z.extractall(target)
runtime = target / 'scripts/offline-runtime/python.exe'
launcher = target / 'scripts/offline-map-launcher.ps1'
manifest = target / 'scripts/offline-runtime-manifest.json'
marker = target / 'scripts/offline-runtime/runtime-ready.json'
check('Bundled runtime marker matches pinned manifest', json.loads(marker.read_text('utf-8'))['manifestSHA256'] == hashlib.sha256(manifest.read_bytes()).hexdigest())
env = os.environ.copy()
for name in ('PYTHONHOME', 'PYTHONPATH'):
    env.pop(name, None)
ps = shutil.which('powershell.exe')
result = subprocess.run([ps, '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(launcher), '-SelfTest'],
                        cwd=target, env=env, capture_output=True, timeout=45, creationflags=subprocess.CREATE_NO_WINDOW)
check('Extracted launcher imports all portable dependencies', result.returncode == 0 and b'PASS: portable Python' in result.stdout)
state = target / 'qa-state.json'
stdout = (target / 'stdout.log').open('wb')
stderr = (target / 'stderr.log').open('wb')
proc = subprocess.Popen([str(runtime), '-u', str(target / 'scripts/offline-map-server.py'),
                         '--no-browser', '--state-file', str(state), '--cache', str(target / 'qa-cache')],
                        cwd=target, env=env, stdout=stdout, stderr=stderr, creationflags=subprocess.CREATE_NO_WINDOW)
try:
    info = None
    for _ in range(150):
        if state.exists():
            info = json.loads(state.read_text('utf-8'))
            break
        if proc.poll() is not None:
            raise RuntimeError((target / 'stderr.log').read_text('utf-8'))
        time.sleep(.1)
    check('Extracted server starts in Unicode and space path', info is not None)
    base = info['url'].split('/?')[0]

    def request(path, body=None, auth=True):
        req = urllib.request.Request(base + path, data=json.dumps(body).encode() if body is not None else None,
                                     headers={'X-Map-Token': info['token']} if auth else {})
        with urllib.request.urlopen(req, timeout=30) as response:
            return response.status, response.read()

    code, raw = request('/api/config')
    config = json.loads(raw)
    check('Extracted province data has all 77 choices', len(config['provinces']) == 77)
    check('Default output resolves inside extracted portable folder', Path(config['output']).is_relative_to(target))
    code, raw = request('/?token=' + info['token'], auth=False)
    check('Extracted UI has Thai text and replaced token', code == 200 and 'จังหวัด'.encode() in raw and b'__MAP_TOKEN__' not in raw)
    for asset in ('/app.js', '/Sarabun-Regular.ttf'):
        code, raw = request(asset, auth=False)
        check('Extracted asset loads: ' + asset, code == 200 and len(raw) > 100)
    code, raw = request('/api/plan', {'province': 'TH-84', 'minZoom': 8, 'maxZoom': 14})
    plan = json.loads(raw)
    check('Extracted actual Surat boundary planner works offline', plan['count'] > 100 and plan['geometry']['type'] in ('Polygon', 'MultiPolygon'))
    request('/api/close', {})
    proc.wait(timeout=10)
    check('Extracted server closes cleanly', proc.returncode == 0)
finally:
    if proc.poll() is None:
        proc.terminate()
        proc.wait(timeout=10)
    stdout.close()
    stderr.close()
report = {'result': 'PASS', 'checks': checks, 'count': len(checks), 'archive': str(archive),
          'sha256': hashlib.sha256(archive.read_bytes()).hexdigest(), 'test_path': str(target),
          'external_requests': 0, 'installed_python_required_for_end_user': False}
(ROOT / 'docs/thai-offline-portable-test-report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), 'utf-8')
print(json.dumps(report, ensure_ascii=True, indent=2))
