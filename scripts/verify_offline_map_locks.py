"""Check cross-process map cache/output locks and all lock release paths."""
from pathlib import Path
import json
import os
import subprocess
import sys
import time
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from offline_maps import builder as b


def run():
    work = ROOT / '.thai-work' / ('offline-lock-qa-' + str(time.time_ns()))
    work.mkdir(parents=True)
    cache, output = work / 'cache', work / 'maps'
    other_cache, other_output = work / 'other-cache', work / 'other-maps'
    ready, release = work / 'ready', work / 'release'
    checks = []

    def check(name, condition):
        assert condition, name
        checks.append(name)

    def rejected(c, o):
        try:
            with b.exclusive_build(c, o):
                return False
        except ValueError:
            return True

    child = '''from pathlib import Path
import sys,time
sys.path.insert(0,sys.argv[1])
from offline_maps import builder
with builder.exclusive_build(sys.argv[2],sys.argv[3]):
 Path(sys.argv[4]).write_text('ready')
 while not Path(sys.argv[5]).exists():time.sleep(.05)
'''
    stdout = (work / 'stdout.log').open('wb')
    stderr = (work / 'stderr.log').open('wb')
    proc = subprocess.Popen([sys.executable, '-X', 'utf8', '-c', child,
                             str(ROOT / 'scripts'), str(cache), str(output), str(ready), str(release)],
                            stdout=stdout, stderr=stderr,
                            creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0)
    try:
        deadline = time.monotonic() + 30
        while not ready.exists() and proc.poll() is None and time.monotonic() < deadline:
            time.sleep(.05)
        check('Separate process acquires cache and output locks', ready.exists())
        check('Concurrent shared cache is refused', rejected(cache, other_output))
        check('Concurrent shared output is refused', rejected(other_cache, output))
        with b.exclusive_build(other_cache, other_output):
            check('Output contention releases partially acquired cache lock', True)
        release.write_text('release')
        proc.wait(timeout=15)
        check('Lock holder exits cleanly', proc.returncode == 0)
        with b.exclusive_build(cache, output):
            check('Successful process releases both locks', True)
    finally:
        if proc.poll() is None:
            proc.terminate()
            proc.wait(timeout=15)
        stdout.close()
        stderr.close()

    for exc, label in ((b.BuildCancelled('QA cancellation'), 'Cancellation'),
                       (RuntimeError('QA failure'), 'Failure')):
        try:
            with patch.object(b, '_build', side_effect=exc):
                b.build('TH-84', output, cache, 8, 8)
        except type(exc):
            pass
        else:
            raise AssertionError(label + ' was not propagated')
        with b.exclusive_build(cache, output):
            check(label + ' releases both build locks', True)

    report = {'result': 'PASS', 'count': len(checks), 'checks': checks,
              'work': str(work), 'hardwareTested': False, 'externalRequests': 0}
    (ROOT / 'docs/thai-offline-lock-test-report.json').write_text(
        json.dumps(report, ensure_ascii=False, indent=2), 'utf-8')
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return report


if __name__ == '__main__':
    run()
