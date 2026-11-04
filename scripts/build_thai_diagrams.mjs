/** Author Thai diagrams as native SVG and export JPEGs for TJpg_Decoder.
 * Width 448 renders at 224 px using the CYD's 1/2 JPEG scale.
 * Requires playwright-core and a local Chrome/Edge browser. No image model.
 */
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath, pathToFileURL} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
let pw;
try { pw=await import('playwright-core'); }
catch { pw=await import(pathToFileURL(path.join(root,'.thai-work/web-qa/node_modules/playwright-core/index.mjs')).href); }
const {chromium}=pw;
const folder=path.join(root,'sdcard/wiki/img');
const backup=path.join(root,'.thai-work/original/sdcard/wiki/img');
const qa=path.join(root,'.thai-work/diagrams');
fs.mkdirSync(backup,{recursive:true});fs.mkdirSync(qa,{recursive:true});
const FONT=fs.readFileSync(path.join(root,'sdcard/web/Sarabun-Regular.ttf')).toString('base64');
const P={paper:'#f6f2e8',ink:'#202b2a',muted:'#58605b',orange:'#d75d09',red:'#b82223',blue:'#236ab9',green:'#517845',wood:'#77532e',soil:'#ae8b61',night:'#111b35',star:'#fff2b0'};
const esc=s=>String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&apos;'}[c]));
function text(s,x,y,size=24,anchor='middle',fill=P.ink,width=416,weight=400){return `<text data-fit="${width}" x="${x}" y="${y}" font-size="${size}" text-anchor="${anchor}" fill="${fill}" font-weight="${weight}">${esc(s)}</text>`}
function lines(ss,x,y,size=24,step=34,anchor='middle',fill=P.ink,width=416){return ss.map((s,i)=>text(s,x,y+i*step,size,anchor,fill,width)).join('')}
function line(x1,y1,x2,y2,color=P.ink,w=3,dash=''){return `<line x1="${x1}" y1="${y1}" x2="${x2}" y2="${y2}" stroke="${color}" stroke-width="${w}"${dash?` stroke-dasharray="${dash}"`:''}/>`}
function arrow(x1,y1,x2,y2,color=P.ink,w=3){const a=Math.atan2(y2-y1,x2-x1),l=11;return line(x1,y1,x2,y2,color,w)+`<path d="M${x2-l*Math.cos(a-.45)},${y2-l*Math.sin(a-.45)} L${x2},${y2} L${x2-l*Math.cos(a+.45)},${y2-l*Math.sin(a+.45)}" fill="none" stroke="${color}" stroke-width="${w}"/>`}
function rect(x,y,w,h,fill,stroke='none',r=0,sw=2){return `<rect x="${x}" y="${y}" width="${w}" height="${h}" rx="${r}" fill="${fill}" stroke="${stroke}" stroke-width="${sw}"/>`}
function circle(x,y,r,fill,stroke='none',sw=2){return `<circle cx="${x}" cy="${y}" r="${r}" fill="${fill}" stroke="${stroke}" stroke-width="${sw}"/>`}
function poly(points,fill,stroke='none',sw=2){return `<polygon points="${points}" fill="${fill}" stroke="${stroke}" stroke-width="${sw}"/>`}
function pathD(d,fill,stroke='none',sw=2){return `<path d="${d}" fill="${fill}" stroke="${stroke}" stroke-width="${sw}"/>`}
function flame(x,y,k=1){return `<g transform="translate(${x},${y}) scale(${k})">${pathD('M0 0 C-50 -20 -30 -70 0 -110 C10 -65 55 -35 0 0',P.orange)}${pathD('M0 -5 C-23 -20 -12 -42 0 -60 C13 -32 18 -19 0 -5','#ffcf43')}</g>`}
function badge(x,y,w,h,fill,label){return rect(x,y,w,h,fill,'none',10)+text(label,x+w/2,y+h/2+9,28,'middle','#fff',w-12,700)}
function tree(x,y){return rect(x-7,y+20,14,210,P.wood)+circle(x,y,35,'#8caf76')}
function star(x,y,r=5){return circle(x,y,r,P.star)}
function frame(title,height,body,dark=false){const bg=dark?P.night:P.paper;return `<svg xmlns="http://www.w3.org/2000/svg" width="448" height="${height}" viewBox="0 0 448 ${height}" lang="th"><title>${esc(title)}</title><defs><style>@font-face{font-family:Sarabun;src:url(data:font/ttf;base64,${FONT}) format('truetype')}text{font-family:Sarabun,Tahoma,sans-serif;dominant-baseline:alphabetic}</style><linearGradient id="sunset" x2="0" y2="1"><stop stop-color="#ffe1a8"/><stop offset="1" stop-color="#f28b63"/></linearGradient></defs>${rect(0,0,448,height,bg)}${text(title,224,44,30,'middle',dark?P.star:P.ink,420,700)}${body}</svg>`}
const diagrams={};

diagrams.cpr=frame('กดหน้าอกช่วยชีวิต',570,
  circle(210,125,36,'#e5bd9c',P.ink)+rect(135,165,150,200,'#e5bd9c',P.ink,28)+line(210,180,210,315,'#c08e68',2)+circle(210,235,22,P.red)+
  lines(['ใช้ส้นมือ','วางกลางหน้าอก'],332,111,24,32,'middle',P.ink,195)+arrow(342,150,237,223)+
  lines(['กดแรง','และเร็ว','ปล่อยหน้าอก','คืนตัวเต็มที่'],66,245,24,34,'middle',P.red,126)+
  rect(16,395,200,95,'#e5edf6',P.blue,10)+text('100-120',116,429,34,'middle',P.blue,180,700)+text('ครั้งต่อนาที',116,480,26,'middle',P.ink,180)+
  rect(232,395,200,95,'#f8e7dd',P.orange,10)+text('ลึก 5-6 ซม.',332,454,29,'middle',P.orange,180,700)+
  text('ภาพนี้แสดง CPR แบบกดหน้าอกอย่างเดียว',224,537,23,'middle',P.muted,416));

diagrams.debris_hut=frame('กระท่อมใบไม้: ภาพตัดขวาง',550,
  poly('45,392 224,122 403,392','#83683d')+poly('139,392 224,251 309,392',P.paper)+rect(0,392,448,92,P.soil)+
  circle(224,250,8,P.wood)+rect(140,365,168,27,'#83a758')+
  lines(['ไม้สันหลังคา'],97,165,24,32,'middle',P.ink,180)+arrow(100,178,215,244,P.muted,2)+
  lines(['ใบไม้และเศษพืช','หนา 60-90 ซม.'],330,92,24,34,'middle',P.orange,210)+arrow(340,145,335,295,P.muted,2)+
  text('ชั้นใบไม้รองนอนหนา',224,445,26,'middle',P.ink,410)+
  lines(['ขนาดพอให้นอนได้เท่านั้น','พื้นที่เล็กช่วยเก็บความอบอุ่น'],224,494,24,40,'middle',P.ink,416));

{
 let tee='';for(let i=0;i<8;i++)tee+=line(105-58+i*16,305,105+(i-3.5)*2,150,P.wood,6);
 let logs='';for(let r=0;r<5;r++){const y=180+r*25;logs+=line(247,y,405,y,P.wood,10)+rect(253,y-8,16,16,'#966e40')+rect(382,y-8,16,16,'#966e40')}
 diagrams.fire_lays=frame('กองฟืนสองแบบที่ใช้ได้ดี',520,
 text('วางเชื้อจุดไฟไว้ตรงกลาง',224,93,25,'middle',P.orange)+circle(105,296,24,'#dbbf79')+circle(325,288,24,'#dbbf79')+tee+logs+
 lines(['ทรงกระโจม','ร้อนเร็วและติดง่าย'],105,365,25,39,'middle',P.ink,200)+lines(['แบบกระท่อม','เผาไหม้นาน','และสม่ำเสมอ'],325,365,25,39,'middle',P.ink,200));
}

{
 let tinder='';for(let i=0;i<25;i++){const x=45+(i*23)%85,y=160+(i*19)%85;tinder+=`<ellipse cx="${x}" cy="${y}" rx="12" ry="5" fill="none" stroke="#ab8a3b" stroke-width="2"/>`}
 let kind='';for(let i=0;i<6;i++)kind+=line(178+i*9,250,207+i*9,160,P.wood,4);
 let logs='';for(let i=0;i<3;i++)logs+=line(318+i*22,245,349+i*22,160,P.wood,18);
 diagrams.fire_materials=frame('เชื้อจุดไฟ ไม้ต่อไฟ และฟืน',540,tinder+kind+logs+
 lines(['เชื้อจุดไฟ','หญ้าแห้ง','เปลือกไม้','ขนปุย สำลี'],81,300,24,36,'middle',P.ink,130)+
 lines(['ไม้ต่อไฟ','ก้านไม้ขีด','ถึงแท่งดินสอ'],224,300,24,36,'middle',P.ink,128)+
 lines(['ฟืน','หัวแม่มือ','ถึงข้อมือ'],367,300,24,36,'middle',P.ink,128)+
 rect(16,440,416,88,'#fae6cf','none',8)+lines(['เตรียมวัสดุให้มากเป็นสองเท่า','ของปริมาณที่คิดว่าต้องใช้'],224,470,25,40,'middle',P.orange,390));
}

diagrams.fire_triangle=frame('สามเหลี่ยมไฟ',510,
 poly('70,365 224,125 378,365','none',P.orange,7)+flame(224,350,.85)+
 text('ออกซิเจน',85,245,26,'middle',P.blue,150,700)+text('ความร้อน',365,245,26,'middle',P.red,150,700)+
 text('เชื้อเพลิง',224,411,28,'middle',P.wood,400,700)+text('ขาดด้านใดด้านหนึ่ง ไฟก็ดับ',224,473,26,'middle',P.muted,416));

{
 let rows='';for(const [i,s,l] of [[0,'V','ต้องการความช่วยเหลือ'],[1,'X','ต้องการแพทย์'],[2,'N','ไม่ใช่ / ไม่'],[3,'Y','ใช่'],[4,'↑','ไปทางนี้']])rows+=badge(26,89+i*78,66,64,P.green,s)+text(l,114,130+i*78,26,'start',P.ink,310);
 diagrams.ground_signals=frame('สัญญาณภาคพื้นถึงอากาศยาน',605,rows+lines(['ทำแต่ละสัญลักษณ์ให้ใหญ่ 3 ม. (10 ฟุต) ขึ้นไป','ใช้สีที่ตัดกับพื้นให้เห็นได้ชัด'],224,535,24,40,'middle',P.muted,416));
}

{
 let fingers='';for(let i=0;i<4;i++)fingers+=rect(125,155+i*33,166,31,'#dfb58c','#9c7755',14)+text((60-i*15)+' นาที',105,178+i*33,23,'end',P.ink,95);
 diagrams.hand_sunset=frame('ใช้มือกะเวลาที่เหลือก่อนมืด',570,
 rect(0,73,448,245,'url(#sunset)')+circle(192,122,24,'#ffcf27')+rect(0,318,448,70,'#4b6043')+
 fingers+rect(273,150,50,186,'#dfb58c','#9c7755',19)+line(0,318,448,318,'#788267',3)+
 lines(['เหยียดแขนตรงไปข้างหน้า','ให้นิ้วล่างสุดแตะเส้นขอบฟ้า'],224,414,25,35,'middle',P.ink,416)+
 text('1 นิ้ว ≈ 15 นาที',224,506,29,'middle',P.orange,416,700)+text('4 นิ้ว ≈ 1 ชั่วโมง',224,548,29,'middle',P.orange,416,700));
}

{
 let rows='';const data=[['1','นาที',P.blue,['ตั้งสติ คุมการหายใจ','อาการช็อกจากความเย็น','จะผ่านไป']],['10','นาที',P.orange,['ช่วงที่ยังขยับแขนขาได้','รีบใช้เวลานี้ขึ้นจากน้ำ']],['1','ชั่วโมง',P.red,['ก่อนหมดสติจากความหนาว','พาดแขนไว้บนน้ำแข็ง']]];
 data.forEach((d,i)=>{let y=97+i*146;rows+=rect(16,y,110,120,d[2],'none',12)+text(d[0],71,y+56,44,'middle','#fff',100,700)+text(d[1],71,y+100,24,'middle','#fff',100)+lines(d[3],143,y+48,24,36,'start',P.ink,288)});
 diagrams.ice_1101=frame('หลัก 1-10-1 เมื่อตกน้ำเย็น',580,rows+text('เวลาเป็นค่าประมาณ ไม่ใช่เวลาที่รับประกัน',224,561,22,'middle',P.muted,416));
}

{
 let roof='';for(let i=0;i<12;i++)roof+=line(96+i*12,167,205+i*12,381,P.wood,3);
 let reflector='';for(let i=0;i<5;i++)reflector+=rect(367,310+i*13,45,9,P.wood);
 diagrams.lean_to=frame('เพิงลาดกับแนวสะท้อนความร้อน',570,
 tree(88,122)+tree(251,122)+line(77,167,271,167,P.wood,10)+poly('87,169 252,169 366,383 197,383','#85a458',P.green,3)+roof+
 rect(0,383,448,75,P.soil)+rect(114,384,187,20,'#a0b96d')+flame(345,381,.35)+reflector+
 arrow(428,204,326,204,P.blue,3)+lines(['หันด้านหลัง','รับลม'],374,140,24,34,'middle',P.blue,140)+
 lines(['แนวสะท้อน','ความร้อน'],350,269,23,32,'middle',P.ink,170)+arrow(367,277,397,316,P.muted,2)+
 text('ที่นอนมีฉนวนรองพื้น',224,440,26,'middle',P.ink,416)+text('กองไฟอยู่หน้าด้านเปิดของเพิง',224,513,25,'middle',P.ink,416));
}

{
 const pts=[[100,366],[144,358],[187,380],[228,395],[224,437],[285,429],[290,387]];let chart='';for(let i=0;i<pts.length-1;i++)chart+=line(...pts[i],...pts[i+1],'#8f9ab7',2);chart+=line(290,387,228,395,'#8f9ab7',2);chart+=pts.map(p=>star(...p)).join('');
 diagrams.north_star=frame('หาดาวเหนือ',585,
 chart+star(315,177,8)+line(285,429,315,177,'#ebc63c',2,'7 5')+
 text('ดาวเหนือ (Polaris)',312,117,25,'middle',P.star,250,700)+
 lines(['จากดาวชี้คู่ ต่อแนวออกไป','ประมาณ 5 เท่าของระยะห่าง','ระหว่างดาวคู่นั้น'],20,174,24,35,'start','#cad4ee',270)+
 text('ดาวชี้คู่',344,404,24,'middle','#ebc63c',170)+text('กลุ่มดาวหมีใหญ่',130,475,24,'middle','#cad4ee',245)+
 text('หันหน้าหาดาวเหนือ คือหันไปทิศเหนือ',224,552,25,'middle',P.star,416),true);
}

{
 let sugar='';for(let i=0;i<6;i++)sugar+=`<ellipse cx="${191+i%3*25}" cy="${152+Math.floor(i/3)*27}" rx="9" ry="5" fill="#d0ac43" stroke="#856c25"/>`;
 diagrams.ors_recipe=frame('น้ำเกลือแร่ชดเชยน้ำ',585,
 rect(16,96,128,240,'#eaf0f6',P.blue,12,3)+rect(160,96,128,240,'#fbf1d3','#b79529',12,3)+rect(304,96,128,240,'#eeece6','#8d8d86',12,3)+
 rect(58,133,43,71,'#a3c9ef',P.blue)+sugar+`<ellipse cx="348" cy="170" rx="20" ry="10" fill="#ddd" stroke="${P.ink}"/>`+line(368,170,411,164,P.ink,3)+
 lines(['น้ำสะอาด','ปลอดภัย','1 ลิตร'],80,242,25,36,'middle',P.ink,115)+
 lines(['น้ำตาล','6 ช้อนชา','ปาดเรียบ'],224,242,25,36,'middle',P.ink,115)+
 lines(['เกลือ','1/2 ช้อนชา','ปาดเรียบ'],368,242,25,36,'middle',P.ink,115)+
 text('+',152,185,28)+text('+',296,185,28)+
 lines(['คนให้ละลายหมด','จิบครั้งละน้อย แต่จิบบ่อย ๆ'],224,393,28,40,'middle',P.ink,416)+
 rect(16,468,416,92,'#f8e0dc','none',10)+lines(['ตวงอย่างระมัดระวัง','เกลือมากเกินไปเป็นอันตราย'],224,502,25,37,'middle',P.red,390));
}

{
 let body='';const rows=[['อากาศ','3 นาที',P.red],['ที่พัก','3 ชั่วโมง',P.orange],['น้ำ','3 วัน',P.blue],['อาหาร','3 สัปดาห์',P.green]];
 rows.forEach((r,i)=>{let y=94+i*104;body+=badge(16,y,130,66,r[2],r[0])+rect(160,y,272,66,P.paper,r[2],8,3)+text(r[1],296,y+44,30,'middle',P.ink,250,700);if(i===1)body+=text('เมื่อสภาพอากาศรุนแรง',295,y+92,22,'middle',P.muted,265)});
 diagrams.rule_of_threes=frame('กฎเลขสาม',600,body+lines(['เป็นขีดจำกัดโดยประมาณ','จัดการปัญหาตามลำดับนี้'],224,530,25,41,'middle',P.muted,416));
}

diagrams.shadow_stick=frame('หาทิศด้วยเงาไม้',570,
 rect(16,93,416,380,'#e2c99a')+line(224,360,224,211,P.wood,6)+line(224,211,108,360,'#77766e',3)+line(224,211,337,360,'#77766e',3)+
 arrow(46,360,407,360,P.ink,3)+arrow(224,452,224,128,P.blue,3)+circle(108,360,8,P.red)+circle(337,360,8,P.red)+
 text('เหนือ',224,119,26,'middle',P.blue,170,700)+text('ตะวันตก',75,316,24,'middle',P.ink,135)+text('ตะวันออก',368,333,24,'middle',P.ink,145)+
 text('จุดแรก',94,404,24,'middle',P.ink,150)+lines(['จุดที่สอง','หลัง 15 นาที'],334,413,24,34,'middle',P.ink,180)+
 lines(['แนวเหนือ-ใต้ตั้งฉาก','กับแนวระหว่างจุดเงาทั้งสอง'],224,505,25,40,'middle',P.blue,416));

diagrams.signal_mirror=frame('เล็งกระจกส่งสัญญาณ',590,
 lines(['1. ใช้นิ้วทำรูปตัว V คร่อมเป้าหมาย','2. เอียงกระจกให้จุดแสงตกในช่อง V'],18,91,24,36,'start',P.ink,412)+
 rect(32,340,68,68,'#a9cdef',P.ink,0,3)+line(110,381,288,220,'#c59b6e',24)+line(286,220,309,149,'#c59b6e',12)+line(286,220,355,191,'#c59b6e',12)+
 poly('357,163 389,143 422,164','#777d77')+line(67,361,387,155,'#f1b921',3)+
 text('เป้าหมาย',379,211,24,'middle',P.ink,130)+lines(['กระจกอยู่','ใต้ระดับตา'],82,451,24,34,'middle',P.ink,140)+
 text('กวาดแสงช้า ๆ ผ่านเป้าหมาย',224,549,27,'middle',P.orange,416));

{
 let drops='';for(let i=0;i<6;i++)drops+=circle(113+i*43,211+(i<3?i*23:(5-i)*23),4,P.blue);
 diagrams.solar_still=frame('กลั่นน้ำด้วยแสงอาทิตย์',585,
 rect(0,178,448,285,P.soil)+poly('67,178 381,178 328,414 121,414','#775839')+
 poly('67,178 224,313 381,178','#bbd2dc66')+line(67,178,224,313,'#71b3db',4)+line(224,313,381,178,'#71b3db',4)+
 line(102,364,120,276,P.green,6)+line(341,366,324,277,P.green,6)+rect(194,358,61,45,'#acd4ef',P.ink,0,3)+circle(224,313,10,'#a6a9ac',P.ink)+drops+
 circle(380,103,28,'#f8ce34')+lines(['ปิดขอบด้วยดิน'],94,132,24,32,'middle',P.ink,180)+arrow(91,141,69,178,P.muted,2)+
 text('พลาสติกใส',283,126,24,'middle',P.ink,190)+arrow(293,138,324,227,P.muted,2)+
 text('หินก้อนเล็ก',321,326,23,'middle',P.ink,170)+arrow(288,325,236,314,P.muted,2)+text('ภาชนะรองน้ำ',224,442,24,'middle',P.ink,350)+
 lines(['ได้น้ำน้อย มักต่ำกว่า 0.5 ลิตรต่อวัน','ควรใช้ร่วมกับแหล่งน้ำอื่น'],224,511,24,35,'middle',P.ink,416));
}

{
 let code='';for(const x of [32,57,82,346,371,396])code+=circle(x,200,9,P.orange);for(const x of [132,196,260])code+=rect(x-23,191,46,18,P.orange,'none',9);
 diagrams.sos_morse=frame('SOS ในรหัสมอร์ส',440,code+text('S',57,260,30,'middle',P.ink,100,700)+text('O',196,260,30,'middle',P.ink,130,700)+text('S',371,260,30,'middle',P.ink,100,700)+
 lines(['สั้น 3 ครั้ง - ยาว 3 ครั้ง - สั้น 3 ครั้ง','เว้นช่วง แล้วส่งซ้ำ'],224,325,25,38,'middle',P.ink,416)+text('ใช้ไฟฉาย นกหวีด กระจก หรือแตร',224,412,24,'middle',P.muted,416));
}

{
 let stars=star(275,250,6)+star(264,276,6)+star(301,270,6)+star(283,298,6)+star(104,365,6)+star(147,383,6);
 diagrams.southern_cross=frame('หาทิศด้วยกลุ่มดาวกางเขนใต้',690,
 line(275,250,283,298,'#9aa7ca',2)+line(264,276,301,270,'#9aa7ca',2)+line(283,298,319,514,'#ebc63c',2,'7 5')+stars+
 lines(['ต่อแกนยาวของกลุ่มดาว','ออกไปประมาณ 4.5 เท่า','ของความยาวแกนนั้น','(ภาพย่อระยะให้พอดีกรอบ)'],20,91,24,35,'start','#cad4ee',408)+
 text('ดาวชี้คู่',119,430,24,'middle','#cad4ee',210)+circle(319,514,6,'none','#ebc63c',3)+
 text('ขั้วฟ้าใต้',289,558,26,'middle',P.star,290)+lines(['ลากลงตรง ๆ ถึงเส้นขอบฟ้า','จุดนั้นคือทิศใต้'],224,620,25,40,'middle',P.star,416),true);
}

diagrams.tarp_shelter=frame('ที่พักผ้าใบทรงจั่ว',550,
 rect(34,132,16,295,P.wood)+rect(398,132,16,295,P.wood)+line(42,187,406,187,P.ink,3)+
 poly('102,187 347,187 392,416 57,416','#4883a4',P.ink,2)+line(224,187,224,416,'#27617b',3)+
 rect(0,427,448,70,P.soil)+line(57,416,42,438,P.ink,3)+line(392,416,409,438,P.ink,3)+
 lines(['เชือกสันหลังคา','ต้องขึงให้ตึง'],224,103,25,34,'middle',P.ink,330)+
 text('ยึดมุมผ้าให้ต่ำทางด้านรับลม',224,480,26,'middle',P.ink,416));

diagrams.tick_removal=frame('วิธีเอาเห็บออก',570,
 rect(0,322,448,220,'#e4bd9c')+line(0,322,448,322,'#ba8b69',3)+
 pathD('M170 103 L211 313 L221 310 L181 100','#a6a8a8',P.ink,2)+pathD('M291 103 L238 313 L228 309 L281 100','#a6a8a8',P.ink,2)+
 `<ellipse cx="224" cy="291" rx="17" ry="27" fill="#76502f" stroke="${P.ink}"/>`+circle(224,317,6,P.ink)+
 [277,290,303].map(y=>line(202,y,214,y+5,P.wood,2)+line(234,y+5,246,y,P.wood,2)).join('')+
 lines(['คีบให้ชิด','ผิวหนัง'],87,156,26,35,'middle',P.ink,150)+arrow(82,192,209,311,P.muted,2)+
 arrow(369,284,369,144,P.blue,5)+lines(['ดึงขึ้น','ตรง ๆ'],380,109,24,34,'middle',P.blue,120)+
 lines(['ค่อย ๆ ดึงด้วยแรงสม่ำเสมอ','อย่าบิดหรือกระชาก','ห้ามใช้ความร้อน น้ำยาทาเล็บ','หรือปิโตรเลียมเจลลี'],224,382,24,38,'middle',P.ink,416));

diagrams.tourniquet=frame('ตำแหน่งสายรัดห้ามเลือด',550,
 text('จดเวลาที่เริ่มรัดไว้',224,105,28,'middle',P.orange,416,700)+
 rect(29,179,370,73,'#e4bd9c',P.ink,35)+`<ellipse cx="397" cy="215" rx="29" ry="48" fill="#e4bd9c" stroke="${P.ink}" stroke-width="2"/>`+
 circle(132,216,12,'none','#9c8468',2)+rect(229,162,22,108,'#252525')+line(240,159,240,298,'#646464',7)+`<ellipse cx="326" cy="215" rx="16" ry="13" fill="${P.red}"/>`+
 text('ข้อศอก',126,292,23,'middle',P.muted,170)+text('บาดแผล',337,294,24,'middle',P.red,150)+
 lines(['รัดเหนือแผล 5-7 ซม.','ห้ามรัดทับข้อต่อ','ขันให้แน่นจนเลือดหยุด'],224,372,29,48,'middle',P.ink,416));

{
 let ticks='';for(let n=0;n<12;n++){let a=n*Math.PI/6;ticks+=line(224+115*Math.sin(a),259-115*Math.cos(a),224+126*Math.sin(a),259-126*Math.cos(a),P.ink,3)}
 diagrams.watch_method=frame('หาทิศใต้ด้วยนาฬิกาเข็ม',640,
 circle(224,259,130,'none',P.ink,5)+ticks+text('12',224,164,27,'middle',P.ink,100,700)+
 line(224,259,296,301,P.ink,8)+arrow(224,259,347,189,P.blue,4)+text('ใต้',366,176,28,'middle',P.blue,120,700)+circle(387,371,25,'#f6cd2d')+
 lines(['หันเข็มชั่วโมง','เข้าหาดวงอาทิตย์'],344,425,24,34,'middle',P.ink,195)+
 lines(['ทิศใต้คือแนวกึ่งกลางระหว่าง','เข็มชั่วโมงกับเลข 12','หากมีการปรับเวลาออมแสง','ให้ใช้เลข 1 แทนเลข 12'],224,511,24,34,'middle',P.ink,416));
}

{
 let layers=rect(85,159,145,34,'#bdb9a0')+rect(85,193,145,62,'#e4ce98')+rect(85,255,145,66,'#3e413a')+rect(85,321,145,62,'#e4ce98')+rect(85,383,145,53,'#a4a28e')+rect(85,436,145,22,'#bbb5a3');
 for(let i=0;i<19;i++)layers+=circle(95+(i*31)%125,392+(i*13)%35,4,'#7b7b68');
 const labels=[['ผ้า / หญ้า',180],['ทรายละเอียด',232],['ถ่านบด',295],['ทรายละเอียด',360],['กรวดเล็ก',418],['ผ้า',454]];
 diagrams.water_filter=frame('เครื่องกรองน้ำอย่างง่าย',660,
 text('น้ำสกปรก',88,104,25,'middle',P.blue,160,700)+arrow(77,115,118,157,P.blue,3)+layers+rect(85,159,145,299,'none',P.ink,0,3)+
 labels.map(([s,y])=>line(235,y-7,249,y-7,P.ink,2)+text(s,257,y,24,'start',P.ink,178)).join('')+
 [478,496,514].map(y=>circle(157,y,5,P.blue)).join('')+rect(111,531,92,48,'#9ac2e7',P.ink,0,3)+
 lines(['กรองได้เพียงตะกอนและสิ่งสกปรก','ต้องต้มหรือฆ่าเชื้อหลังกรองทุกครั้ง'],224,609,24,35,'middle',P.red,416));
}

const executable=process.env.CHROME_PATH||['C:/Program Files/Google/Chrome/Application/chrome.exe','C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe','/usr/bin/chromium','/usr/bin/google-chrome'].find(p=>fs.existsSync(p));
if(!executable)throw new Error('Set CHROME_PATH to a local Chrome/Edge executable');
const browser=await chromium.launch({executablePath:executable,headless:true});
const page=await browser.newPage({deviceScaleFactor:1});
const reports=[];
for(const [name,source] of Object.entries(diagrams)){
  const jpg=path.join(folder,name+'.jpg'),original=path.join(backup,name+'.jpg');
  if(!fs.existsSync(original)&&fs.existsSync(jpg))fs.copyFileSync(jpg,original);
  await page.setContent('<!doctype html><meta charset="utf-8"><style>body{margin:0}svg{display:block}</style>'+source);
  await page.evaluate(()=>document.fonts.ready);
  const report=await page.evaluate(()=>{
    const changes=[],issues=[];
    for(const el of document.querySelectorAll('text[data-fit]')){
      const max=Number(el.getAttribute('data-fit')),initial=Number(el.getAttribute('font-size'));let size=initial;
      while(el.getBBox().width>max&&size>18){size-=.5;el.setAttribute('font-size',size)}
      const b=el.getBBox();if(size!==initial)changes.push({text:el.textContent,initial,size});
      if(b.x<3||b.x+b.width>445||b.y<0||b.y+b.height>document.querySelector('svg').viewBox.baseVal.height)issues.push('Text out of canvas: '+el.textContent);
      if(b.width>max+.5)issues.push('Text over label width: '+el.textContent);
    }
    const texts=[...document.querySelectorAll('text')];
    for(let i=0;i<texts.length;i++)for(let j=i+1;j<texts.length;j++){
      const a=texts[i].getBBox(),b=texts[j].getBBox();
      const ox=Math.min(a.x+a.width,b.x+b.width)-Math.max(a.x,b.x),oy=Math.min(a.y+a.height,b.y+b.height)-Math.max(a.y,b.y);
      if(ox>2&&oy>2)issues.push('Text overlap: '+texts[i].textContent+' / '+texts[j].textContent);
    }
    return {changes,issues,texts:texts.length,svg:document.querySelector('svg').outerHTML,height:document.querySelector('svg').height.baseVal.value};
  });
  fs.writeFileSync(path.join(folder,name+'.svg'),report.svg+'\n');
  await page.setViewportSize({width:448,height:report.height});
  await page.screenshot({path:jpg,type:'jpeg',quality:94,clip:{x:0,y:0,width:448,height:report.height}});
  reports.push({name,width:448,height:report.height,texts:report.texts,resizedLabels:report.changes,issues:report.issues});
  if(report.issues.length)console.log(report.issues);
  console.log(name+': 448x'+report.height+', '+report.texts+' Thai labels');
}
await browser.close();
fs.writeFileSync(path.join(qa,'validation.json'),JSON.stringify({diagrams:reports.length,font:'Sarabun Regular (OFL)',reports},null,2));
if(reports.length!==22)throw new Error('Expected every one of the 22 source diagrams');
if(reports.some(r=>r.issues.length))throw new Error('Fix the diagram label bounds reported above');
