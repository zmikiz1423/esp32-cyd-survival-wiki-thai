#pragma once
#include <Arduino.h>
struct ThaiRunGlyph { uint16_t glyph; int16_t x, y; };
struct ThaiShape { uint32_t key; uint16_t offset, advance; uint8_t count; };
struct ThaiCodepoint { uint16_t codepoint, glyph, advance; };
struct ThaiBitmap { uint32_t offset; uint8_t width, height; int8_t left, top; };
struct ThaiAtlas { const uint8_t *pixels; const ThaiBitmap *glyphs; uint8_t size, baseline, height; };
extern const ThaiRunGlyph thaiRuns[];
extern const ThaiShape thaiShapes[];
extern const ThaiCodepoint thaiCodepoints[];
extern const uint32_t thaiShapeCount, thaiCodepointCount;
extern const ThaiAtlas thaiAtlases[];
