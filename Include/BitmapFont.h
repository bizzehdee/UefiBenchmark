#pragma once
// 8x16 bitmap font renderer for GOP framebuffer.
// Covers printable ASCII 32-126.

#include "UefiTypes.h"
#include "ColorTheme.h"

namespace BitmapFont {

constexpr int CHAR_WIDTH  = 8;
constexpr int CHAR_HEIGHT = 16;
constexpr int FIRST_CHAR  = 32;
constexpr int LAST_CHAR   = 126;
constexpr int CHAR_COUNT  = LAST_CHAR - FIRST_CHAR + 1;

// Access to the raw font bitmap data (CHAR_COUNT * CHAR_HEIGHT bytes).
const UINT8* GetFontData();

// Draw a single character at pixel coordinates into a 32-bit framebuffer.
// `pitch` is pixels per scan line (not bytes).
void DrawChar(UINT32* fb, UINT32 pitch, int px, int py,
              char c, Color fg);

// Draw a null-terminated string at pixel coordinates.
void DrawString(UINT32* fb, UINT32 pitch, int px, int py,
                const char* text, Color fg);

} // namespace BitmapFont
