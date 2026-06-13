#pragma once
// GOP-based framebuffer renderer with character-grid text API.
// Falls back to ConOut text mode if GOP is unavailable.

#include "UefiTypes.h"
#include "ColorTheme.h"

namespace Renderer {

// Available GOP mode descriptor (de-duplicated by resolution, BGR preferred).
struct ModeDesc {
    UINT32 ModeIndex;
    UINT32 Width;
    UINT32 Height;
    bool   IsBGR;
};

// Initialise graphics. Auto-selects 1024x768 (then 1920x1080, then 800x600,
// then closest available). Returns true if GOP mode was established.
bool Init(UINT32 preferredWidth = 1024, UINT32 preferredHeight = 768);

// Is GOP available? If false, only text-mode functions work.
bool IsGraphics();

// Screen dimensions
UINT32 ScreenWidth();
UINT32 ScreenHeight();
UINT32 Columns();  // text columns  (ScreenWidth / (8 * FontScale))
UINT32 Rows();     // text rows     (ScreenHeight / (16 * FontScale))

// Integer font scale factor (1 or 2). Set automatically based on resolution.
UINT32 FontScale();

// ── Resolution management ───────────────────────────────────
// Fill `out` with up to `cap` de-duplicated available GOP modes.
// Returns the number of modes written.
UINT32 ListModes(ModeDesc* out, UINT32 cap);

// Switch to a GOP mode by ModeIndex (from ListModes). Reallocates the
// back-buffer and sets font scale. Returns false on failure.
bool SetModeByIndex(UINT32 modeIndex);

// Index of the currently active GOP mode.
UINT32 CurrentModeIndex();

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
// Discard all keys currently queued in the input buffer.
// Call once when entering a new screen to avoid stale key carry-over.
void FlushInput();

// ── Helpers ─────────────────────────────────────────────────
// Pad or truncate a string to exactly `width` characters.
// Writes into a static buffer — not reentrant.
const char* Pad(const char* text, int width);

} // namespace Renderer
