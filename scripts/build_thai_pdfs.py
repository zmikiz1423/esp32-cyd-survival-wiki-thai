"""Create Thai PDFs with real HarfBuzz glyph positioning and embedded Sarabun.

Uses ReportLab for PDF authoring. The complete original FM PDF is retained as
an explicitly labelled reference appendix, so none of its original figures
or tables disappear when the Thai text edition is added.
"""
from pathlib import Path
import re,sys,json,functools,shutil
import uharfbuzz as hb
from pythainlp.tokenize import word_tokenize
from reportlab.pdfgen import canvas
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont,shapeStr
from reportlab.lib.colors import HexColor
from reportlab.lib.pagesizes import A4
from reportlab.lib.utils import ImageReader
from pypdf import PdfReader,PdfWriter
from build_thai_sd import article,plain,HISTORICAL

ROOT=Path(__file__).resolve().parents[1];SD=ROOT/'sdcard';TMP=ROOT/'tmp/pdfs';TMP.mkdir(parents=True,exist_ok=True)
FONT=ROOT/'firmware/fonts/Sarabun-Regular.ttf';NAME='SarabunThai'
pdfmetrics.registerFont(TTFont(NAME,str(FONT),shapable=True))
face=hb.Face(FONT.read_bytes());font=hb.Font(face);font.scale=(1000,1000);hb.ot_font_set_funcs(font)
W,H=A4;LEFT=46;RIGHT=W-46;TOP=H-66;BOTTOM=48

@functools.lru_cache(maxsize=32768)
def units(s):
    total=0
    for run in script_runs(s):
        b=hb.Buffer();b.add_str(run);b.guess_segment_properties();hb.shape(font,b)
        total+=sum(p.x_advance for p in b.glyph_positions)
    return total
def script_runs(s):
    # HarfBuzz guesses one script per buffer. A Latin prefix such as FM 21-76
    # must not disable Thai SARA AM decomposition or contextual mark forms.
    return re.findall(r'[\u0e00-\u0e7f]+|[^\u0e00-\u0e7f]+',plain(s))
def width(s,size):return units(s)*size/1000
def clusters(s):
    return re.findall(r'[\u0e40-\u0e44]?[\s\S][\u0e31\u0e34-\u0e3a\u0e47-\u0e4e\u0e33]*',s)
def lines(s,size,maxw):
    tokens=word_tokenize(plain(s),engine='newmm',keep_whitespace=True)
    result=[];line=''
    for token in tokens:
        if width(line+token,size)<=maxw:line+=token;continue
        if line.strip():result.append(line.rstrip());line=''
        if width(token,size)<=maxw:line=token.lstrip();continue
        for cluster in clusters(token):
            if width(line+cluster,size)>maxw and line:result.append(line);line=''
            line+=cluster
    if line.strip():result.append(line.rstrip())
    assert all(width(x,size)<=maxw+0.1 for x in result)
    return result

class Book:
    def __init__(self,path,title):
        self.c=canvas.Canvas(str(path),pagesize=A4,pageCompression=1)
        self.c.setTitle(title);self.c.setAuthor('Survival Wiki - ฉบับภาษาไทย');self.title=title;self.page=0;self.y=TOP
        self.newpage()
    def text(self,s,x,y,size=12,color='#182820'):
        self.c.setFillColor(HexColor(color))
        logical=plain(s)
        # Shaped glyphs may use private font codepoints. Preserve the original
        # Unicode string for PDF selection, copying, search and accessibility.
        actual='feff'+logical.encode('utf-16-be').hex()
        self.c._code.append('/Span << /ActualText <'+actual+'> >> BDC')
        # Pass ShapedStr directly to the PDF text object so mark/mkmk offsets
        # are retained even when optional RTL packages are not installed.
        for run in script_runs(s):
            t=self.c.beginText(x,y);t.setFont(NAME,size)
            t.textOut(shapeStr(run,NAME,size,force=True));self.c.drawText(t)
            x+=units(run)*size/1000
        self.c._code.append('EMC')
    def newpage(self):
        if self.page:self.c.showPage()
        self.page+=1;self.y=TOP
        self.c.setStrokeColor(HexColor('#cfdbd2'));self.c.line(LEFT,H-43,RIGHT,H-43)
        self.text(self.title,LEFT,H-32,9,'#526358')
        self.text(str(self.page),RIGHT-20,25,9,'#526358')
    def need(self,h):
        if self.y-h<BOTTOM:self.newpage()
    def paragraph(self,s,size=12,heading=False,warning=False,indent=0):
        s=plain(s).strip()
        if not s:return
        s=re.sub(r'\[([^]]+)\]\([^)]+\)',r'\1',s)
        s=s.replace('**','').replace('`','').replace('__','')
        leading=size*1.7;ls=lines(s,size,RIGHT-LEFT-indent)
        if heading:self.need(min(3,len(ls))*leading+18)
        for line in ls:
            self.need(leading)
            if warning:
                self.c.setFillColor(HexColor('#fff3df'));self.c.rect(LEFT-6,self.y-leading+3,RIGHT-LEFT+12,leading,fill=1,stroke=0)
            self.text(line,LEFT+indent,self.y-size*.25,size,'#ad3c16' if heading else '#674110' if warning else '#182820')
            self.y-=leading
        self.y-=5 if heading else 3
    def heading(self,s,level=1,key=None):
        self.y-=8
        self.paragraph(s,18 if level==1 else 14,heading=True)
        if key:
            self.c.bookmarkPage(key);self.c.addOutlineEntry(plain(s),key,0,False)
    def picture(self,path,caption):
        image=ImageReader(str(path));iw,ih=image.getSize();dw=min(350,RIGHT-LEFT);dh=dw*ih/iw
        if dh>430:dw*=430/dh;dh=430
        self.need(dh+45)
        self.c.drawImage(image,LEFT+(RIGHT-LEFT-dw)/2,self.y-dh,width=dw,height=dh,preserveAspectRatio=True)
        self.y-=dh+8
        self.paragraph(caption,10);self.y-=5
    def cover(self,subtitle,notes):
        self.y=H-145;self.heading(self.title);self.paragraph(subtitle,15)
        self.y-=24
        for note in notes:self.paragraph(note,12)
        self.newpage()
    def save(self):self.c.save();return self.page

def field_guide():
    path=SD/'library/field-guide/field-guide.pdf';book=Book(path,'คู่มือเอาตัวรอด - ฉบับภาษาไทย')
    book.cover('คู่มือภาคสนาม 118 เรื่อง พร้อมภาพประกอบภาษาไทย',[
        'สำหรับโครงการ ESP32 CYD Survival Wiki อ่านได้โดยไม่ต้องเชื่อมต่ออินเทอร์เน็ต',
        'เรียบเรียงคำแปลโดยคงลำดับการปฏิบัติ ตัวเลข และข้อควรระวังตามเนื้อหาเดิม ข้อมูลเฉพาะภูมิภาคยังระบุบริบทของต้นฉบับไว้',
        'ใช้สารบัญของไฟล์ PDF เพื่อเลือกบทที่ต้องการ และดูบทความฉบับเดียวกันบนหน้าจอ CYD หรือหน้าเว็บของอุปกรณ์ได้'])
    for l in (SD/'wiki/catalog.tsv').read_text('utf-8').splitlines():
        if not l or l.startswith('#'):continue
        aid,cid,title=l.split('\t',2);meta,body=article(SD/f'wiki/articles/{aid}.md')
        book.heading(title,key=aid)
        for line in body.splitlines():
            if not line.strip():book.y-=5;continue
            image=re.match(r'!\[([^]]*)\]\(([^)]+)\)',line)
            if image:book.picture(SD/'wiki'/image[2],image[1]);continue
            if line.startswith('#'):book.heading(line.lstrip('# ').strip(),2)
            elif line.startswith('>'):book.paragraph(line.lstrip('> '),warning=True)
            elif re.match(r'^[-*] ',line):book.paragraph('• '+line[2:],indent=8)
            else:book.paragraph(line)
        book.y-=12
    return book.save()

def manual():
    dest=SD/'library/fm21-76/fm21-76.pdf';source=ROOT/'.thai-work/original/library/fm21-76/fm21-76.pdf'
    source.parent.mkdir(parents=True,exist_ok=True)
    if not source.exists():
        assert len(PdfReader(dest).pages)==233,'Original appendix backup missing'
        shutil.copy2(dest,source)
    textpdf=TMP/'fm21-76-thai-text.pdf';book=Book(textpdf,'FM 21-76 - คู่มือเอาชีวิตรอดฉบับแปลไทย')
    book.cover('เอกสารกองทัพบกสหรัฐอเมริกา ปี 1992',[
        HISTORICAL,
        'ฉบับภาษาไทยแปลข้อความทั้ง 62 ส่วนของโครงการเดิม โดยไม่ย่อเนื้อหา คำแปลใช้ระบบแปลร่วมกับการเกลาศัพท์และตรวจข้อความที่อาจคลาดเคลื่อน ยังไม่ใช่งานแปลที่ผ่านผู้เชี่ยวชาญตรวจทุกบรรทัด',
        'ภาคผนวกท้ายเล่มเก็บต้นฉบับภาษาอังกฤษครบ 233 หน้า เพื่อรักษาภาพประกอบ ตาราง แหล่งอ้างอิง และตำแหน่งหน้าต้นฉบับ'])
    for i in range(1,63):
        book.heading(f'ตอนที่ {i} จาก 62',key=f'part-{i}')
        text=plain((SD/f'library/fm21-76/{i:04}.txt').read_text('utf-8'))
        # The cover states the warning; keep one short reminder in each part.
        if text.startswith(HISTORICAL):text=text[len(HISTORICAL):].lstrip()
        book.paragraph('เอกสารประวัติศาสตร์ปี 1992 โปรดอ่านคำเตือนต้นเล่ม',10,warning=True)
        for para in re.split(r'\n\s*\n',text):
            para=para.strip()
            if not para:continue
            if len(para)<105 and not re.search('[.!?]$',para):book.heading(para,2)
            else:book.paragraph(para.replace('\n',' '))
    book.newpage();book.heading('ภาคผนวก - ต้นฉบับภาษาอังกฤษ 233 หน้า',key='original-appendix')
    book.paragraph('ภาพประกอบและตารางจากเอกสารต้นฉบับเก็บไว้ครบเพื่อใช้เทียบกับข้อความแปลไทย เลขหน้าที่ปรากฏในข้อความอ้างถึงเลขหน้าของต้นฉบับในภาคผนวกนี้')
    thai_pages=book.save();writer=PdfWriter();writer.append(textpdf);writer.append(source)
    writer.add_metadata({'/Title':'FM 21-76 ฉบับแปลไทย พร้อมภาคผนวกต้นฉบับ','/Author':'Survival Wiki - Thai edition'})
    with dest.open('wb') as f:writer.write(f)
    return thai_pages,thai_pages+233

if __name__=='__main__':
    report={'field_guide_pages':field_guide()}
    report['manual_thai_pages'],report['manual_total_pages']=manual()
    (ROOT/'docs/thai-pdf-build-report.json').write_text(json.dumps(report,indent=2),'utf-8')
    print(report)
