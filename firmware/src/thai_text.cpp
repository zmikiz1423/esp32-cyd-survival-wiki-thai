#include "thai_text.h"
#include "thai_font_data.h"
#include <math.h>

static const ThaiShape *findShape(uint32_t key) {
  int lo=0,hi=thaiShapeCount-1;
  while(lo<=hi) { int m=(lo+hi)/2; uint32_t k=thaiShapes[m].key; if(k==key) return &thaiShapes[m]; if(k<key)lo=m+1;else hi=m-1; }
  return nullptr;
}
static const ThaiCodepoint *findCodepoint(uint32_t cp) {
  int lo=0,hi=thaiCodepointCount-1;
  while(lo<=hi) { int m=(lo+hi)/2; uint32_t c=thaiCodepoints[m].codepoint; if(c==cp)return &thaiCodepoints[m];if(c<cp)lo=m+1;else hi=m-1; }
  return nullptr;
}
void ThaiDisplay::setFreeFont(const GFXfont *f) {
  atlasIndex=(f==&FreeSansBold12pt7b||f==&FreeSansBold18pt7b)?4:2;
  TFT_eSPI::setFreeFont(f);
}
void ThaiDisplay::setThaiSize(uint8_t px) {
  atlasIndex=px<=12?0:px<=14?1:px<=16?2:px<=18?3:4;
}
int ThaiDisplay::lineHeight() const { return thaiAtlases[atlasIndex].height; }

int ThaiDisplay::render(const char *s,int x,int y,bool paint) {
  const ThaiAtlas &a=thaiAtlases[atlasIndex];
  int len=strlen(s),p=0; int32_t pen=0;
  auto glyph=[&](uint16_t gid,int gx,int gy) {
    if(!paint)return;
    const ThaiBitmap &b=a.glyphs[gid];
    int x0=x+gx+b.left, y0=y+a.baseline-gy-b.top;
    int stride=(b.width+1)/2;
    // Read/compose one small glyph tile (at most 4.5 KB), not one SPI read per
    // pixel. Marks blend with the existing consonant instead of erasing it.
    static uint16_t tile[48*48];
    if(b.width>48||b.height>48||!b.width||!b.height)return;
    memset(tile,0,sizeof(tile));
    readRect(x0,y0,b.width,b.height,tile);
    for(int row=0;row<b.height;row++) for(int col=0;col<b.width;col++) {
      uint8_t packed=a.pixels[b.offset+row*stride+col/2];
      uint8_t alpha=col&1?packed&15:packed>>4;
      if(!alpha)continue;
      uint16_t color=ink;
      if(alpha<15) {
        uint16_t raw=tile[row*b.width+col];
        uint16_t bg=(raw<<8)|(raw>>8); // readRect/pushRect use swapped RGB565
        int r=(((ink>>11)&31)*alpha+((bg>>11)&31)*(15-alpha)+7)/15;
        int g=(((ink>>5)&63)*alpha+((bg>>5)&63)*(15-alpha)+7)/15;
        int bl=((ink&31)*alpha+(bg&31)*(15-alpha)+7)/15;
        color=(r<<11)|(g<<5)|bl;
      }
      tile[row*b.width+col]=(color<<8)|(color>>8);
    }
    pushRect(x0,y0,b.width,b.height,tile);
  };
  while(p<len) {
    uint32_t cp=utf8Decode(s,len,p);
    if(cp==0x200B||cp==0xFEFF)continue;
    const ThaiShape *sh=nullptr;
    if(cp>=0xE01&&cp<=0xE5B) {
      uint32_t key=(cp&255)<<24; sh=findShape(key);
      int q=p,used=p;
      for(int n=1;n<4&&q<len;n++) {
        uint32_t next=utf8Decode(s,len,q);
        if(!(thaiMark(next)||next==0xE33||next==0xE32))break;
        key|=(next&255)<<(24-8*n);
        const ThaiShape *candidate=findShape(key);
        if(candidate) { sh=candidate; used=q; }
      }
      p=used;
    }
    if(sh) {
      for(int i=0;i<sh->count;i++) {
        const ThaiRunGlyph &g=thaiRuns[sh->offset+i];
        glyph(g.glyph,(pen+g.x*a.size+500)/1000,(int)roundf(g.y*a.size/1000.0f));
      }
      pen+=sh->advance*a.size;
    } else {
      const ThaiCodepoint *g=findCodepoint(cp);
      if(!g)g=findCodepoint('?');
      glyph(g->glyph,(pen+500)/1000,0); pen+=g->advance*a.size;
    }
  }
  return (pen+999)/1000;
}
int16_t ThaiDisplay::textWidth(const char *s,uint8_t font) {
  uint8_t old=atlasIndex;if(font)setTextFont(font);
  int w=render(s,0,0,false);atlasIndex=old;return w;
}
String ThaiDisplay::fit(const String &s,int maxW) {
  if(maxW<=0)return "";
  if(textWidth(s)<=maxW)return s;
  const char *b=s.c_str();int len=s.length(),p=0,fit=0;
  int ell=textWidth("..."); if(ell>maxW)return "";
  while(p<len) {int e=utf8ClusterEnd(b,len,p);if(textWidth(s.substring(0,e))+ell>maxW)break;fit=e;p=e;}
  String out=s.substring(0,fit);out.trim();return out+"...";
}
int16_t ThaiDisplay::drawString(const char *s,int32_t x,int32_t y,uint8_t font) {
  if(font)setTextFont(font);
  int horizontal=datum%3,vertical=datum/3;
  int limit=horizontal==0?width()-x:horizontal==1?2*min<int>(x,width()-x):x;
  String fitted=fit(String(s),max(0,limit));int w=textWidth(fitted),h=lineHeight();
  if(horizontal==1)x-=w/2;else if(horizontal==2)x-=w;
  if(vertical==1)y-=h/2;else if(vertical>=2)y-=h;
  render(fitted.c_str(),x,y,true);return w;
}
