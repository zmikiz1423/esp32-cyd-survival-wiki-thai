"""Run the real firmware map-storage/POI/route code with a host filesystem SD adapter.

Use --compiler /path/to/zig.exe for Zig, or --compiler /path/to/g++.
The small host adapter replaces Arduino/SD hardware only; map logic is compiled unchanged.
"""
from pathlib import Path
import argparse, json, shutil, subprocess, time
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--compiler');args=p.parse_args()
compiler=args.compiler
if not compiler:
    compiler=next((shutil.which(n) for n in ('clang++','g++','c++') if shutil.which(n)),None)
if not compiler:
    zig=ROOT/'.thai-work/native-compiler/ziglang/zig.exe'
    if zig.exists():compiler=str(zig)
if not compiler:raise SystemExit('Host C++ compiler required; pass --compiler (g++, clang++, or Zig).')
work=ROOT/'.thai-work'/('multi-map-storage-'+str(time.time_ns()));work.mkdir(parents=True)
binary=work/('verify.exe' if __import__('os').name=='nt' else 'verify')
tests=ROOT/'scripts/tests/map_native'
command=[compiler]
if Path(compiler).name.lower() in ('zig','zig.exe'):command+=['c++','-target','x86_64-windows-gnu'] if __import__('os').name=='nt' else ['c++']
command+=['-std=c++17','-O0','-w','-I'+str(tests),'-I'+str(ROOT/'firmware/src'),str(tests/'test_map_storage.cpp'),str(ROOT/'firmware/src/map_storage.cpp'),str(ROOT/'firmware/src/offline_maps.cpp'),'-o',str(binary)]
with (work/'compile.log').open('w',encoding='utf-8') as log:
    built=subprocess.run(command,cwd=ROOT,stdout=log,stderr=log)
if built.returncode:
    lines=(work/'compile.log').read_text('utf-8',errors='replace').splitlines()
    print('\n'.join(lines[-60:]))
    raise SystemExit('Host compile failed; see '+str(work/'compile.log'))
result=subprocess.run([str(binary),str(work/'sd')],check=True,capture_output=True,text=True,encoding='utf-8')
report={'result':'PASS','details':result.stdout.strip(),'implementation':'Actual firmware/src/map_storage.cpp and offline_maps.cpp compiled against host Arduino/SD adapters','physicalBoardTested':False,'fixturePath':str(work)}
(ROOT/'docs/thai-multi-map-storage-test-report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n','utf-8')
print(result.stdout.strip())
