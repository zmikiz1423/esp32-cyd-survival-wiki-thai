// Article / book renderer: loads a small Markdown-style text file from SD,
// word-wraps it for the 320px screen and draws any part of it with images.
#pragma once
#include <Arduino.h>
#include <vector>

enum DocStyle : uint8_t { S_H1, S_H2, S_BODY, S_WARN, S_WARNPAD, S_CAPTION, S_IMG, S_RULE, S_BULLET, S_MARK };

struct DocItem {
  uint16_t off;   // start of text (or image path) in buffer
  uint16_t len;
  int16_t x;
  int32_t y;
  int16_t h;
  uint8_t style;
};

class Doc {
public:
  bool load(const char *path, bool markdown);
  void free();
  void draw(int scroll);          // draws into the current content viewport
  int  height() const { return totalH; }
  String title() const { return docTitle; }
  bool loaded() const { return buf != nullptr; }
private:
  void layout(bool markdown);
  void wrap(int start, int end, int x0, int width, uint8_t style);
  void addGap(int px) { cursorY += px; }
  char *buf = nullptr;
  int len = 0;
  int cursorY = 0;
  int totalH = 0;
  String docTitle;
  std::vector<DocItem> items;
};

void docSanitize(char *s, int &len);   // normalize line endings; preserve Unicode

