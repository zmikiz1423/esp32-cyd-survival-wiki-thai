"""Reproducible, resumable Thai translation of the original FM 21-76 text.

Originals and service responses stay under .thai-work/, never on the SD card.
The segment markers are verified before any source file is replaced.
"""
from __future__ import annotations

import concurrent.futures
import hashlib
import json
import re
import time
import urllib.parse
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "sdcard/library/fm21-76"
WORK = ROOT / ".thai-work"
ORIGINAL = WORK / "original/library/fm21-76"
CACHE = WORK / "translate-cache"
MAX_CHARS = 3000

# English source terms with a specialised meaning in this manual. Protect them
# from common literal translations (e.g. "still" as a photograph or "game" as a game).
GLOSSARY = {
    "size up": "ประเมิน",
    "improvisation": "การประยุกต์ใช้สิ่งที่มีอยู่",
    "improvising": "ประยุกต์ใช้สิ่งที่มีอยู่",
    "improvise": "ประยุกต์ใช้สิ่งที่มีอยู่",
    "improvised": "ที่ประยุกต์ทำจากวัสดุที่มีอยู่",
    "field-expedient": "ที่ประยุกต์ใช้ในภาคสนาม",
    "survival kits": "ชุดอุปกรณ์เอาชีวิตรอด",
    "survival kit": "ชุดอุปกรณ์เอาชีวิตรอด",
    "survival medicine": "การแพทย์เพื่อการเอาชีวิตรอด",
    "firecraft": "ทักษะการก่อไฟ",
    "basic fire principles": "หลักพื้นฐานของการก่อไฟ",
    "fire materials": "วัสดุสำหรับก่อไฟ",
    "fire material": "วัสดุสำหรับก่อไฟ",
    "fire-starting equipment": "อุปกรณ์จุดไฟ",
    "tinder": "เชื้อไฟ",
    "kindling": "ไม้ชิ้นเล็กสำหรับต่อไฟ",
    "aboveground still": "เครื่องกลั่นน้ำเหนือพื้นดิน",
    "belowground still": "เครื่องกลั่นน้ำใต้พื้นดิน",
    "solar still": "เครื่องกลั่นน้ำพลังงานแสงอาทิตย์",
    "still construction": "การสร้างเครื่องกลั่นน้ำ",
    "stills": "เครื่องกลั่นน้ำ",
    "a still": "เครื่องกลั่นน้ำ",
    "the still": "เครื่องกลั่นน้ำ",
    "game": "สัตว์ป่าที่ล่าเป็นอาหาร",
    "snare wire": "ลวดทำบ่วงดักสัตว์",
    "snares": "บ่วงดักสัตว์",
    "snare": "บ่วงดักสัตว์",
    "lashing": "ผูกมัดด้วยเชือก",
    "lash": "ผูกมัดด้วยเชือก",
    "cordage": "เชือกและวัสดุผูกมัด",
    "signaling mirror": "กระจกส่งสัญญาณ",
    "friendly": "ฝ่ายเดียวกัน",
    "cover and concealment": "ที่กำบังและที่ซ่อนพราง",
    "solar blanket": "ผ้าห่มฉุกเฉินสะท้อนความร้อน",
    "butterfly sutures": "แถบปิดแผลแบบผีเสื้อ",
    "metal match": "แท่งจุดไฟ",
    "hand lens": "แว่นขยาย",
    "mouth-to-mouth": "การผายปอดแบบปากต่อปาก",
    "rescue breathing": "การช่วยหายใจ",
    "artificial respiration": "การช่วยหายใจ",
    "tourniquet": "สายรัดห้ามเลือด",
    "trench foot": "โรคเท้าเปื่อยจากความชื้นและความเย็น",
    "immersion foot": "โรคเท้าเปื่อยจากความชื้นและความเย็น",
    "whiteout": "ภาวะขาวโพลนจนแยกทิศทางไม่ได้",
    "windchill": "ความหนาวเย็นจากลม",
    "chilblains": "ผิวหนังอักเสบจากความเย็น",
    "chilblain": "ผิวหนังอักเสบจากความเย็น",
    "frostbite": "เนื้อเยื่อบาดเจ็บจากความเย็นจัด",
    "heat exhaustion": "ภาวะเพลียแดด",
    "heatstroke": "โรคลมร้อน",
    "heat stroke": "โรคลมร้อน",
    "clubs": "กระบอง",
    "club": "กระบอง",
    "bowls": "ชาม",
    "pots": "หม้อ",
    "pot": "หม้อ",
    "auger shell": "หอยเจดีย์",
    "school of fish": "ฝูงปลา",
    "schools of fish": "ฝูงปลา",
    "seasoned hardwood": "ไม้เนื้อแข็งที่แห้งสนิท",
    "seasoned wood": "ไม้ที่แห้งสนิท",
    "fire-plow": "การก่อไฟด้วยวิธีถูร่องไม้",
    "bow and drill": "การก่อไฟด้วยคันธนูและแกนหมุน",
    "tepee": "กระโจม",
    "cross-ditch": "ร่องไขว้",
    "mirage": "ภาพลวงตาในทะเลทราย",
    "windward": "ด้านรับลม",
    "leeward": "ด้านอับลม",
    "treadle": "แป้นเหยียบ",
    "deadfall": "กับดักแบบทับ",
    "spring pole": "ไม้ดีดสำหรับกับดัก",
    "noose": "บ่วงรูด",
    "birdlime": "ยางเหนียวสำหรับดักนก",
    "pemmican": "อาหารพกพาจากเนื้อแห้งผสมไขมัน",
    "jerky": "เนื้อแห้ง",
    "bedding": "วัสดุรองนอน",
    "transpiration": "การคายน้ำของพืช",
    "charred cloth": "ผ้าเผาเป็นถ่าน",
    "gill net": "ตาข่ายดักปลา",
    "weir": "ฝายดักปลา",
    "gorge hook": "เบ็ดตรงแบบกลืนขวาง",
    "resin": "ยางไม้เรซิน",
    "cattail": "ธูปฤาษี",
    "scurvy": "โรคลักปิดลักเปิด",
    "rain shadow": "เขตอับฝน",
    "biological agents": "สารชีวภาพ",
    "biological agent": "สารชีวภาพ",
    "chemical agents": "สารเคมีที่ใช้เป็นอาวุธ",
    "chemical agent": "สารเคมีที่ใช้เป็นอาวุธ",
    "agents": "สารก่ออันตราย",
    "agent": "สารก่ออันตราย",
    "nerve agents": "สารพิษต่อระบบประสาท",
    "blood agents": "สารพิษต่อระบบเลือด",
    "choking agents": "สารพิษต่อระบบหายใจ",
    "blister agents": "สารพุพอง",
    "mustard gas": "แก๊สมัสตาร์ด",
    "decontamination": "การชำระล้างสารปนเปื้อน",
    "depressions in the ground": "แอ่งต่ำบนพื้นดิน",
    "depressions": "แอ่งต่ำ",
    "spines": "เงี่ยง",
    "positively identify": "ระบุให้แน่ชัด",
    "positive identification": "การระบุให้แน่ชัด",
    "radiation sickness": "อาการป่วยจากรังสี",
    "radioactive fallout": "ฝุ่นกัมมันตรังสี",
    "fallout": "ฝุ่นกัมมันตรังสี",
    "crater": "หลุมระเบิด",
    "lethal dose": "ปริมาณที่ทำให้เสียชีวิต",
    "lethal doses": "ปริมาณที่ทำให้เสียชีวิต",
    "incapacitating dose": "ปริมาณที่ทำให้หมดความสามารถ",
    "dose": "ปริมาณที่ได้รับ",
    "doses": "ปริมาณที่ได้รับ",
    "blast overpressure": "แรงดันเกินจากแรงระเบิด",
    "flash blindness": "ตาพร่าหรือตาบอดจากแสงวาบ",
    "staff or branch": "ท่อนไม้หรือกิ่งไม้",
    "straight-grained hardwood": "ไม้เนื้อแข็งเสี้ยนตรง",
    "north star": "ดาวเหนือ",
    "big dipper": "กลุ่มดาวกระบวยใหญ่",
    "little dipper": "กลุ่มดาวกระบวยเล็ก",
    "true north": "ทิศเหนือจริง",
    "magnetic declination": "มุมเบี่ยงเบนแม่เหล็ก",
    "grid north": "ทิศเหนือกริด",
    "back azimuth": "มุมทิศย้อนกลับ",
    "banana or plantain trees": "ต้นกล้วยหรือกล้วยกล้าย",
    "forked sticks": "ไม้ง่าม",
    "forked stick": "ไม้ง่าม",
    "twitch-up snare": "บ่วงแบบไม้ดีด",
    "twitch-up": "ไม้ดีด",
    "game trails": "ทางเดินสัตว์ป่า",
    "game trail": "ทางเดินสัตว์ป่า",
    "wild game": "สัตว์ป่า",
    "slight relief": "ความต่างระดับเล็กน้อย",
    "half-value layer thickness": "ความหนาครึ่งค่าของวัสดุกำบังรังสี",
    "half-value layer": "ชั้นวัสดุที่ลดรังสีลงครึ่งหนึ่ง",
    "canteen cup": "ถ้วยโลหะครอบกระติกน้ำ",
    "canteens": "กระติกน้ำ",
    "canteen": "กระติกน้ำ",
    "field dressing": "ผ้าปิดแผลสนาม",
    "pressure dressing": "ผ้าปิดแผลกดห้ามเลือด",
    "dressings": "ผ้าปิดแผล",
    "dressing": "ผ้าปิดแผล",
    "compression bandage": "ผ้าพันแผลแบบกดรัด",
    "bandages": "ผ้าพันแผล",
    "bandage": "ผ้าพันแผล",
    "respiration": "การหายใจ",
    "respirations": "การหายใจ",
    "capillary": "หลอดเลือดฝอย",
    "nail beds": "ฐานเล็บ",
    "nail bed": "ฐานเล็บ",
    "blood vessels": "หลอดเลือด",
    "blood vessel": "หลอดเลือด",
    "windpipe": "หลอดลม",
    "external bleeding": "เลือดออกภายนอก",
    "internal bleeding": "เลือดออกภายใน",
    "digital ligation": "การใช้นิ้วกดหลอดเลือด",
    "digital pressure": "แรงกดจากนิ้วมือ",
    "traction splint": "เฝือกดามแบบดึงจัดแนว",
    "compound fracture": "กระดูกหักแบบเปิด",
    "open fracture": "กระดูกหักแบบเปิด",
    "closed fracture": "กระดูกหักแบบปิด",
    "fractures": "กระดูกหัก",
    "fracture": "กระดูกหัก",
    "dislocation": "ข้อเคลื่อน",
    "dislocations": "ข้อเคลื่อน",
    "sprain": "ข้อแพลง",
    "sprains": "ข้อแพลง",
    "ligament": "เอ็นยึดข้อ",
    "ligaments": "เอ็นยึดข้อ",
    "stakes": "หลักไม้",
    "a stake": "หลักไม้",
    "the stake": "หลักไม้",
    "a spit": "ไม้เสียบย่าง",
}
TERM_PATTERN = re.compile(r"\b(?:" + "|".join(re.escape(term) for term in sorted(GLOSSARY, key=len, reverse=True)) + r")\b", re.IGNORECASE)
EDITED_SEGMENTS = dict(line.split("\t", 1) for line in (ROOT / "scripts/manual_thai_edits.tsv").read_text(encoding="utf-8").splitlines() if "\t" in line)
EDITED_PARAGRAPHS = dict(line.split("\t", 1) for line in (ROOT / "scripts/manual_thai_paragraphs.tsv").read_text(encoding="utf-8").splitlines() if "\t" in line)
CHAPTERS = {
    1: "บทนำ", 2: "จิตวิทยาการเอาชีวิตรอด", 3: "การวางแผนและชุดอุปกรณ์เอาชีวิตรอด",
    4: "การแพทย์พื้นฐานเพื่อการเอาชีวิตรอด", 5: "ที่พักพิง", 6: "การหาน้ำ",
    7: "ทักษะการก่อไฟ", 8: "การหาอาหาร", 9: "การใช้ประโยชน์จากพืชเพื่อเอาชีวิตรอด",
    10: "พืชมีพิษ", 11: "สัตว์อันตราย", 12: "การทำอาวุธ เครื่องมือ และอุปกรณ์จากวัสดุในพื้นที่",
    13: "การเอาชีวิตรอดในทะเลทราย", 14: "การเอาชีวิตรอดในเขตร้อน",
    15: "การเอาชีวิตรอดในสภาพอากาศหนาว", 16: "การเอาชีวิตรอดในทะเล",
    17: "การข้ามสิ่งกีดขวางทางน้ำด้วยวัสดุที่มีอยู่", 18: "การหาทิศทางในภาคสนาม",
    19: "เทคนิคการส่งสัญญาณ", 20: "การเคลื่อนที่เพื่อเอาชีวิตรอดในพื้นที่ฝ่ายข้าศึก",
    21: "การพราง", 22: "การติดต่อกับผู้คน", 23: "การเอาชีวิตรอดจากอันตรายที่มนุษย์ก่อขึ้น",
}


def edit_segment(source: str, thai: str, part: int) -> str:
    if source.strip() in EDITED_SEGMENTS:
        return EDITED_SEGMENTS[source.strip()]
    for prefix, replacement in EDITED_PARAGRAPHS.items():
        if source.strip().startswith(prefix):
            return replacement
    chapter = re.fullmatch(r"CHAPTER (\d+) - [^\n]+", source.strip())
    if chapter and len(source.strip()) < 120 and source.count("CHAPTER") == 1 and int(chapter[1]) in CHAPTERS:
        return f"บทที่ {chapter[1]} - {CHAPTERS[int(chapter[1])]}"
    replacements = {
        "FM 21-76 US ARMY SURVIVAL MANUAL": "FM 21-76 คู่มือเอาชีวิตรอดกองทัพบกสหรัฐฯ",
        "การอยู่รอด": "การเอาชีวิตรอด",
        "กองทัพสหรัฐฯ": "กองทัพบกสหรัฐฯ",
        "กระทรวงกองทัพบกสหรัฐฯ": "กองทัพบกสหรัฐฯ",
        "กระทรวงกองทัพสหรัฐ": "กองทัพบกสหรัฐฯ",
        "ก่อเหตุเพลิงไหม้": "ก่อไฟ",
        "ความต้องการการยิง": "ความจำเป็นในการก่อไฟ",
        "ปล่อยให้น้ำยืน": "ตั้งน้ำทิ้งไว้",
        "โรงเรียนเหล่านี้": "ฝูงปลาเหล่านี้",
        "ดวงตาที่มืดมนจมลง": "ดวงตาหมองและลึกโหล",
        "การเติมเส้นเลือดฝอยล่าช้าในเตียงเล็บมือ": "สีบริเวณฐานเล็บกลับคืนช้าหลังจากกด",
        "เส้นเลือดฝอย": "หลอดเลือดฝอย",
        "เพิ่มขนาดสถานการณ์": "ประเมินสถานการณ์",
        "เพิ่มขนาดสภาพแวดล้อมของคุณ": "ประเมินสภาพแวดล้อมรอบตัว",
        "เพิ่มขนาดสภาพร่างกายของคุณ": "ประเมินสภาพร่างกายของตนเอง",
        "เพิ่มขนาดอุปกรณ์ของคุณ": "ตรวจสอบอุปกรณ์ที่มีอยู่",
        "กันน้ำหรือกันน้ำ": "กันน้ำได้หรือทนต่อน้ำ",
        "อุปกรณ์สตาร์ทไฟ": "อุปกรณ์จุดไฟ",
        "รายการจัดซื้ออาหาร": "อุปกรณ์หาอาหาร",
        "เหมาะที่จะรับส่วนประกอบที่หลากหลาย": "บรรจุอุปกรณ์ได้หลากหลายชนิด",
        "การแสดงด้นสด": "การประยุกต์ใช้สิ่งที่มีอยู่",
        "แสดงด้นสด": "ประยุกต์ใช้สิ่งที่มีอยู่",
        "เรียนรู้ที่จะโพล่งออกมา": "ฝึกประยุกต์ใช้สิ่งที่มีอยู่",
        "เพื่อความอยู่รอด": "เพื่อเอาชีวิตรอด",
        "ไฟแช็ก ไม้ขีดโลหะ": "ไฟแช็ก แท่งจุดไฟ",
        "เย็บผีเสื้อ": "แถบปิดแผลแบบผีเสื้อ",
        "กระจกสัญญาณ": "กระจกส่งสัญญาณ",
        "สายสแนร์": "ลวดทำบ่วงดักสัตว์",
        "แชปสติ๊ก": "ลิปมัน",
        "ต้นไม้ร่ม": "ต้นอัมเบรลลา",
        "โมเด็ม": "สมัยใหม่",
        "อุปกรณ์รับน้ำหนัก": "สายรัดบรรทุกสัมภาระ",
        "ปฏิบัติต่อตนเอง": "รักษาตนเอง",
        "การฉีดวัคซีนของคุณอยู่ในปัจจุบัน": "ได้รับวัคซีนครบตามกำหนด",
        "การไม่รักษาช็อตให้ทันเวลา": "การไม่ได้รับวัคซีนให้ครบตามกำหนด",
        "ปลาและสาย บ่วงดักสัตว์": "สายเบ็ดและสายทำบ่วงดักสัตว์",
        "กระดานดับเพลิง": "แผ่นไม้ฐานก่อไฟ",
        "การวางเพลิง": "การจัดกองไฟ",
        "ไม้เนื้ออ่อนปรุงรส": "ไม้เนื้ออ่อนที่แห้งสนิท",
        "เดินทางในโรงเรียน": "เคลื่อนที่เป็นฝูง",
        "แพแปรง": "แพกิ่งไม้",
        "แปรงสีเขียวสด": "กิ่งไม้สด",
        "หลังคาปาก": "เพดานปาก",
        "บทความทั้งหมดของคุณ": "สิ่งของทั้งหมดของคุณ",
        "อุปกรณ์วางเท้า": "รองเท้า",
        "แรงดึงดูดของน้ำ": "แรงต้านของน้ำ",
        "โรงงานริมถนน": "พืชริมถนน",
        "ระบุตัวตนเชิงบวก": "ระบุชนิดให้แน่ชัด",
        "ระบุตัวตนในเชิงบวก": "ระบุให้แน่ชัด",
        "ระบุได้ในเชิงบวก": "ระบุชนิดได้แน่ชัด",
        "การจำแนกพืชที่เกี่ยวข้องในเชิงบวก": "การจำแนกชนิดพืชที่ใช้ให้แน่ชัด",
    }
    for before, after in replacements.items():
        thai = thai.replace(before, after)
    if 7 <= part <= 13:
        thai = thai.replace("เหยื่อ", "ผู้บาดเจ็บ")
    # Remove artificial spaces introduced around protected Thai terms.
    thai = re.sub(r"([\u0e00-\u0e7f])  +", r"\1 ", thai)
    return thai


def protect_terms(text: str) -> tuple[str, dict[str, str]]:
    values = {}
    def replace(match):
        token = f"ZXTM{len(values):04d}MTXZ"
        values[token] = GLOSSARY[match[0].lower()]
        return token
    return TERM_PATTERN.sub(replace, text), values


def segments(text: str) -> list[str]:
    """Keep paragraph boundaries; only subdivide unusually long source paragraphs."""
    out = []
    for paragraph in re.split(r"\n\s*\n", text.replace("\r\n", "\n").strip()):
        while len(paragraph) > 2400:
            candidates = [m.end() for m in re.finditer(r"[.!?](?:\s+|$)", paragraph[:2400])]
            at = candidates[-1] if candidates else paragraph.rfind(" ", 0, 2400)
            if at < 1:
                at = 2400
            out.append(paragraph[:at].strip())
            paragraph = paragraph[at:].strip()
        if paragraph:
            out.append(paragraph)
    return out


def translate_block(block: str) -> str:
    original_block = block
    single = re.fullmatch(r"(ZXQ\d{5}QXZ)\n(.*)", block.strip(), re.DOTALL)
    if single and not re.search(r"ZXQ\d{5}QXZ", single[2]):
        source = single[2].strip()
        if source in EDITED_SEGMENTS:
            return single[1] + "\n" + EDITED_SEGMENTS[source]
        for prefix, target in EDITED_PARAGRAPHS.items():
            if source.startswith(prefix):
                return single[1] + "\n" + target
    block, terms = protect_terms(block)
    key = hashlib.sha256(block.encode()).hexdigest()
    cached = CACHE / f"{key}.json"
    if cached.exists():
        result = json.loads(cached.read_text(encoding="utf-8"))["translation"]
        for token, thai in terms.items():
            if result.count(token) != 1:
                raise ValueError(f"Missing or duplicated glossary term: {token}")
            result = result.replace(token, thai)
        return result
    query = urllib.parse.urlencode({"client": "gtx", "sl": "en", "tl": "th", "dt": "t", "q": block})
    url = "https://translate.googleapis.com/translate_a/single?" + query
    last_error = None
    for retry in range(9):
        try:
            request = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
            with urllib.request.urlopen(request, timeout=90) as response:
                data = json.load(response)
            result = "".join(item[0] for item in data[0] if item and item[0])
            if not result.strip():
                raise ValueError("Empty translation")
            for token in terms:
                if result.count(token) != 1:
                    raise ValueError(f"Missing or duplicated glossary term: {token}")
            cached.write_text(json.dumps({"source": block, "translation": result}, ensure_ascii=False), encoding="utf-8")
            for token, thai in terms.items():
                result = result.replace(token, thai)
            return result
        except Exception as error:
            last_error = error
            # Some translation responses drop a protected token in a large batch.
            # Retry its individual paragraphs rather than accepting missing text.
            if isinstance(error, ValueError) and "glossary term" in str(error):
                paragraphs = [part for part in re.split(r"(?=ZXQ\d{5}QXZ)", original_block) if part.strip()]
                if len(paragraphs) > 1:
                    return "\n\n".join(translate_block(part.strip()) for part in paragraphs)
            time.sleep(min(40, 2 ** retry))
    raise RuntimeError(f"Translation failed: {last_error}")


def translate_file(path: Path) -> dict:
    backup = ORIGINAL / path.name
    if not backup.exists():
        backup.write_bytes(path.read_bytes())
    source = backup.read_text(encoding="utf-8-sig")
    pieces = segments(source)
    batches = []
    batch = []
    size = 0
    for i, paragraph in enumerate(pieces):
        item = f"ZXQ{i:05d}QXZ\n{paragraph}"
        if batch and size + len(item) + 2 > MAX_CHARS:
            batches.append(batch)
            batch, size = [], 0
        batch.append((i, item))
        size += len(item) + 2
    if batch:
        batches.append(batch)
    translated = {}
    for batch in batches:
        output = translate_block("\n\n".join(item for _, item in batch))
        found = list(re.finditer(r"ZXQ\s*(\d{5})\s*QXZ", output))
        actual_ids = [int(match[1]) for match in found]
        expected_ids = [i for i, _ in batch]
        if actual_ids != expected_ids:
            raise ValueError(f"Marker mismatch {path.name}: {actual_ids} != {expected_ids}")
        for j, match in enumerate(found):
            end = found[j + 1].start() if j + 1 < len(found) else len(output)
            translated[int(match[1])] = output[match.end():end].strip()
        time.sleep(0.15)
    if len(translated) != len(pieces):
        raise ValueError(f"Missing paragraphs in {path.name}")
    result = "\n\n".join(edit_segment(pieces[i], translated[i], int(path.stem)) for i in range(len(pieces))) + "\n"
    if len(re.findall(r"[\u0e00-\u0e7f]", result)) < 10:
        raise ValueError(f"Thai text missing: {path.name}")
    path.write_text(result, encoding="utf-8", newline="\n")
    before_nums = re.findall(r"\d+(?:[.,]\d+)*", source)
    after_nums = re.findall(r"\d+(?:[.,]\d+)*", result)
    record = {"file": path.name, "segments": len(pieces), "batches": len(batches), "source_bytes": len(source.encode()), "thai_bytes": len(result.encode()), "numeric_tokens_identical": before_nums == after_nums}
    print(json.dumps(record, ensure_ascii=True), flush=True)
    return record


def main():
    ORIGINAL.mkdir(parents=True, exist_ok=True)
    CACHE.mkdir(parents=True, exist_ok=True)
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as executor:
        records = list(executor.map(translate_file, sorted(SOURCE.glob("[0-9][0-9][0-9][0-9].txt"))))
    (WORK / "manual-translation-status.json").write_text(json.dumps(records, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"Completed {len(records)} manual parts; {sum(x['thai_bytes'] for x in records)} UTF-8 bytes.", flush=True)


if __name__ == "__main__":
    main()
