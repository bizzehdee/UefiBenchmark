// GOP-based framebuffer renderer with ConOut text-mode fallback.
// Handles both RGB and BGR pixel formats.

#include "Renderer.h"
#include "BitmapFont.h"
#include "Freestanding.h"

namespace Renderer {

// ── State ────────────────────────────────────────────────────
static UINT32* sFramebuffer  = nullptr;  // back-buffer (we own this)
static UINT32* sHwFb         = nullptr;  // hardware framebuffer (GOP)
static UINT32  sPitch        = 0;        // pixels per scan line
static UINT32  sWidth        = 0;
static UINT32  sHeight       = 0;
static bool    sIsGop        = false;
static bool    sIsBGR        = true;     // default to BGR (most common in UEFI)
static UINT32  sFontScale    = 1;        // integer block scale for glyph rendering
static UINT32  sCurrentMode  = 0;        // active GOP mode index

static EFI_GRAPHICS_OUTPUT_PROTOCOL* sGop = nullptr;

// ── Dirty-row tracking ────────────────────────────────────────
// Bitmask of character-grid rows written since the last Present().
// Supports up to 128 text rows (covers any plausible UEFI resolution).
static UINT64 sDirtyMask[2] = {};

static void MarkRowsDirty(int first, int last) {
    for (int r = first; r <= last && r < 128; ++r)
        sDirtyMask[r >> 6] |= (1ULL << (r & 63));
}
static inline bool IsRowDirty(int r) {
    return r < 128 && ((sDirtyMask[r >> 6] >> (r & 63)) & 1);
}
static void ClearDirtyMask()  { sDirtyMask[0] = sDirtyMask[1] = 0; }
static void MarkAllDirty()    { sDirtyMask[0] = sDirtyMask[1] = ~0ULL; }

// ── Internal helpers ─────────────────────────────────────────

// Convert a Color to a framebuffer pixel according to detected pixel format
static inline UINT32 ToPixel(Color c) {
    if (sIsBGR) {
        // PixelBlueGreenRedReserved8BitPerColor (most common)
        return static_cast<UINT32>(c.B)
             | (static_cast<UINT32>(c.G) << 8)
             | (static_cast<UINT32>(c.R) << 16);
    } else {
        // PixelRedGreenBlueReserved8BitPerColor
        return static_cast<UINT32>(c.R)
             | (static_cast<UINT32>(c.G) << 8)
             | (static_cast<UINT32>(c.B) << 16);
    }
}

static void FillRect(int px, int py, int pw, int ph, Color color) {
    if (!sFramebuffer) return;
    UINT32 pixel = ToPixel(color);
    int effH = static_cast<int>(BitmapFont::CHAR_HEIGHT) * static_cast<int>(sFontScale);
    if (effH > 0)
        MarkRowsDirty(py / effH, (py + ph - 1) / effH);
    for (int y = py; y < py + ph && y < static_cast<int>(sHeight); ++y) {
        UINT32* row = sFramebuffer + static_cast<UINT32>(y) * sPitch;
        for (int x = px; x < px + pw && x < static_cast<int>(sWidth); ++x) {
            row[x] = pixel;
        }
    }
}

// ── Font scale helper ─────────────────────────────────────────
static void UpdateFontScale(UINT32 w) {
    // Scale ×2 at 1920+ width, ×1 otherwise.
    // Guard: if ×2 would give fewer than 100 columns, fall back to ×1.
    UINT32 scale = (w >= 1920) ? 2 : 1;
    if ((w / (static_cast<UINT32>(BitmapFont::CHAR_WIDTH) * scale)) < 100)
        scale = 1;
    sFontScale = scale;
}

// ── Init ─────────────────────────────────────────────────────
bool Init(UINT32 preferredWidth, UINT32 preferredHeight) {
    if (!gBS) return false;

    EFI_GUID gopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_STATUS status = gBS->LocateProtocol(
        &gopGuid, nullptr, reinterpret_cast<VOID**>(&sGop));

    if (EFI_ERROR(status) || !sGop) {
        // GOP unavailable — text mode fallback
        sIsGop = false;
        sWidth  = 80;
        sHeight = 25;
        return false;
    }

    // Preference list: try exact matches in priority order, then fall back to
    // closest-distance so we always land on a valid mode.
    struct Pref { UINT32 w, h; };
    constexpr Pref kPrefs[] = {{1024, 768}, {1920, 1080}, {800, 600}};
    constexpr UINT32 kPrefCount = 3;

    UINT32 bestMode = sGop->Mode->Mode;
    UINT32 bestW = 0;
    UINT32 bestDist = 0xFFFFFFFF;

    // First pass: try preference list in order (exact match wins immediately)
    for (UINT32 pi = 0; pi < kPrefCount; ++pi) {
        for (UINT32 m = 0; m < sGop->Mode->MaxMode; ++m) {
            EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* info = nullptr;
            UINTN infoSize = 0;
            status = sGop->QueryMode(sGop, m, &infoSize, &info);
            if (EFI_ERROR(status) || !info) continue;
            if (info->PixelFormat != PixelRedGreenBlueReserved8BitPerColor &&
                info->PixelFormat != PixelBlueGreenRedReserved8BitPerColor)
                continue;
            if (info->HorizontalResolution == kPrefs[pi].w &&
                info->VerticalResolution   == kPrefs[pi].h) {
                bestMode = m;
                bestW    = kPrefs[pi].w;
                goto modeFound;
            }
        }
    }

    // Fallback: closest distance to the caller-supplied preferred resolution
    for (UINT32 m = 0; m < sGop->Mode->MaxMode; ++m) {
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* info = nullptr;
        UINTN infoSize = 0;
        status = sGop->QueryMode(sGop, m, &infoSize, &info);
        if (EFI_ERROR(status) || !info) continue;
        if (info->PixelFormat != PixelRedGreenBlueReserved8BitPerColor &&
            info->PixelFormat != PixelBlueGreenRedReserved8BitPerColor)
            continue;
        UINT32 w = info->HorizontalResolution;
        UINT32 h = info->VerticalResolution;
        UINT32 dw = w > preferredWidth  ? w - preferredWidth  : preferredWidth  - w;
        UINT32 dh = h > preferredHeight ? h - preferredHeight : preferredHeight - h;
        UINT32 dist = dw + dh;
        if (dist < bestDist) {
            bestDist = dist;
            bestMode = m;
            bestW = w;
        }
    }

modeFound:
    // Set the chosen mode
    if (bestW > 0) {
        status = sGop->SetMode(sGop, bestMode);
        if (EFI_ERROR(status)) {
            sIsGop = false;
            return false;
        }
    }

    // Read mode info
    sCurrentMode = sGop->Mode->Mode;
    sWidth  = sGop->Mode->Info->HorizontalResolution;
    sHeight = sGop->Mode->Info->VerticalResolution;
    sPitch  = sGop->Mode->Info->PixelsPerScanLine;
    sHwFb   = reinterpret_cast<UINT32*>(sGop->Mode->FrameBufferBase);
    sIsBGR  = (sGop->Mode->Info->PixelFormat == PixelBlueGreenRedReserved8BitPerColor);

    UpdateFontScale(sWidth);

    // Allocate back-buffer
    UINTN fbSize = static_cast<UINTN>(sPitch) * sHeight * sizeof(UINT32);
    status = gBS->AllocatePool(EfiLoaderData, fbSize,
                               reinterpret_cast<VOID**>(&sFramebuffer));
    if (EFI_ERROR(status)) {
        sFramebuffer = nullptr;
        sIsGop = false;
        return false;
    }

    sIsGop = true;
    Clear();
    Present();
    return true;
}

bool   IsGraphics()      { return sIsGop; }
UINT32 ScreenWidth()     { return sWidth; }
UINT32 ScreenHeight()    { return sHeight; }
UINT32 FontScale()       { return sFontScale; }
UINT32 CurrentModeIndex(){ return sCurrentMode; }
UINT32 Columns() {
    return sIsGop ? sWidth  / (static_cast<UINT32>(BitmapFont::CHAR_WIDTH)  * sFontScale) : 80;
}
UINT32 Rows() {
    return sIsGop ? sHeight / (static_cast<UINT32>(BitmapFont::CHAR_HEIGHT) * sFontScale) : 25;
}

// ── Resolution management ─────────────────────────────────────
UINT32 ListModes(ModeDesc* out, UINT32 cap) {
    if (!sGop || !out || cap == 0) return 0;
    UINT32 count = 0;
    for (UINT32 m = 0; m < sGop->Mode->MaxMode && count < cap; ++m) {
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* info = nullptr;
        UINTN infoSize = 0;
        EFI_STATUS status = sGop->QueryMode(sGop, m, &infoSize, &info);
        if (EFI_ERROR(status) || !info) continue;
        if (info->PixelFormat != PixelRedGreenBlueReserved8BitPerColor &&
            info->PixelFormat != PixelBlueGreenRedReserved8BitPerColor)
            continue;
        UINT32 w = info->HorizontalResolution;
        UINT32 h = info->VerticalResolution;
        bool isBGR = (info->PixelFormat == PixelBlueGreenRedReserved8BitPerColor);

        // De-duplicate: if this WxH already recorded, prefer BGR entry
        bool found = false;
        for (UINT32 j = 0; j < count; ++j) {
            if (out[j].Width == w && out[j].Height == h) {
                if (isBGR && !out[j].IsBGR) {
                    out[j].ModeIndex = m;
                    out[j].IsBGR = true;
                }
                found = true;
                break;
            }
        }
        if (!found) {
            out[count++] = { m, w, h, isBGR };
        }
    }
    return count;
}

bool SetModeByIndex(UINT32 modeIndex) {
    if (!sGop || !gBS) return false;

    EFI_STATUS status = sGop->SetMode(sGop, modeIndex);
    if (EFI_ERROR(status)) return false;

    // Free old back-buffer
    if (sFramebuffer) {
        gBS->FreePool(sFramebuffer);
        sFramebuffer = nullptr;
    }

    // Update state from new mode
    sCurrentMode = sGop->Mode->Mode;
    sWidth  = sGop->Mode->Info->HorizontalResolution;
    sHeight = sGop->Mode->Info->VerticalResolution;
    sPitch  = sGop->Mode->Info->PixelsPerScanLine;
    sHwFb   = reinterpret_cast<UINT32*>(sGop->Mode->FrameBufferBase);
    sIsBGR  = (sGop->Mode->Info->PixelFormat == PixelBlueGreenRedReserved8BitPerColor);

    UpdateFontScale(sWidth);

    // Allocate new back-buffer
    UINTN fbSize = static_cast<UINTN>(sPitch) * sHeight * sizeof(UINT32);
    status = gBS->AllocatePool(EfiLoaderData, fbSize,
                               reinterpret_cast<VOID**>(&sFramebuffer));
    if (EFI_ERROR(status)) {
        sFramebuffer = nullptr;
        sIsGop = false;
        return false;
    }

    Clear();
    Present();
    return true;
}

// ── Clear ────────────────────────────────────────────────────
void Clear() {
    Clear(Theme::Current().Background);
}

void Clear(Color color) {
    if (sIsGop && sFramebuffer) {
        MarkAllDirty();
        UINT32 pixel = ToPixel(color);
        UINTN total = static_cast<UINTN>(sPitch) * sHeight;
        for (UINTN i = 0; i < total; ++i)
            sFramebuffer[i] = pixel;
    }
}

// ── Text drawing ─────────────────────────────────────────────
void DrawText(int col, int row, const char* text, Color fg) {
    if (!sIsGop || !sFramebuffer || !text) return;
    if (row < 0 || row >= static_cast<int>(Rows())) return;
    MarkRowsDirty(row, row);
    int effW = BitmapFont::CHAR_WIDTH  * static_cast<int>(sFontScale);
    int effH = BitmapFont::CHAR_HEIGHT * static_cast<int>(sFontScale);
    int px   = col * effW;
    int py   = row * effH;

    UINT32 pixel = ToPixel(fg);
    for (int i = 0; text[i]; ++i) {
        int cx = px + i * effW;
        if (cx + effW > static_cast<int>(sWidth)) break;

        int idx = text[i] - BitmapFont::FIRST_CHAR;
        if (idx < 0 || idx >= BitmapFont::CHAR_COUNT) continue;

        const UINT8* glyph = BitmapFont::GetFontData() + idx * BitmapFont::CHAR_HEIGHT;
        for (int r = 0; r < BitmapFont::CHAR_HEIGHT; ++r) {
            UINT8 bits = glyph[r];
            if (bits == 0) continue;
            for (int sy = 0; sy < static_cast<int>(sFontScale); ++sy) {
                UINT32* line = sFramebuffer +
                    static_cast<UINT32>(py + r * static_cast<int>(sFontScale) + sy) * sPitch;
                for (int c = 0; c < BitmapFont::CHAR_WIDTH; ++c) {
                    if (bits & (1 << (7 - c))) {
                        for (int sx = 0; sx < static_cast<int>(sFontScale); ++sx)
                            line[cx + c * static_cast<int>(sFontScale) + sx] = pixel;
                    }
                }
            }
        }
    }
}

void DrawTextBg(int col, int row, const char* text, Color fg, Color bg) {
    if (!sIsGop || !sFramebuffer || !text) return;
    if (row < 0 || row >= static_cast<int>(Rows())) return;
    int effW = BitmapFont::CHAR_WIDTH  * static_cast<int>(sFontScale);
    int effH = BitmapFont::CHAR_HEIGHT * static_cast<int>(sFontScale);
    int px   = col * effW;
    int py   = row * effH;
    int len  = static_cast<int>(StrLen(text));
    FillRect(px, py, len * effW, effH, bg);
    DrawText(col, row, text, fg);
}

void FillRow(int row, Color color) {
    if (!sIsGop || !sFramebuffer) return;
    if (row < 0 || row >= static_cast<int>(Rows())) return;
    int effH = BitmapFont::CHAR_HEIGHT * static_cast<int>(sFontScale);
    int py   = row * effH;
    Renderer::FillRect(0, py, static_cast<int>(sWidth), effH, color);
}

// ── Present ──────────────────────────────────────────────────
void Present() {
    if (!sIsGop || !sFramebuffer || !sHwFb) return;

    int effH      = static_cast<int>(BitmapFont::CHAR_HEIGHT) * static_cast<int>(sFontScale);
    int textRows  = (effH > 0) ? static_cast<int>(sHeight) / effH : 0;
    UINTN rowBytes = static_cast<UINTN>(sPitch) * static_cast<UINTN>(effH) * sizeof(UINT32);

    for (int r = 0; r < textRows && r < 128; ++r) {
        if (!IsRowDirty(r)) continue;
        UINTN offset = static_cast<UINTN>(r * effH) * sPitch;
        memcpy(sHwFb + offset, sFramebuffer + offset, rowBytes);
    }

    // Handle any remaining pixel rows below the last full text row
    int pixelsDone = textRows * effH;
    if (static_cast<UINT32>(pixelsDone) < sHeight) {
        UINTN offset    = static_cast<UINTN>(pixelsDone) * sPitch;
        UINTN remaining = (static_cast<UINTN>(sHeight) - static_cast<UINTN>(pixelsDone))
                          * sPitch * sizeof(UINT32);
        memcpy(sHwFb + offset, sFramebuffer + offset, remaining);
    }

    ClearDirtyMask();
}

// ── Text-mode fallback ───────────────────────────────────────
void TextPrint(const char* str) {
    ConPrint(str);
}

void TextClear() {
    if (gST && gST->ConOut)
        gST->ConOut->ClearScreen(gST->ConOut);
}

// ── Input ────────────────────────────────────────────────────
void FlushInput() {
    if (!gST || !gST->ConIn) return;
    EFI_INPUT_KEY key = {};
    while (gST->ConIn->ReadKeyStroke(gST->ConIn, &key) == EFI_SUCCESS) {}
}

EFI_INPUT_KEY WaitKey() {
    EFI_INPUT_KEY key = {};
    if (!gST || !gST->ConIn) return key;
    UINTN index = 0;
    gBS->WaitForEvent(1, &gST->ConIn->WaitForKey, &index);
    gST->ConIn->ReadKeyStroke(gST->ConIn, &key);
    return key;
}

// ── Pad helper ───────────────────────────────────────────────
static char sPadBuf[256];

const char* Pad(const char* text, int width) {
    if (width <= 0 || width > 255) width = 255;
    int len = text ? static_cast<int>(StrLen(text)) : 0;
    int copy = len < width ? len : width;
    for (int i = 0; i < copy; ++i)
        sPadBuf[i] = text[i];
    for (int i = copy; i < width; ++i)
        sPadBuf[i] = ' ';
    sPadBuf[width] = '\0';
    return sPadBuf;
}

} // namespace Renderer
