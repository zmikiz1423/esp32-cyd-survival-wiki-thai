"""Reproducible paragraph-preserving English -> Thai content migration.

The translation service provides a draft; editorial corrections are applied
afterward. Source backups and translation cache are kept outside the SD image.
"""
from pathlib import Path
import concurrent.futures
import json
import re
import shutil
import time
import urllib.parse
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / '.thai-work'
CACHE = WORK / 'wiki-cache'
CACHE.mkdir(parents=True, exist_ok=True)

TITLES = {
'rule-of-threes':'กฎสามประการเพื่อเอาชีวิตรอด',
'stop':'STOP: หยุด คิด สังเกต วางแผน',
'survival-kit':'ชุดอุปกรณ์เอาชีวิตรอด',
'weather-hazards':'ฟ้าผ่าและภัยจากสภาพอากาศ',
'knots':'เงื่อนเชือกที่ควรรู้',
'animal-encounters':'เมื่อพบสัตว์อันตราย',
'falling-through-ice':'น้ำแข็งบางและการตกน้ำเย็นจัด',
'staying-calm':'ตั้งสติและรักษากำลังใจ',
'power-outage':'เหตุฉุกเฉินในบ้านและไฟดับ',
'finding-water':'การหาแหล่งน้ำ',
'purifying-water':'ทำน้ำให้ปลอดภัยสำหรับดื่ม',
'improvised-filter':'ทำเครื่องกรองน้ำอย่างง่าย',
'solar-still':'กลั่นน้ำด้วยแสงอาทิตย์',
'dehydration':'ภาวะขาดน้ำและปริมาณน้ำที่ต้องการ',
'water-storage':'การสำรองน้ำไว้ใช้ในบ้าน',
'fire-basics':'พื้นฐานการก่อไฟ',
'fire-lays':'รูปแบบการจัดกองฟืน',
'starting-fire':'วิธีจุดไฟ',
'fire-safety':'ใช้ไฟอย่างปลอดภัย',
'cooking-fire':'ทำอาหารบนกองไฟ',
'wet-weather-fire':'ก่อไฟในสภาพอากาศเปียกชื้น',
'shelter-basics':'เลือกที่ตั้งและสร้างที่พัก',
'nuclear-get-inside':'เหตุนิวเคลียร์: เข้าที่กำบังและติดตามข่าว',
'nuclear-decon':'ชำระฝุ่นกัมมันตรังสีอย่างปลอดภัย',
'nuclear-time':'ฝุ่นกัมมันตรังสี: หลบภัยนานเท่าใด',
'lean-to':'ที่พักแบบเพิงลาด',
'debris-hut':'กระท่อมใบไม้และกิ่งไม้',
'tarp-shelters':'ทำที่พักจากผ้าใบหรือเสื้อกันฝน',
'cold-weather':'ที่พักในอากาศหนาวและหิมะ',
'hot-weather':'ที่พักในทะเลทรายและอากาศร้อน',
'sleeping-warm':'นอนให้อบอุ่น',
'snow-cave':'ถ้ำหิมะและกระท่อมหิมะ',
'first-aid-priorities':'ลำดับความสำคัญในการปฐมพยาบาล',
'cpr':'การช่วยฟื้นคืนชีพ (CPR)',
'severe-bleeding':'การห้ามเลือดที่ออกรุนแรง',
'choking':'สิ่งแปลกปลอมอุดกั้นทางเดินหายใจ',
'shock':'ภาวะช็อก',
'hypothermia':'ตัวเย็นเกินและเนื้อเยื่อถูกความเย็น',
'heat-illness':'อ่อนเพลียจากความร้อนและฮีตสโตรก',
'burns':'แผลไหม้และน้ำร้อนลวก',
'fractures-sprains':'กระดูกหักและข้อแพลง',
'wound-care':'ดูแลบาดแผลและป้องกันการติดเชื้อ',
'snakebite':'เมื่องูกัด',
'anaphylaxis':'ภาวะแพ้รุนแรงเฉียบพลัน',
'head-injury':'บาดเจ็บศีรษะและสมองกระทบกระเทือน',
'bites-stings':'เห็บ แมลงต่อย และแมงมุมกัด',
'seizures':'อาการชัก',
'diarrhea-vomiting':'ท้องเสีย อาเจียน และการชดเชยน้ำ',
'eye-injuries':'การบาดเจ็บที่ดวงตา',
'drowning':'จมน้ำและการช่วยคนตกน้ำ',
'electric-shock':'ไฟฟ้าดูด',
'poisoning':'สารพิษและการใช้ยาเกินขนาด',
'diabetic-emergency':'เหตุฉุกเฉินจากเบาหวาน',
'stroke-heart-attack':'หลอดเลือดสมองและหัวใจขาดเลือด',
'spinal-injury':'การบาดเจ็บกระดูกสันหลัง',
'food-priorities':'อาหารในสถานการณ์เอาชีวิตรอด',
'foraging-safely':'หาพืชอาหารอย่างปลอดภัย',
'insects-fishing':'แมลง ปลา และสัตว์เล็กเป็นอาหาร',
'food-storage':'เก็บอาหารและดูแลค่ายให้ปลอดภัย',
'emergency-food':'สำรองอาหารสำหรับเหตุฉุกเฉิน',
'food-after-outage':'ความปลอดภัยของอาหารหลังไฟดับ',
'shadow-stick':'หาทิศด้วยเงาไม้',
'watch-method':'หาทิศด้วยนาฬิกา',
'stars':'หาทิศจากดวงดาว',
'map-compass':'พื้นฐานแผนที่และเข็มทิศ',
'staying-found':'เดินทางอย่างไรไม่ให้หลง',
'natural-direction':'เบาะแสทิศทางและแสงจากธรรมชาติ',
'ground-signals':'สัญญาณภาคพื้นสำหรับอากาศยาน',
'signal-mirror':'กระจกส่งสัญญาณ',
'sos-signals':'ส่งสัญญาณด้วย SOS นกหวีด ไฟ และควัน',
'emergency-radio':'ใช้วิทยุและโทรศัพท์ในเหตุฉุกเฉิน',
'night-signals':'ส่งสัญญาณในเวลากลางคืน',
'helping-searchers':'ทำให้ทีมค้นหาพบคุณง่ายขึ้น',
'goodboy-heat':'ฮีตสโตรกในสุนัข',
'goodboy-toxic':'สารพิษและอาหารอันตรายต่อสุนัข',
'goodboy-choking':'สุนัขสำลักและทางเดินหายใจอุดตัน',
'goodboy-wounds':'แผล เลือดออก และอุ้งเท้าสุนัข',
'goodboy-cpr':'การช่วยฟื้นคืนชีพสุนัข',
'goodboy-cold':'ดูแลสุนัขในอากาศหนาว',
'goodboy-pack':'กระเป๋าฉุกเฉินสำหรับสุนัข',
'tornado':'รับมือพายุทอร์นาโด',
'flood':'น้ำท่วมและน้ำป่าไหลหลาก',
'hurricane':'เฮอริเคนและคลื่นพายุซัดฝั่ง',
'wildfire':'ไฟป่าและควันไฟ',
'blizzard':'พายุหิมะและพายุน้ำแข็ง',
'earthquake':'แผ่นดินไหว',
'extreme-heat':'คลื่นความร้อนรุนแรง',
'stranded-winter':'ติดอยู่ในรถช่วงฤดูหนาว',
'car-breakdown':'รถเสียระหว่างทาง',
'flat-tire':'เปลี่ยนยางรถที่แบน',
'jump-start':'พ่วงแบตเตอรี่สตาร์ตรถ',
'car-fire':'รถเกิดไฟไหม้',
'car-in-water':'เมื่อรถตกน้ำ',
'car-kit':'ชุดฉุกเฉินประจำรถ',
'gas-leak':'แก๊สรั่ว',
'house-fire':'ไฟไหม้บ้าน: ออกจากอาคารให้เร็ว',
'burst-pipe':'ท่อน้ำแตกและน้ำในท่อแข็งตัว',
'carbon-monoxide':'พิษคาร์บอนมอนอกไซด์',
'generator-safety':'ใช้เครื่องปั่นไฟอย่างปลอดภัย',
'utility-shutoffs':'ปิดระบบแก๊ส ไฟฟ้า และน้ำ',
'downed-power-lines':'สายไฟฟ้าขาดตกพื้น',
'72-hour-kit':'ชุดฉุกเฉินสำหรับ 72 ชั่วโมง',
'radio-basics':'เลือกวิทยุสำหรับเหตุฉุกเฉิน',
'frs-gmrs':'วิทยุสื่อสาร FRS และ GMRS',
'ham-radio':'วิทยุสมัครเล่นในเหตุฉุกเฉิน',
'radio-calls':'เรียกขอความช่วยเหลือทางวิทยุ',
'noaa-weather-radio':'วิทยุและประกาศเตือนอากาศ NOAA',
'mesh-messaging':'ส่งข้อความระหว่างบอร์ดผ่านเมช',
'radio-range':'เพิ่มระยะรับส่งวิทยุ',
'children-first-aid':'ข้อควรรู้เมื่อปฐมพยาบาลเด็ก',
'child-fever':'เด็กมีไข้',
'baby-care':'ดูแลทารกเมื่อไฟดับ',
'kids-calm':'ช่วยเด็กตั้งสติและรับมือความเครียด',
'family-plan':'แผนฉุกเฉินสำหรับครอบครัว',
'lost-child':'เด็กพลัดหลงหรือสูญหาย',
'elders-special-needs':'ผู้สูงอายุและผู้ต้องการความช่วยเหลือพิเศษ',
'emergency-toilet':'ห้องน้ำฉุกเฉินและการจัดการของเสีย',
'handwashing':'ล้างมือเมื่อมีน้ำน้อย',
}

def translate(text):
    url = 'https://translate.googleapis.com/translate_a/single?' + urllib.parse.urlencode(
        {'client':'gtx','sl':'en','tl':'th','dt':'t','q':text})
    for attempt in range(7):
        try:
            req = urllib.request.Request(url, headers={'User-Agent':'Mozilla/5.0'})
            with urllib.request.urlopen(req, timeout=45) as response:
                result = json.load(response)
            return ''.join(item[0] for item in result[0] if item and item[0])
        except Exception:
            if attempt == 6: raise
            time.sleep(2 + attempt * 2)

def translate_lines(lines, key):
    cache = CACHE / (key + '.json')
    if cache.exists():
        result = json.loads(cache.read_text(encoding='utf-8'))
        if result.get('source') == lines: return result['target']
    prefixes = []
    texts = []
    for line in lines:
        match = re.match(r'^(\s*(?:#{1,6} |[-*] |> |\d+\. ))?(.*)$', line)
        prefixes.append(match[1] or '')
        texts.append(match[2])
    query = '\n'.join('⟦%04d⟧ %s' % (i,t) for i,t in enumerate(texts))
    out = translate(query)
    matches = list(re.finditer(r'⟦\s*(\d{4})\s*⟧', out))
    if [int(m[1]) for m in matches] == list(range(len(texts))):
        translated = [out[m.end():matches[i+1].start() if i+1<len(matches) else len(out)].strip() for i,m in enumerate(matches)]
    else:
        translated = [translate(t) if t else '' for t in texts]
    target = [prefix + text for prefix,text in zip(prefixes,translated)]
    cache.write_text(json.dumps({'source':lines,'target':target},ensure_ascii=False,indent=2),encoding='utf-8')
    return target

def article(path):
    original = WORK / 'original' / path.relative_to(ROOT)
    original.parent.mkdir(parents=True, exist_ok=True)
    if not original.exists(): shutil.copy2(path, original)
    source = original.read_text(encoding='utf-8-sig')
    _,front,body = source.split('---',2)
    fields = dict(line.split(':',1) for line in front.strip().splitlines())
    fields = {k:v.strip() for k,v in fields.items()}
    fields['title'] = TITLES[path.stem]
    fields['tags'], fields['summary'] = translate_lines([fields['tags'], fields['summary']], path.stem+'-meta')
    raw_lines = body.strip().splitlines()
    indices = [i for i,line in enumerate(raw_lines) if line.strip()]
    lines = [raw_lines[i] for i in indices]
    # Keep all destinations out of the translation engine.
    urls = []
    def protect(m):
        urls.append(m[1])
        return '(QQQURL%03dQQQ)' % (len(urls)-1)
    protected = [re.sub(r'\]\(([^)]+)\)',lambda m: ']'+protect(m),line) for line in lines]
    target = translate_lines(protected, path.stem)
    for i,line in enumerate(target):
        prefix = re.match(r'^(\s*(?:#{1,6} |[-*] |> |\d+\. ))', protected[i])
        if prefix:
            line = prefix[0] + re.sub(r'^\s*(?:#{1,6} |[-*] |> |\d+\. )','',line)
        for j,url in enumerate(urls):
            line = re.sub(r'QQQ\s*URL\s*%03d\s*QQQ'%j,lambda m:url,line,flags=re.I)
        line = re.sub(r'\]\s+\(', '](', line)
        if 'QQQ' in line: raise ValueError('URL placeholder damaged: '+path.name)
        raw_lines[indices[i]] = line
    result = '---\n'+'\n'.join(k+': '+v for k,v in fields.items())+'\n---\n'+'\n'.join(raw_lines)+'\n'
    # No broken image paths, links, lost warnings or lost numbered steps.
    for pattern in [r'\]\(([^)]+)\)',r'^> ',r'^\d+\. ',r'^[-*] ',r'^#{1,6} ']:
        a = re.findall(pattern,source,re.M)
        b = re.findall(pattern,result,re.M)
        if a != b: raise ValueError(path.name+' structural check failed: '+pattern)
    if not re.search('[ก-๙]', result): raise ValueError('No Thai: '+path.name)
    path.write_text(result,encoding='utf-8')
    return path.name

def main():
    paths=sorted((ROOT/'sdcard/wiki/articles').glob('*.md'))
    assert len(paths)==len(TITLES)==118
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        for i,name in enumerate(pool.map(article,paths)):
            print('%d/%d %s'%(i+1,len(paths),name),flush=True)
    catalog = ROOT/'sdcard/wiki/catalog.tsv'
    backup = WORK/'original/sdcard/wiki/catalog.tsv'
    if not backup.exists(): shutil.copy2(catalog,backup)
    rows=[]
    for line in backup.read_text(encoding='utf-8-sig').splitlines():
        if line.startswith('#'):
            rows.append('# id\tcategory\ttitle')
        elif line.strip():
            ident,category,title=line.split('\t')
            rows.append('\t'.join([ident,category,TITLES[ident]]))
    catalog.write_text('\n'.join(rows)+'\n',encoding='utf-8')
    (WORK/'thai-titles.json').write_text(json.dumps(TITLES,ensure_ascii=False,indent=2),encoding='utf-8')

if __name__=='__main__': main()
