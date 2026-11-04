// Triage assistant: a few menu questions that lead to the right guide. Built in, so it works
// with any SD card. Targets starting with "g:" are guide ids from /wiki/catalog.tsv; anything
// else is the id of another node. Keep labels short (they have to fit a button on the 2.8").
#pragma once

struct TriOpt  { const char *label; const char *to; };
struct TriNode { const char *id; const char *q; uint8_t n; TriOpt o[5]; };

static const TriNode TRIAGE[] = {
  {"root", "เกิดอะไรขึ้น? หากทำได้ ให้โทรขอความช่วยเหลือฉุกเฉินก่อน", 5, {
    {"มีผู้บาดเจ็บหรือป่วย", "person"},
    {"พายุ แผ่นดินไหว ภัยพิบัติ", "disaster"},
    {"เหตุในบ้านหรือในรถ", "home"},
    {"หลงทาง หนาว ขาดเสบียง", "outdoors"},
    {"เด็ก ทารก หรือผู้สูงอายุ", "family"}}},

  {"person", "รู้สึกตัวและหายใจเป็นปกติหรือไม่?", 5, {
    {"ไม่ตอบสนอง ไม่หายใจ", "g:cpr"},
    {"สำลัก พูดไม่ได้", "g:choking"},
    {"เลือดออกมาก", "g:severe-bleeding"},
    {"รู้สึกตัว แต่บาดเจ็บหรือป่วย", "hurt"},
    {"พิษ สัตว์กัด หรือแพ้", "poison"}}},

  {"hurt", "มีปัญหาแบบใด?", 5, {
    {"ตก ชน หรือกระดูกหัก", "fall"},
    {"เจ็บอก อัมพาต ชัก", "heart"},
    {"แผลไหม้ หรือไฟฟ้าช็อต", "burn"},
    {"ร้อนจัด หรือหนาวจัด", "temp"},
    {"อาเจียน หรือท้องเสีย", "g:diarrhea-vomiting"}}},

  {"fall", "หลังตกจากที่สูง ชน หรือถูกกระแทก:", 5, {
    {"ปวดคอ ปวดหลัง หรือชา", "g:spinal-injury"},
    {"บาดเจ็บที่ศีรษะ", "g:head-injury"},
    {"กระดูกหัก หรือข้อแพลง", "g:fractures-sprains"},
    {"บาดแผล หรือถูกบาด", "g:wound-care"},
    {"ช่วยขึ้นจากน้ำ", "g:drowning"}}},

  {"heart", "อาการใดใกล้เคียงที่สุด?", 5, {
    {"เจ็บอก หรืออาการอัมพาต", "g:stroke-heart-attack"},
    {"ชัก", "g:seizures"},
    {"เบาหวาน ตัวสั่น สับสน", "g:diabetic-emergency"},
    {"ซีด เหงื่อเย็น หน้ามืด", "g:shock"},
    {"บาดเจ็บที่ตา", "g:eye-injuries"}}},

  {"burn", "เกิดเหตุใดขึ้น?", 4, {
    {"แผลไหม้", "g:burns"},
    {"ไฟฟ้าช็อต", "g:electric-shock"},
    {"ฟ้าผ่า หรือสายไฟ", "g:downed-power-lines"},
    {"ไฟไหม้บ้าน", "g:house-fire"}}},

  {"temp", "ร้อนเกินไปหรือหนาวเกินไป?", 4, {
    {"หนาวสั่น เนื้อเยื่อเย็นจัด", "g:hypothermia"},
    {"ตัวร้อน สับสน ไม่มีเหงื่อ", "g:heat-illness"},
    {"ตกทะลุน้ำแข็ง", "g:falling-through-ice"},
    {"กระหายน้ำ หรือขาดน้ำ", "g:dehydration"}}},

  {"poison", "ได้รับสิ่งใด?", 5, {
    {"กินสารพิษ หรือยาเกินขนาด", "g:poisoning"},
    {"แพ้รุนแรง", "g:anaphylaxis"},
    {"งูกัด", "g:snakebite"},
    {"เห็บ แมลงต่อย แมงมุม", "g:bites-stings"},
    {"คาร์บอนมอนอกไซด์", "g:carbon-monoxide"}}},

  {"disaster", "ภัยชนิดใด?", 5, {
    {"พายุทอร์นาโด", "g:tornado"},
    {"น้ำท่วม", "g:flood"},
    {"แผ่นดินไหว", "g:earthquake"},
    {"ไฟป่า", "g:wildfire"},
    {"เพิ่มเติม...", "disaster2"}}},

  {"disaster2", "ภัยชนิดใด?", 5, {
    {"พายุเฮอริเคน", "g:hurricane"},
    {"พายุหิมะ หรือน้ำแข็ง", "g:blizzard"},
    {"คลื่นความร้อน", "g:extreme-heat"},
    {"เหตุฉุกเฉินนิวเคลียร์", "g:nuclear-get-inside"},
    {"ไฟฟ้าดับ", "g:power-outage"}}},

  {"home", "เกิดปัญหาที่ใด?", 4, {
    {"แก๊ส ไฟไหม้ หรือท่อน้ำ", "utility"},
    {"รถเสีย หรือรถติดหล่ม", "car"},
    {"สายไฟ หรือเครื่องปั่นไฟ", "power"},
    {"สัญญาณเตือนก๊าซ CO", "g:carbon-monoxide"}}},

  {"utility", "ปัญหาใด?", 5, {
    {"ได้กลิ่นแก๊ส", "g:gas-leak"},
    {"ไฟไหม้บ้าน", "g:house-fire"},
    {"ท่อแตก หรือท่อแข็งตัว", "g:burst-pipe"},
    {"ปิดแก๊ส ไฟฟ้า หรือน้ำ", "g:utility-shutoffs"},
    {"อาหารเริ่มเสีย", "g:food-after-outage"}}},

  {"power", "ปัญหาใด?", 3, {
    {"สายไฟขาดตกพื้น", "g:downed-power-lines"},
    {"ใช้เครื่องปั่นไฟ", "g:generator-safety"},
    {"ไฟฟ้าดับ", "g:power-outage"}}},

  {"car", "รถมีปัญหาอย่างไร?", 5, {
    {"รถเสีย", "g:car-breakdown"},
    {"ติดค้างในอากาศหนาว", "g:stranded-winter"},
    {"รถตกน้ำ", "g:car-in-water"},
    {"รถไฟไหม้", "g:car-fire"},
    {"ยางแบน", "g:flat-tire"}}},

  {"outdoors", "ต้องการสิ่งใด?", 5, {
    {"หลงทาง", "g:staying-found"},
    {"น้ำ", "g:finding-water"},
    {"ที่พักให้อบอุ่นและแห้ง", "g:shelter-basics"},
    {"ก่อไฟ", "g:fire-basics"},
    {"ส่งสัญญาณขอความช่วยเหลือ", "g:sos-signals"}}},

  {"family", "ใครต้องการความช่วยเหลือ?", 5, {
    {"เด็กหลงทางหรือหายตัว", "g:lost-child"},
    {"เด็กมีไข้", "g:child-fever"},
    {"ดูแลทารกเมื่อไฟดับ", "g:baby-care"},
    {"เด็กบาดเจ็บหรือป่วย", "g:children-first-aid"},
    {"ผู้สูงอายุ", "g:elders-special-needs"}}},
};
static const int N_TRIAGE = sizeof(TRIAGE) / sizeof(TRIAGE[0]);
