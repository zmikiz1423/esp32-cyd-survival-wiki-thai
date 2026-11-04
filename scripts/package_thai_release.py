"""Verify the final merge/SD assets, then create the ready-to-copy SD ZIP."""
from pathlib import Path
import re,json,hashlib,gzip,zipfile
from concurrent.futures import ThreadPoolExecutor
from itertools import islice
from PIL import Image

ROOT=Path(__file__).resolve().parents[1];SD=ROOT/'sdcard'
def sha(data):return hashlib.sha256(data).hexdigest()
def rows(path):return [l.split('\t') for l in path.read_text('utf-8').splitlines() if l and not l.startswith('#')]

def write_zip(archive,pairs):
    """Read a bounded batch concurrently; one writer preserves ZIP integrity.

    Provincial packs have tens of thousands of small files. Parallel reads
    avoid serial filesystem latency without loading the full map into RAM.
    """
    def read(item):
        path,name=item;info=zipfile.ZipInfo.from_file(path,arcname=name)
        return info,path.read_bytes()
    items=iter(pairs)
    with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z,ThreadPoolExecutor(max_workers=8) as readers:
        while batch:=list(islice(items,64)):
            for info,data in readers.map(read,batch):z.writestr(info,data,compress_type=zipfile.ZIP_DEFLATED,compresslevel=6)

if __name__=='__main__':
    image=ROOT/'firmware-CYD28.bin';merged=image.read_bytes()
    build=ROOT/'firmware/.pio/build/survival-wiki-28'
    components=[(0x1000,build/'bootloader.bin'),(0x8000,build/'partitions.bin'),
        (0xe000,Path.home()/'.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin'),
        (0x10000,build/'firmware.bin')]
    for offset,path in components:
        data=path.read_bytes();assert merged[offset:offset+len(data)]==data,(offset,path)
    assert merged[0x1000]==0xE9 and merged[0x10000]==0xE9
    assert merged[0x1000+2]==2 and merged[0x1000+3]==0x20 # DIO / 4MB / 40MHz
    version=re.search(r'#define FW_VERSION "([^"]+)"',(ROOT/'firmware/src/config.h').read_text('utf-8'))[1]
    assert version.encode('ascii') in merged
    aliases=['firmware/bin/survival-wiki-28-full.bin','docs/firmware/survival-wiki-28-full.bin','web-flasher/firmware/survival-wiki-28-full.bin']
    assert all((ROOT/p).read_bytes()==merged for p in aliases)
    for p in SD.rglob('*.gz'):
        assert gzip.decompress(p.read_bytes())==p.with_suffix('').read_bytes(),p
    assert len(rows(SD/'wiki/catalog.tsv'))==118
    assert len(rows(SD/'wiki/categories.tsv'))==15
    assert len(json.loads((SD/'wiki/search.json').read_text('utf-8')))==118
    books=rows(SD/'library/catalog.tsv');documents=[]
    for bid,title,count,pdf,*rest in books:
        assert (SD/'library'/bid/pdf).exists()
        for i in range(1,int(count)+1):
            p=SD/f'library/{bid}/{i:04}.txt';text=p.read_text('utf-8')
            assert '\ufffd' not in text and p.stat().st_size<=32768,p
            documents.append(p)
    assert len(documents)==84
    jpgs=list((SD/'wiki/img').glob('*.jpg'));assert len(jpgs)==22
    for p in jpgs:
        im=Image.open(p);assert im.width==448 and not im.info.get('progressive'),p
        assert p.with_suffix('.svg').exists()
    source=(ROOT/'firmware/src/main.cpp').read_text('utf-8')
    block=re.search(r'const char \*PAGES\[\] = \{(.*?)\};',source,re.S)[1]
    pages=[json.loads('"'+s+'"') for s in re.findall(r'"((?:\\.|[^"\\])*)"',block)]
    assert all(len(p)<=30 for p in pages),list(map(len,pages))
    assert any('฿' in p for p in pages) and any('\\' in p for p in pages)
    # Verify the revised CYD editor: two text lines, a dedicated byte-counter
    # row, and five key rows all fit before the footer at y=282.
    box_y=40;box_h=84;line_h=27;counter_h=24;key_top=130;key_h=(320-38-4-key_top)//5
    assert box_y+1+line_h*2 <= box_y+box_h-2-counter_h
    assert key_h>=28 and key_top+5*key_h<=282
    assert 'bh = 84' in source and 'top = CONTENT_Y + 94' in source
    archive=ROOT/'sdcard-TH.zip'
    files=sorted(p for p in SD.rglob('*') if p.is_file())
    write_zip(archive,((p,p.relative_to(SD).as_posix()) for p in files))
    with zipfile.ZipFile(archive) as z:
        assert z.testzip() is None
        assert 'config.txt' in z.namelist() and 'wiki/catalog.tsv' in z.namelist()
        assert not any(n.startswith('sdcard/') for n in z.namelist())
    helper=ROOT/'Thailand-Map-Downloader.zip'
    helper_files=[ROOT/'LICENSE',ROOT/'ดาวน์โหลดแผนที่ไทย.bat',ROOT/'MAPS_TH.md',ROOT/'OFFLINE_MAPS_TH.md',ROOT/'README_TH.md',
        *[ROOT/'scripts'/p for p in ('map-downloader.bat','map-downloader-gui.ps1','thailand-map-server.ps1','get-map-tiles.ps1')],
        ROOT/'sdcard/web/Sarabun-Regular.ttf',ROOT/'sdcard/web/OFL-Sarabun.txt',
        *[p for p in (ROOT/'scripts/map-app').rglob('*') if p.is_file() and 'node_modules' not in p.parts]]
    write_zip(helper,((p,p.relative_to(ROOT).as_posix()) for p in helper_files))
    checksums={image.name:sha(merged),archive.name:sha(archive.read_bytes()),helper.name:sha(helper.read_bytes())}
    # Complete province authoring tool: offline runtime included so the user
    # does not need to install Python or download binary dependencies manually.
    province_helper=ROOT/'Thailand-Offline-Province-Maps.zip'
    if (ROOT/'scripts/offline-map-server.py').exists():
        province_files=[ROOT/'LICENSE',ROOT/'เตรียมแผนที่ทั้งจังหวัด.bat',ROOT/'OFFLINE_MAPS_TH.md',ROOT/'MAPS_TH.md',ROOT/'README_TH.md',image,
            ROOT/'sdcard/web/Sarabun-Regular.ttf',ROOT/'sdcard/web/OFL-Sarabun.txt',
            *[ROOT/'scripts'/p for p in ('offline-map-launcher.ps1','offline-map-server.py','offline-runtime-manifest.json','requirements-offline-maps.txt')]]
        for directory in ('offline_maps','offline-map-app','offline-runtime'):
            province_files.extend(p for p in (ROOT/'scripts'/directory).rglob('*') if p.is_file() and '__pycache__' not in p.parts and p.suffix!='.pyc')
        assert (ROOT/'scripts/offline-runtime/python.exe').is_file(),'Build the portable runtime before packaging'
        write_zip(province_helper,((p,p.relative_to(ROOT).as_posix()) for p in province_files))
        with zipfile.ZipFile(province_helper) as z:
            assert z.testzip() is None
            assert 'scripts/offline_maps/data/provinces.geojson' in z.namelist()
        checksums[province_helper.name]=sha(province_helper.read_bytes())
    map_files=list((SD/'maps').rglob('*')) if (SD/'maps').exists() else []
    map_source=json.loads((SD/'maps/source.json').read_text('utf-8')) if (SD/'maps/source.json').exists() else {}
    if map_files and map_source.get('provider')=='openfreemap':
        demo=ROOT/'maps-Bangkok-demo.zip'
        write_zip(demo,((p,p.relative_to(SD).as_posix()) for p in map_files if p.is_file()))
        checksums[demo.name]=sha(demo.read_bytes())
    elif (ROOT/'maps-Bangkok-demo.zip').exists():
        # Preserve the earlier Bangkok example when the default SD pack has
        # been upgraded to a complete provincial map set.
        checksums['maps-Bangkok-demo.zip']=sha((ROOT/'maps-Bangkok-demo.zip').read_bytes())
    province_demo=ROOT/'province-maps/maps'
    if (province_demo/'coverage.json').exists() and not (province_demo/'working-job.json').exists() and json.loads((province_demo/'coverage.json').read_text('utf-8')).get('complete'):
        assert json.loads((province_demo/'coverage.json').read_text('utf-8'))['province']['id']=='TH-84','The Surat release ZIP must contain Surat Thani'
        province_zip=ROOT/'maps-Surat-Thani-offline.zip'
        write_zip(province_zip,((p,p.relative_to(province_demo.parent).as_posix()) for p in sorted(province_demo.rglob('*')) if p.is_file() and not p.name.endswith('.part') and p.name!='.map-builder.lock'))
        with zipfile.ZipFile(province_zip) as z:assert z.testzip() is None
        checksums[province_zip.name]=sha(province_zip.read_bytes())
    (ROOT/'SHA256SUMS.txt').write_text(''.join(f'{v}  {k}\n' for k,v in checksums.items()),'ascii')
    report={'board':'ESP32-2432S028R CYD 2.8 / ILI9341','version':version,'flash_offset':'0x0',
        'flash_mode':'dio','flash_frequency':'40m','flash_size':'4MB','firmware_bytes':len(merged),
        'merged_components_verified':len(components),'matching_web_flasher_images':len(aliases),
        'articles':118,'book_parts':len(documents),'thai_diagrams':len(jpgs),'sd_files':len(files),
        'largest_book_part_bytes':max(p.stat().st_size for p in documents),'keyboard_page_lengths':list(map(len,pages)),
        'keyboard_counter_overlap':False,'checksums':checksums,'physical_board_tested':False}
    if (SD/'maps/coverage.json').exists():
        coverage=json.loads((SD/'maps/coverage.json').read_text('utf-8'))
        report['sd_map']={'province':coverage['province']['id'],'tiles':coverage['count'],'minZoom':coverage['minZoom'],'maxZoom':coverage['maxZoom'],'complete':coverage['complete'],'missing':coverage.get('missing',0)}
    (ROOT/'docs/thai-release-report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),'utf-8')
    print(json.dumps(report,ensure_ascii=True,indent=2))
