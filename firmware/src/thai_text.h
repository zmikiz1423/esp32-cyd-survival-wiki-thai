#pragma once
#include <TFT_eSPI.h>
#include "utf8.h"

// Preserve the TFT drawing/viewport APIs while routing every text operation
// through one UTF-8 shaping, measurement and clipping implementation.
class ThaiDisplay : public TFT_eSPI {
public:
  void setTextFont(uint8_t f=1) { atlasIndex=f==1?0:1; TFT_eSPI::setTextFont(f); }
  void setFreeFont(const GFXfont *f=nullptr);
  void setThaiSize(uint8_t px);
  void setTextDatum(uint8_t d) { datum=d; TFT_eSPI::setTextDatum(d); }
  void setTextColor(uint16_t fg) { ink=fg; TFT_eSPI::setTextColor(fg); }
  void setTextColor(uint16_t fg,uint16_t bg,bool fill=false) { ink=fg; TFT_eSPI::setTextColor(fg,bg,fill); }
  int16_t textWidth(const char *s,uint8_t font=0);
  int16_t textWidth(const String &s,uint8_t font=0) { return textWidth(s.c_str(),font); }
  int16_t drawString(const char *s,int32_t x,int32_t y,uint8_t font=0);
  int16_t drawString(const String &s,int32_t x,int32_t y,uint8_t font=0) { return drawString(s.c_str(),x,y,font); }
  String fit(const String &s,int maxW);
  int lineHeight() const;
private:
  int render(const char *s,int x,int y,bool paint);
  uint8_t atlasIndex=1,datum=TL_DATUM;
  uint16_t ink=TFT_WHITE;
};

extern ThaiDisplay tft;
