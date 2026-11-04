"""Rebuild derived SD content after editing the Thai source articles/manual.

Uses PyThaiNLP only at authoring time, not on the ESP32. Word-break hints are
zero-width spaces; all files remain ordinary editable UTF-8 Markdown/text.
"""
from pathlib import Path
import re,json,gzip,hashlib
from pythainlp.tokenize import word_tokenize

ROOT=Path(__file__).resolve().parents[1];SD=ROOT/'sdcard'
ZW='\u200b'
HISTORICAL='คำเตือน: FM 21-76 เป็นเอกสารกองทัพปี 1992 มีแนวทางการแพทย์และความปลอดภัยที่ล้าสมัยหรืออันตราย ข้อความนี้แปลเพื่อรักษาเอกสารต้นฉบับ ไม่ใช่คำแนะนำรักษาพยาบาลปัจจุบัน สำหรับเหตุฉุกเฉินให้ใช้หมวดปฐมพยาบาลและติดต่อบุคลากรทางการแพทย์'

def plain(s):return s.replace(ZW,'')
def breaks(s):
    # Only Thai text is segmented. IDs, URLs, ASCII syntax and numbers stay
    # byte-for-byte unchanged, so links and protocol parsers keep working.
    def segment(m):
        tokens=word_tokenize(m[0],engine='newmm',keep_whitespace=True)
        out=[]
        for token in tokens:
            if out and token and (ord(token[0])==0xE31 or 0xE34<=ord(token[0])<=0xE3A or 0xE47<=ord(token[0])<=0xE4E):out[-1]+=token
            else:out.append(token)
        return ZW.join(out)
    return re.sub('[\u0e01-\u0e5b]+',segment,plain(s))
def article(path):
    s=plain(path.read_text('utf-8'));m=re.match(r'^---\n(.*?)\n---\n?(.*)',s,re.S)
    assert m,path
    meta={k:v.strip() for k,v in re.findall(r'^(\w+):\s*(.*)$',m[1],re.M)}
    return meta,m[2].strip()
def strip(s):
    s=re.sub(r'!\[([^]]*)\]\([^)]+\)',r'[ภาพ: \1]',s)
    s=re.sub(r'\[([^]]+)\]\([^)]+\)',r'\1',s)
    s=re.sub(r'^>\s*', 'คำเตือน: ',s,flags=re.M)
    return re.sub(r'[*`_#]','',s)

if __name__=='__main__':
    catfile=SD/'wiki/catalog.tsv';entries=[]
    for line in catfile.read_text('utf-8').splitlines():
        if line and not line.startswith('#'):
            aid,cid,_=line.split('\t',2);meta,body=article(SD/f'wiki/articles/{aid}.md')
            assert cid==meta['category'];entries.append((aid,cid,meta,body))
    assert len(entries)==118
    catfile.write_text('# id\tcategory\ttitle\n'+''.join(f'{aid}\t{cid}\t{meta["title"]}\n' for aid,cid,meta,body in entries),'utf-8',newline='\n')
    search=[{'id':aid,'c':cid,'t':meta['title'],'s':meta.get('summary',''),'k':meta.get('tags',''),'x':strip(body)} for aid,cid,meta,body in entries]
    raw=json.dumps(search,ensure_ascii=False,separators=(',',':')).encode('utf-8')
    (SD/'wiki/search.json').write_bytes(raw);(SD/'wiki/search.json.gz').write_bytes(gzip.compress(raw,mtime=0))
    # Never split an article between book parts. Thai byte expansion means the
    # combined field guide now needs more parts than the original English book.
    cats={p[0]:p[1] for l in (SD/'wiki/categories.tsv').read_text('utf-8').splitlines() if l and not l.startswith('#') for p in [l.split('\t')]}
    parts=[];current='';lastcat=''
    for aid,cid,meta,body in entries:
        text=('\n=== '+cats[cid]+' ===\n\n' if cid!=lastcat else '')+meta['title']+'\n\n'+strip(body)+'\n\n'
        text=breaks(text);lastcat=cid
        if current and len((current+text).encode('utf-8'))>24000:parts.append(current);current=''
        current+=text
    if current:parts.append(current)
    dest=SD/'library/field-guide';backup=ROOT/'.thai-work/original/library/field-guide';backup.mkdir(parents=True,exist_ok=True)
    for p in dest.glob('*.txt'):
        if not (backup/p.name).exists():(backup/p.name).write_bytes(p.read_bytes())
    for i,text in enumerate(parts,1):(dest/f'{i:04}.txt').write_text(text.strip()+'\n','utf-8',newline='\n')
    for p in dest.glob('*.txt'):
        if int(p.stem)>len(parts):p.unlink()
    for p in (SD/'wiki/articles').glob('*.md'):
        meta,body=article(p);front=plain(p.read_text('utf-8')).split('---',2)[1]
        p.write_text('---'+front+'---\n'+breaks(body)+'\n','utf-8',newline='\n')
    for p in (SD/'library/fm21-76').glob('*.txt'):
        s=plain(p.read_text('utf-8'))
        # Idempotent warning. Source translation is retained in full below it.
        if not s.startswith(HISTORICAL):s=HISTORICAL+'\n\n'+s
        p.write_text(breaks(s),'utf-8',newline='\n')
    (SD/'library/catalog.tsv').write_text('# id\ttitle\tparts\tpdf file\tsource\n'
        f'field-guide\tคู่มือเอาตัวรอดฉบับรวมทุกบท\t{len(parts)}\tfield-guide.pdf\tรวมบทความภาษาไทยทั้ง 118 เรื่อง พร้อมภาพประกอบ\n'
        'fm21-76\tคู่มือเอาชีวิตรอดกองทัพสหรัฐฯ FM 21-76\t62\tfm21-76.pdf\tเอกสารประวัติศาสตร์ปี 1992 แปลไทย พร้อมภาคผนวกต้นฉบับสำหรับอ้างอิงภาพ\n','utf-8',newline='\n')
    for p in [SD/'web/index.html',SD/'web/Sarabun-Regular.ttf']:
        p.with_suffix(p.suffix+'.gz').write_bytes(gzip.compress(p.read_bytes(),mtime=0))
    sizes={str(p.relative_to(SD)):p.stat().st_size for p in SD.glob('library/**/*.txt')}
    assert max(sizes.values())<=32768,max(sizes.items(),key=lambda x:x[1])
    report={'articles':len(entries),'field_guide_parts':len(parts),'manual_parts':62,'largest_document_bytes':max(sizes.values()),'wordbreaks':'PyThaiNLP newmm / U+200B','search_sha256':hashlib.sha256(raw).hexdigest()}
    (ROOT/'docs/thai-sd-build-report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),'utf-8')
    print(json.dumps(report,ensure_ascii=True))
