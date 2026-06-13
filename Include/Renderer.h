#pragma once
// GOP-based framebuffer renderer with character-grid text API.
// Falls back to ConOut text mode if GOP is unavailable.

#include "UefiTypes.h"
#include "ColorTheme.h"

namespace Renderer {

// Initialise graphics. Tries GOP first; falls back to text mode.
// Returns true if GOP mode was established.
bool Init(UINT32 preferredWidth = 800, UINT32 preferredHeight = 600);

// Is GOP available? If false, only text-mode functions work.
bool IsGraphics();

// Screen dimensions
UINT32 ScreenWidth();
UINT32 ScreenHeight();
UINT32 Columns();  // text columns  (ScreenWidth / 8)
UINT32 Rows();     // text rows     (ScreenHeight / 16)

// ── Drawing (GOP mode) ──────────────────────────────────────
void Clear();
void Clear(Color color);

// Draw text at character-grid coordinates. Background is untouched.
void DrawText(int col, int row, const char* text, Color fg);
// Draw text with explicit background fill behind the text span.
void DrawTextBg(int col, int row, const char* text, Color fg, Color bg);
// Fill an entire text row with a colour.
void FillRow(int row, Color color);

// Flush the back-buffer to the screen. Call once per frame.
void Present();

// ── Drawing (text-mode fallback) ────────────────────────────
void TextPrint(const char* str);
void TextClear();

// ── Input ───────────────────────────────────────────────────
// Block until a key is pressed. Returns scan code + unicode char.
EFI_INPUT_KEY WaitKey();

// ── Helpers ─────────────────────────────────────────────────
// Pad or truncate a string to exactly `width` characters.
// Writes into a static buffer — not reentrant.
const char* Pad(const char* text, int width);

} // namespace Renderer
