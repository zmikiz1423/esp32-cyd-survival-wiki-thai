"""Exercise the generated firmware tables against HarfBuzz and 240px layout.

This is host verification, not a physical display or touch test.
"""
from pathlib import Path
import re,json,math
import uharfbuzz as hb
from PIL import Image,ImageDraw
from fontTools.ttLib import TTFont

ROOT=Path(__file__).resolve().parents[1]
DATA=(ROOT/'firmware/src/generated/thai_font_data.cpp').read_text('utf-8')
def array(name):
    return re.search(r'\b'+name+r'\[\][^=]*=\s*\{(.*?)\n\};',DATA,re.S)[1]
def records(name):
    return [tuple(int(x,0) for x in r.split(',')) for r in re.findall(r'\{([^{}]+)\}',array(name))]
runs=records('thaiRuns'); shapes={r[0]:r[1:] for r in records('thaiShapes')}
cmap={r[0]:r[1:] for r in records('thaiCodepoints')}
atlases=records('thaiAtlases') if False else json.loads((ROOT/'.thai-work/font-report.json').read_text())['atlases']
sizes={}
for sz,baseline,height,_ in atlases:
    sizes[sz]=(baseline,height,records(f'thaiGlyphs{sz}'),bytes(int(x) for x in array(f'thaiPixels{sz}').replace('\n','').split(',') if x.strip()))
def mark(c):return ord(c)==0xE31 or 0xE34<=ord(c)<=0xE3A or 0xE47<=ord(c)<=0xE4E
def cluster_end(s,p):
    c=s[p];p+=1
    if 0xE40<=ord(c)<=0xE44 and p<len(s) and 0xE01<=ord(s[p])<=0xE2E:p+=1
    while p<len(s) and (mark(s[p]) or s[p] in '\u0e33\u200d'):p+=1
    return p
def glyph_run(s):
    p=0;pen=0;out=[]
    while p<len(s):
        cp=ord(s[p]);p+=1
        if cp in (0x200B,0xFEFF):continue
        sh=None
        if 0xE01<=cp<=0xE5B:
            key=(cp&255)<<24;sh=shapes.get(key);used=p;q=p
            for n in range(1,4):
                if q>=len(s):break
                nxt=ord(s[q]);q+=1
                if not (mark(chr(nxt)) or nxt in (0xE32,0xE33)):break
                key|=(nxt&255)<<(24-8*n)
                if key in shapes:sh=shapes[key];used=q
            p=used
        if sh:
            off,advance,count=sh
            for g,x,y in runs[off:off+count]:out.append((g,pen+x,y))
            pen+=advance
        else:
            g,advance=cmap.get(cp,cmap[ord('?')]);out.append((g,pen,0));pen+=advance
    return out,pen
def width(s,sz):return math.ceil(glyph_run(s)[1]*sz/1000)
def wrap(s,sz,w):
    lines=[];pos=0
    while pos<len(s):
        while pos<len(s) and s[pos]==' ':pos+=1
        if pos>=len(s):break
        p=pos;fit=pos;br=-1
        while p<len(s):
            e=cluster_end(s,p)
            if width(s[pos:e],sz)>w:break
            fit=e
            if s[p] in ' \u200b-':br=e
            p=e
        if fit<len(s) and br>pos:fit=br
        assert fit>pos,(s[pos:pos+30],w)
        lines.append(s[pos:fit]);pos=fit
    return lines
def draw(im,s,x,y,sz,color=(255,255,255)):
    baseline,height,bitmaps,pixels=sizes[sz]
    for gid,gx,gy in glyph_run(s)[0]:
        off,w,h,left,top=bitmaps[gid]
        # Firmware rounds positions, then rasterizes pre-built glyph bitmaps.
        x0=x+math.floor(gx*sz/1000+0.5)+left;y0=y+baseline-round(gy*sz/1000)-top
        for row in range(h):
            for col in range(w):
                v=pixels[off+row*((w+1)//2)+col//2]
                a=(v&15 if col&1 else v>>4)/15
                px=x0+col;py=y0+row
                if a and 0<=px<im.width and 0<=py<im.height:
                    bg=im.getpixel((px,py));im.putpixel((px,py),tuple(round(c*a+b*(1-a)) for c,b in zip(color,bg)))
    return width(s,sz)

if __name__=='__main__':
    sources=[*ROOT.glob('sdcard/wiki/articles/*.md'),*ROOT.glob('sdcard/library/**/*.txt'),*ROOT.glob('firmware/src/*.*')]
    texts=[p.read_text('utf-8',errors='strict') for p in sources]
    missing={c for s in texts for c in s if 0xE00<=ord(c)<=0xE7F and ord(c) not in cmap}
    assert not missing,missing
    # Verify the exact embedded lookup runs, including all standard Thai
    # consonant/vowel/tone combinations, against the upstream OpenType engine.
    face=hb.Face((ROOT/'firmware/fonts/Sarabun-Regular.ttf').read_bytes());font=hb.Font(face)
    font.scale=(1000,1000);hb.ot_font_set_funcs(font)
    # Recover atlas glyph -> original TTF glyph mapping from every shaped single
    # codepoint and cluster (some contextual glyphs have no Unicode codepoint).
    gidmap={};compared=0;extent_errors=[]
    for key,(off,advance,count) in shapes.items():
        string=''.join(chr(0xE00+b) for b in key.to_bytes(4,'big') if b)
        b=hb.Buffer();b.add_str(string);b.guess_segment_properties();hb.shape(font,b,{'kern':False})
        assert len(b.glyph_infos)==count
        x=0
        for (idx,gx,gy),g,p in zip(runs[off:off+count],b.glyph_infos,b.glyph_positions):
            assert gx==x+p.x_offset and gy==p.y_offset,(string,gx,gy,p)
            assert gidmap.get(idx,g.codepoint)==g.codepoint;gidmap[idx]=g.codepoint
            x+=p.x_advance
        assert x==advance
        for sz,(base,h,bitmaps,pix) in sizes.items():
            for idx,gx,gy in runs[off:off+count]:
                _,w,gh,left,top=bitmaps[idx]
                yy=base-round(gy*sz/1000)-top
                if yy<0 or yy+gh>h:extent_errors.append((string,sz,yy,gh,h))
        compared+=1
    assert not extent_errors,extent_errors[:20]
    # Whole-word shaping can involve contextual substitutions outside a single
    # cluster. Compare actual SD words to HarfBuzz as well as isolated cases.
    words={m[0] for p,s in zip(sources,texts) if 'sdcard' in p.parts for m in re.finditer('[\u0e01-\u0e5b]+',s)}
    mismatches=[]
    for word in words:
        b=hb.Buffer();b.add_str(word);b.guess_segment_properties();hb.shape(font,b,{'kern':False})
        expected=[];x=0
        for g,p in zip(b.glyph_infos,b.glyph_positions):
            expected.append((g.codepoint,x+p.x_offset,p.y_offset));x+=p.x_advance
        actual,advance=glyph_run(word)
        actual=[(gidmap.get(g,-1),gx,gy) for g,gx,gy in actual]
        if actual!=expected or advance!=x:mismatches.append(word)
    assert not mismatches,repr(mismatches[:20])
    wrapped=0
    for source in texts:
        for raw in source.splitlines():
            if not any('\u0e01'<=c<='\u0e5b' for c in raw):continue
            for sz,w in ((14,224),(18,206),(18,210),(22,224)):
                lines=wrap(raw,sz,w)
                assert ''.join(lines).replace(' ','')==raw.strip().replace(' ',''),raw[:100]
                for line in lines:
                    assert width(line,sz)<=w
                    assert not mark(line[0]),repr(line)
                wrapped+=len(lines)
    assert max(g[1] for _,_,bs,_ in sizes.values() for g in bs)<=48
    assert max(g[2] for _,_,bs,_ in sizes.values() for g in bs)<=48
    sample='ปู่ ปุ๋ย ญี่ปุ่น ผู้ใหญ่ น้ำ กุ้ง กิ๊บ กึ๋น เกื้อหนุน ฐาน ฤๅษี'
    pages=[]
    home=Image.new('RGB',(240,320),'#080c10');d=ImageDraw.Draw(home)
    d.rectangle((0,0,239,35),fill='#212830');draw(home,'คู่มือเอาตัวรอด',8,2,16)
    titles=['ประเมินเหตุ','เริ่มต้น','น้ำ','ก่อไฟ','ที่พัก','ปฐมพยาบาล']
    for i,title in enumerate(titles):
        x=6+(i%2)*117;y=42+(i//2)*64
        d.rounded_rectangle((x,y,x+110,y+57),6,fill='#2b5940' if i else '#be2828')
        draw(home,title,x+5,y+3,14);draw(home,'คู่มือภาษาไทย',x+5,y+31,12)
    for i,t in enumerate(['คู่มือ','Wi-Fi','ไฟ','เมช','แผนที่','ตั้งค่า']):draw(home,t,2+i*40,289,12)
    pages.append(home)
    article=Image.new('RGB',(240,320),'#080c10');d=ImageDraw.Draw(article)
    d.rectangle((0,0,239,35),fill='#212830');draw(article,'ทดสอบอักษรไทย',8,2,16)
    y=36
    for line in wrap(sample,18,224):draw(article,line,8,y,18);y+=38
    for line in wrap('สระและวรรณยุกต์ต้องอยู่ครบ ไม่ขาด ไม่ลอย และตัดบรรทัดพอดีหน้าจอ',18,224):
        if y+36>282:break
        draw(article,line,8,y,18);y+=38
    d.rectangle((0,282,239,319),fill='#212830');draw(article,'กลับ       50%       ต่อ',8,288,12)
    pages.append(article)
    proof=Image.new('RGB',(240,320),'#f6f4eb');y=2
    for sz in sizes:
        for line in wrap('ปุ๋ย ญิ๋ว น้ำ กุ้ง ผู้ใหญ่',sz,224):draw(proof,line,8,y,sz,(20,35,25));y+=sizes[sz][1]
    pages.append(proof)
    sheet=Image.new('RGB',(760,360),'#333333')
    for i,im in enumerate(pages):sheet.paste(im,(10+i*250,20))
    out=ROOT/'docs/thai-render-preview.png';sheet.save(out)
    report={'shaping_cases':compared,'whole_thai_words':len(words),'whole_word_mismatches':len(mismatches),'wrapped_lines':wrapped,'sources':len(sources),'missing_thai':len(missing),'vertical_extent_failures':len(extent_errors),'preview':str(out),'hardware_tested':False}
    (ROOT/'docs/thai-render-test-report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),'utf-8')
    print(json.dumps(report,ensure_ascii=True))
