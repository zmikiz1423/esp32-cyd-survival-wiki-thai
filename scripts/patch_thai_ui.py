"""One-time structural UI migration, kept as implementation provenance."""
from pathlib import Path
p=Path(__file__).resolve().parents[1]/'firmware/src/main.cpp'
s=p.read_text('utf-8')
def rep(a,b):
    global s
    assert a in s,a[:100]
    s=s.replace(a,b)
def function(start,end,new):
    global s
    a=s.index(start);b=s.index(end,a)
    s=s[:a]+new+'\n\n'+s[b:]
rep('int tw = tft.textWidth(b.label);','String label=fitText(b.label,b.w-30);\n      int tw = tft.textWidth(label);')
rep('tft.drawString(b.label, ix + (SCR_W < 300 ? 12 : 15), cy);','tft.drawString(label, ix + (SCR_W < 300 ? 12 : 15), cy);')
rep('tft.drawString(b.label, cx, cy);','if(b.w<60||b.h<30)tft.setTextFont(1);\n    tft.drawString(fitText(b.label,b.w-8), cx, cy);')
rep('const int cols = 2, rows = 4, gap = TILE_GAP, perPage = cols * rows;','const int cols = 2, rows = SCR_W<300?3:4, gap = TILE_GAP, perPage = cols * rows;')
rep('x + 34, y + 8);','x + 34, y + 3);')
rep('x + 35, y + 26);','x + 35, y + 31);')
function('static int wrapLines(', 'static String ageText(', '''static int wrapLines(const String &s, int maxW, String *out, int lines) {
  int n=0,pos=0,len=s.length();const char *b=s.c_str();
  while(pos<len&&n<lines) {
    while(pos<len&&b[pos]==' ')pos++;
    if(pos>=len)break;
    int p=pos,fit=pos,br=-1;
    while(p<len) {
      int e=utf8ClusterEnd(b,len,p);
      if(tft.textWidth(s.substring(pos,e))>maxW)break;
      fit=e;int q=p;uint32_t cp=utf8Decode(b,len,q);
      if(cp==' '||cp==0x200B||cp=='-')br=e;
      p=e;
    }
    if(fit<len&&br>pos)fit=br;
    if(fit==pos)fit=utf8ClusterEnd(b,len,pos);
    out[n]=s.substring(pos,fit);out[n].trim();n++;pos=fit;
  }
  if(pos<len&&n)out[n-1]=fitText(out[n-1]+s.substring(pos),maxW);
  return n;
}''')
rep('int rh = (SCR_W < 300) ? 36 : 44;','int rh = (SCR_W < 300) ? 58 : 62;')
rep('ry + 2 + k * 16','ry + 1 + k * 27')
rep('SCR_W - 6, ry + 18);','SCR_W - 6, ry + 30);')
# Replace triage's ASCII-space wrapper with the same grapheme-aware wrapper.
a=s.index('  // wrap the question on word boundaries');b=s.index('  y += 6;',a)
s=s[:a]+'''  String question[3];
  int y=CONTENT_Y+2;
  int count=wrapLines(nd.q,SCR_W-16,question,3);
  for(int i=0;i<count;i++) { tft.drawString(question[i],8,y); y+=tft.lineHeight(); }
'''+s[b:]
# Compact status information fits below six settings rows and above footer.
rep('int lineH = (SCR_W < 300 ? 12 : 18);','int lineH = tft.lineHeight();')
rep('tft.drawString("Survival Wiki " FW_VERSION " (" BOARD_NAME ")", 12, y);','tft.drawString("Survival Wiki " FW_VERSION, 12, y);')
# Detailed status is available on its own map/radar screen; show the remaining
# information as one line on CYD instead of painting over the footer.
rep('if (gpsPresent()) {\n    GpsFix g = gpsGet();','if (gpsPresent() && y + lineH * 4 <= SCR_H - FOOTER_H) {\n    GpsFix g = gpsGet();')
rep('tft.drawString("SD card: " + String(usedMB) + " MB used of " + totalMB + " MB", 12, y + lineH * 2);','if(y+lineH*3<=SCR_H-FOOTER_H)tft.drawString("SD card: " + String(usedMB) + " MB used of " + totalMB + " MB", 12, y + lineH * 2);')
# Thai keyboard: four pages cover the complete Thai alphabet, vowels/marks,
# Thai digits and punctuation; the existing English keyboard remains available.
rep('static bool kbShift = false;','static bool kbThai = true;\nstatic uint8_t kbThaiPage = 0;\nstatic bool kbShift = false;')
rep('B_QUICK_BASE = 60,','B_KB_LANG = 54, B_KB_PAGE,\n  B_QUICK_BASE = 60,')
rep('#define KB_MAX_MSG  50','#define KB_MAX_MSG  (MESH_TEXT_LEN - 1)')
rep('int bx = 6, by = CONTENT_Y + 4, bw = SCR_W - 12, bh = 44;','int bx = 6, by = CONTENT_Y + 4, bw = SCR_W - 12, bh = 60;')
rep('by + 5 + k * 17','by + 1 + k * 27')
rep('int top = CONTENT_Y + 54;','int top = CONTENT_Y + 70;')
a=s.index('  for (int r = 0; r < 4; r++) {',s.index('static void drawKeys()'));b=s.index('  FootDef f[]',a)
s=s[:a]+'''  if(kbThai) {
    static const char *PAGES[] = {
      "กขฃคฅฆงจฉชซฌญฎฏฐฑฒณดตถทธนบปผฝพ",
      "ฟภมยรลวศษสหฬอฮะาำิีึืุูั็่้๊๋์",
      "เแโใไๅๆฯฤฦํฺ๎๏๐๑๒๓๔๕๖๗๘๙๚๛!?.,",
      "1234567890()/-+:;=%@#&*_[]{}<>\\\\"
    };
    const char *page=PAGES[kbThaiPage];int len=strlen(page),p=0;
    for(int i=0;i<30&&p<len;i++) {
      int start=p;uint32_t cp=utf8Decode(page,len,p);
      String label=String(page+start).substring(0,p-start);
      if(thaiMark(cp))label=String("ก")+label; // show marks on a sample base
      addBtn((i%10)*kw,top+(i/10)*kh,kw,kh,B_KEY_BASE+cp,label);
    }
    addBtn(0,top+3*kh,kw*3,kh,B_KB_PAGE,String("ไทย ")+String(kbThaiPage+1)+"/4");
    addBtn(kw*3,top+3*kh,kw*4,kh,B_KB_LANG,"EN");
    addBtn(kw*7,top+3*kh,SCR_W-kw*7,kh,B_KB_DEL,"<-");
  } else {
    for(int r=0;r<4;r++) {
      int len=strlen(ROWS[r]),x0=(SCR_W-len*kw)/2;
      if(r==3)x0=kw/2;
      for(int c=0;c<len;c++) {
        char ch=ROWS[r][c];String label=String(kbShift&&isalpha(ch)?(char)toupper(ch):ch);
        addBtn(x0+c*kw,top+r*kh,kw,kh,B_KEY_BASE+ch,label);
      }
      if(r==3)addBtn(x0+len*kw,top+r*kh,SCR_W-(x0+len*kw),kh,B_KB_DEL,"<-");
    }
  }
  int y=top+4*kh;
  addBtn(0,y,kw*2,kh,kbThai?B_KB_LANG:B_KB_SHIFT,kbThai?"EN":kbShift?"ABC":"abc");
  addBtn(kw*2,y,kw*4,kh,B_KEY_BASE+' ',"space");
  addBtn(kw*6,y,kw*2,kh,B_KB_LANG,kbThai?"EN":"ไทย");
  addBtn(kw*8,y,SCR_W-kw*8,kh,B_KEY_BASE+'?',"?");
'''+s[b:]
a=s.index('    char c = (char)(id - B_KEY_BASE);');b=s.index('    drawKbText();',a)
s=s[:a]+'''    uint32_t cp=id-B_KEY_BASE;
    if(cp<128&&kbShift&&isalpha((unsigned char)cp))cp=toupper(cp);
    char encoded[5]={0};
    if(cp<128)encoded[0]=cp;
    else if(cp<2048) {encoded[0]=0xC0|(cp>>6);encoded[1]=0x80|(cp&63);}
    else {encoded[0]=0xE0|(cp>>12);encoded[1]=0x80|((cp>>6)&63);encoded[2]=0x80|(cp&63);}
    if(kbBuf.length()+strlen(encoded)<=(unsigned)kbMax())kbBuf+=encoded;
'''+s[b:]
rep('case B_KB_DEL: if (kbBuf.length()) kbBuf.remove(kbBuf.length() - 1);','case B_KB_LANG: kbThai=!kbThai; drawKeys(); break;\n    case B_KB_PAGE: kbThaiPage=(kbThaiPage+1)%4; drawKeys(); break;\n    case B_KB_DEL: if (kbBuf.length()) kbBuf.remove(utf8PreviousCluster(kbBuf.c_str(),kbBuf.length()));')
rep('prefs.putString("nick", v.substring(0, MESH_NAME_LEN - 1));','{ char nick[MESH_NAME_LEN];utf8Copy(nick,v.c_str(),sizeof(nick));prefs.putString("nick",nick); }')
rep('char mapToast[24]','char mapToast[96]')
p.write_text(s,'utf-8',newline='\n')
print('UI structural migration applied')
