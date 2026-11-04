"""Final, idempotent Thai editorial fixes and readable original-page contents.

Run before build_thai_sd.py; all original numeric references remain intact.
"""
from pathlib import Path
import re
ROOT=Path(__file__).resolve().parents[1]
EDITS={
    'การเติม หลอดเลือดฝอย ล่าช้าในเตียงเล็บมือ':'สีบริเวณฐานเล็บกลับคืนช้าหลังจากกด',
    'อาหาร การจัดซื้อจัดจ้าง':'การหาอาหาร',
    'น้ำ การจัดหา':'การหาแหล่งน้ำ',
    'การดำเนินการเอาชีวิตรอด':'แนวปฏิบัติเพื่อเอาชีวิตรอด',
    'รูปแบบเพื่อเอาชีวิตรอด':'แนวทางการเอาชีวิตรอด',
    'มองความเครียด':'ทำความเข้าใจความเครียด',
    'การเตรียมตัวของตัวเอง':'การเตรียมความพร้อมของตนเอง',
    'การบาดเจ็บต่อสิ่งแวดล้อม':'การบาดเจ็บจากสภาพแวดล้อม',
    'ความสามารถในการกินของพืช':'การพิจารณาว่าพืชชนิดใดกินได้',
    'ทั้งหมดเกี่ยวกับพืช':'ข้อควรรู้เกี่ยวกับพืช',
    'ข้อกำหนดสำหรับการบำรุงรักษาสุขภาพ':'สิ่งจำเป็นต่อการรักษาสุขภาพ',
}
if __name__=='__main__':
    changed=[]
    for path in sorted((ROOT/'sdcard/library/fm21-76').glob('*.txt')):
        text=path.read_text('utf-8').replace('\u200b','');original=text
        for before,after in EDITS.items():text=text.replace(before,after)
        if int(path.stem)<=3:
            # The English PDF extraction split several leader runs across
            # paragraphs. Rejoin only dots, then keep each title/page together.
            text=re.sub(r'(\.{2,})\s*\n\s*\n(?=\.{2,})',r'\1',text)
            text=re.sub(r'\.{12,}\s*(\d+)',r'  (หน้าต้นฉบับ \1)\n\n',text)
        if text!=original:
            path.write_text(text,'utf-8',newline='\n');changed.append(path.name)
    print('Edited:',','.join(changed))
