#include "doc.h"
#include "config.h"
#include "thai_text.h"
#include "thai_font_data.h"
#include <TJpg_Decoder.h>
#include <SD.h>

static const int TEXT_X = (SCR_W < 300) ? 8 : 10;
static const int TEXT_W = SCR_W - (TEXT_X * 2);

static int lineHeight(uint8_t s) { return thaiAtlases[s==S_H1?4:s==S_CAPTION?1:3].height+2; }

static void setStyleFont(uint8_t s) {
  tft.setThaiSize(s==S_H1?22:s==S_CAPTION?14:18);
}

// Keep Thai (including every combining mark) intact. Normalize only CR/BOM.
void docSanitize(char *s, int &len) {
  int w=0,r=(len>=3&&(uint8_t)s[0]==0xEF&&(uint8_t)s[1]==0xBB&&(uint8_t)s[2]==0xBF)?3:0;
  for(;r<len;r++) if(s[r]!='\r')s[w++]=s[r];
  len=w;s[len]=0;
}

// Remove inline Markdown (bold, code, links) so the text wraps cleanly.
static void stripInline(char *s, int &len) {
  int w = 0;
  for (int r = 0; r < len;) {
    char c = s[r];
    if ((c == '*' && r + 1 < len && s[r + 1] == '*') || (c == '_' && r + 1 < len && s[r + 1] == '_')) { r += 2; continue; }
    if (c == '`') { r++; continue; }
    if (c == '[' && (r == 0 || s[r - 1] != '!')) {
      // [text](url) -> text   (only when it all sits on one line)
      int close = -1, end = -1;
      for (int k = r + 1; k < len && s[k] != '\n'; k++) if (s[k] == ']') { close = k; break; }
      if (close > 0 && close + 1 < len && s[close + 1] == '(') {
        for (int k = close + 2; k < len && s[k] != '\n'; k++) if (s[k] == ')') { end = k; break; }
      }
      if (end > 0) {
        for (int k = r + 1; k < close; k++) s[w++] = s[k];
        r = end + 1;
        continue;
      }
    }
    s[w++] = c;
    r++;
  }
  len = w;
  s[len] = 0;
}

bool Doc::load(const char *path, bool markdown) {
  free();
  File f = SD.open(path, FILE_READ);
  if (!f) return false;
  size_t bytes=f.size();
  if(bytes>MAX_DOC_BYTES) { f.close(); return false; } // never silently lose a chapter
  buf = (char *)malloc(bytes + 1);
  if (!buf) { f.close(); return false; }
  len = f.read((uint8_t *)buf, bytes);
  f.close();
  if (len < 0) len = 0;
  buf[len] = 0;
  docSanitize(buf, len);
  if (markdown) stripInline(buf, len);
  layout(markdown);
  return true;
}

void Doc::free() {
  if (buf) ::free(buf);
  buf = nullptr;
  len = 0;
  items.clear();
  items.shrink_to_fit();
  totalH = 0;
  docTitle = "";
}

static char tmp[768];

static const char *slice(const char *b, int start, int n) {
  if (n > (int)sizeof(tmp) - 1) { n=sizeof(tmp)-1;while(n>0&&((uint8_t)b[start+n]&0xC0)==0x80)n--; }
  memcpy(tmp, b + start, n);
  tmp[n] = 0;
  return tmp;
}

void Doc::wrap(int start, int end, int x0, int width, uint8_t style) {
  setStyleFont(style);
  int lh = lineHeight(style);
  int pos = start;
  while (pos < end) {
    while (pos < end && buf[pos] == ' ') pos++;
    if (pos >= end) break;
    int lineStart=pos,lastFit=pos,lastBreak=-1,p=pos;
    while(p<end) {
      int next=utf8ClusterEnd(buf,end,p);
      if(next-lineStart>760||tft.textWidth(slice(buf,lineStart,next-lineStart))>width)break;
      lastFit=next;
      int cpPos=p;uint32_t cp=utf8Decode(buf,end,cpPos);
      if(cp==' '||cp=='\t'||cp==0x200B||cp=='-')lastBreak=next;
      p=next;
    }
    if(lastFit<end&&lastBreak>lineStart)lastFit=lastBreak;
    if(lastFit==lineStart)lastFit=utf8ClusterEnd(buf,end,lineStart);
    items.push_back({(uint16_t)lineStart, (uint16_t)(lastFit - lineStart), (int16_t)x0, cursorY, (int16_t)lh, style});
    cursorY += lh;
    pos = lastFit;
  }
}

void Doc::layout(bool markdown) {
  items.clear();
  items.reserve(160);
  cursorY = 8;
  int i = 0;

  // Optional front matter: ---\n title: ...\n ---
  if (markdown && len > 4 && strncmp(buf, "---\n", 4) == 0) {
    int p = 4;
    while (p < len) {
      int e = p;
      while (e < len && buf[e] != '\n') e++;
      if (e - p >= 3 && strncmp(buf + p, "---", 3) == 0) { i = e + 1; break; }
      if (strncmp(buf + p, "title:", 6) == 0) {
        int t = p + 6;
        while (t < e && buf[t] == ' ') t++;
        docTitle = String(slice(buf, t, e - t));
        wrap(t, e, TEXT_X, TEXT_W, S_H1);
        addGap(4);
      }
      p = e + 1;
    }
  }

  bool lastBlank = true, inWarn = false;
  while (i < len) {
    int e = i;
    while (e < len && buf[e] != '\n') e++;
    int s = i;
    i = e + 1;
    while (s < e && buf[s] == ' ') s++;
    int n = e - s;

    bool isWarn = markdown && n >= 1 && buf[s] == '>';
    if (inWarn && !isWarn) {
      items.push_back({0, 0, 0, cursorY, 8, S_WARNPAD});
      cursorY += 8 + 6;
      inWarn = false;
    }

    if (n == 0) {
      if (!lastBlank) addGap(8);
      lastBlank = true;
      continue;
    }
    lastBlank = false;

    if (!markdown) { wrap(s, e, TEXT_X, TEXT_W, S_BODY); continue; }

    if (isWarn) {
      if (!inWarn) {
        addGap(2);
        items.push_back({0, 0, 0, cursorY, 8, S_WARNPAD});
        cursorY += 8;
        inWarn = true;
      }
      int t = s + 1;
      while (t < e && buf[t] == ' ') t++;
      wrap(t, e, (SCR_W < 300 ? 18 : 22), SCR_W - (SCR_W < 300 ? 30 : 40), S_WARN);
    } else if (n > 2 && buf[s] == '#' ) {
      int t = s;
      while (t < e && buf[t] == '#') t++;
      bool h1 = (t - s) == 1;
      while (t < e && buf[t] == ' ') t++;
      addGap(h1 ? (SCR_W < 300 ? 6 : 8) : (SCR_W < 300 ? 4 : 6));
      wrap(t, e, TEXT_X, TEXT_W, h1 ? S_H1 : S_H2);
    } else if (n >= 3 && strncmp(buf + s, "---", 3) == 0) {
      items.push_back({0, 0, 0, cursorY, (int16_t)(SCR_W < 300 ? 12 : 16), S_RULE});
      cursorY += (SCR_W < 300 ? 12 : 16);
    } else if (n > 2 && (buf[s] == '-' || buf[s] == '*') && buf[s + 1] == ' ') {
      items.push_back({0, 0, (int16_t)(SCR_W < 300 ? 10 : 14), cursorY, (int16_t)lineHeight(S_BULLET), S_MARK});
      wrap(s + 2, e, (SCR_W < 300 ? 22 : 28), SCR_W - (SCR_W < 300 ? 30 : 38), S_BULLET);
    } else if (n > 2 && isdigit((unsigned char)buf[s])) {
      int t = s;
      while (t < e && isdigit((unsigned char)buf[t])) t++;
      if (t < e - 1 && buf[t] == '.' && buf[t + 1] == ' ') {
        items.push_back({(uint16_t)s, (uint16_t)(t + 1 - s), (int16_t)(SCR_W < 300 ? 8 : 10), cursorY, (int16_t)lineHeight(S_BULLET), S_MARK});
        wrap(t + 2, e, (SCR_W < 300 ? 26 : 32), SCR_W - (SCR_W < 300 ? 34 : 42), S_BULLET);
      } else {
        wrap(s, e, TEXT_X, TEXT_W, S_BODY);
      }
    } else if (n > 5 && buf[s] == '!' && buf[s + 1] == '[') {
      // ![caption](img/file.jpg)
      int cb = s + 2, ce = cb;
      while (ce < e && buf[ce] != ']') ce++;
      int ps = ce + 2, pe = ps;
      while (pe < e && buf[pe] != ')') pe++;
      if (ce < e && ps < e && pe <= e) {
        String full = String("/wiki/") + slice(buf, ps, pe - ps);
        uint16_t w = 0, h = 0;
        addGap(4);
        if (TJpgDec.getFsJpgSize(&w, &h, full.c_str(), SD) == JDR_OK && w > 0) {
          int sc = 1;
          while (w / sc > TEXT_W && sc < 8) sc *= 2;
          int dh = h / sc;
          items.push_back({(uint16_t)ps, (uint16_t)(pe - ps), (int16_t)((SCR_W - w / sc) / 2), cursorY, (int16_t)dh, S_IMG});
          cursorY += dh + 4;
        } else {
          items.push_back({(uint16_t)ps, (uint16_t)(pe - ps), 0, cursorY, 40, S_IMG});
          cursorY += 44;
        }
        if (ce > cb) wrap(cb, ce, TEXT_X, TEXT_W, S_CAPTION);
        addGap(6);
      }
    } else {
      wrap(s, e, TEXT_X, TEXT_W, S_BODY);
    }
  }
  if (inWarn) {
    items.push_back({0, 0, 0, cursorY, 8, S_WARNPAD});
    cursorY += 8;
  }
  totalH = cursorY + 16;
}

static int jpgClipBottom = CONTENT_H;
static bool jpgOut(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bmp) {
  if (y >= jpgClipBottom) return false;   // rest of the picture is off-screen
  tft.pushImage(x, y, w, h, bmp);
  return true;
}

void Doc::draw(int scroll) {
  tft.fillRect(0, 0, SCR_W, CONTENT_H, C_BG);
  if (!buf) return;
  tft.setTextDatum(TL_DATUM);
  for (const DocItem &it : items) {
    int y = it.y - scroll;
    if (y + it.h < 0 || y > CONTENT_H) continue;
    switch (it.style) {
      case S_WARNPAD:
        tft.fillRect((SCR_W < 300 ? 6 : 10), y, SCR_W - (SCR_W < 300 ? 12 : 20), it.h, C_WARN_BG);
        tft.fillRect((SCR_W < 300 ? 6 : 10), y, (SCR_W < 300 ? 3 : 4), it.h, C_WARN_BAR);
        break;
      case S_WARN:
        tft.fillRect((SCR_W < 300 ? 6 : 10), y, SCR_W - (SCR_W < 300 ? 12 : 20), it.h, C_WARN_BG);
        tft.fillRect((SCR_W < 300 ? 6 : 10), y, (SCR_W < 300 ? 3 : 4), it.h, C_WARN_BAR);
        setStyleFont(S_WARN);
        tft.setTextColor(C_TEXT);
        tft.drawString(slice(buf, it.off, it.len), it.x, y + (SCR_W < 300 ? 2 : 4));
        break;
      case S_RULE:
        tft.drawFastHLine(TEXT_X, y + (SCR_W < 300 ? 6 : 8), TEXT_W, C_LINE);
        break;
      case S_MARK:
        if (it.len == 0) {
          tft.fillCircle(it.x + (SCR_W < 300 ? 3 : 4), y + (SCR_W < 300 ? 9 : 11), (SCR_W < 300 ? 2 : 3), C_ACCENT);
        } else {
          setStyleFont(S_BODY);
          tft.setTextColor(C_ACCENT);
          tft.drawString(slice(buf, it.off, it.len), it.x, y + (SCR_W < 300 ? 2 : 4));
        }
        break;
      case S_IMG: {
        String full = String("/wiki/") + slice(buf, it.off, it.len);
        uint16_t w = 0, h = 0;
        if (it.x == 0 && it.h == 40) {
          tft.drawRect(TEXT_X, y, TEXT_W, (SCR_W < 300 ? 30 : 36), C_LINE);
          tft.setTextFont(2);
          tft.setTextColor(C_MUTED);
          tft.drawString("[ไม่พบรูปภาพ]", TEXT_X + 6, y + 8);
        } else if (TJpgDec.getFsJpgSize(&w, &h, full.c_str(), SD) == JDR_OK) {
          int sc = 1;
          while (w / sc > TEXT_W && sc < 8) sc *= 2;
          TJpgDec.setJpgScale(sc);
          TJpgDec.setCallback(jpgOut);
          jpgClipBottom = CONTENT_H;
          TJpgDec.drawFsJpg(it.x, y, full.c_str(), SD);
        }
        break;
      }
      default: {
        setStyleFont(it.style);
        uint16_t col = C_TEXT;
        if (it.style == S_H2) col = C_ACCENT;
        if (it.style == S_CAPTION) col = C_MUTED;
        tft.setTextColor(col);
        tft.drawString(slice(buf, it.off, it.len), it.x, y + (it.style == S_CAPTION ? 1 : (SCR_W < 300 ? 2 : 4)));
        break;
      }
    }
  }
}

