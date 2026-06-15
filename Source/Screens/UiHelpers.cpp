// Implementation of the shared TUI widget helpers (see UiHelpers.h).
// Moved verbatim from the original monolithic Tui.cpp.

#include "Screens/UiHelpers.h"
#include "Freestanding.h"
#include "Renderer.h"
#include "ColorTheme.h"

namespace Ui {

// ── String builder helper (uses static buffer) ───────────────
static char sBuf[256];

const char* Concat2(const char* a, const char* b) {
    int p = 0;
    if (a) for (int i = 0; a[i] && p < 254; ++i) sBuf[p++] = a[i];
    if (b) for (int i = 0; b[i] && p < 254; ++i) sBuf[p++] = b[i];
    sBuf[p] = '\0';
    return sBuf;
}

const char* Concat3(const char* a, const char* b, const char* c) {
    int p = 0;
    if (a) for (int i = 0; a[i] && p < 254; ++i) sBuf[p++] = a[i];
    if (b) for (int i = 0; b[i] && p < 254; ++i) sBuf[p++] = b[i];
    if (c) for (int i = 0; c[i] && p < 254; ++i) sBuf[p++] = c[i];
    sBuf[p] = '\0';
    return sBuf;
}

void DrawMenuItem(int row, const char* text, bool highlighted,
                  bool showCheckbox, bool isChecked) {
    char line[128];
    int p = 0;

    line[p++] = ' '; line[p++] = ' ';
    line[p++] = highlighted ? '>' : ' ';
    line[p++] = ' ';

    if (showCheckbox) {
        line[p++] = '[';
        line[p++] = isChecked ? 'X' : ' ';
        line[p++] = ']';
        line[p++] = ' ';
    }

    if (text) for (int i = 0; text[i] && p < 126; ++i) line[p++] = text[i];

    int cols = static_cast<int>(Renderer::Columns());
    while (p < cols && p < 127) line[p++] = ' ';
    line[p] = '\0';

    if (highlighted)
        Renderer::DrawTextBg(0, row, line, Theme::Current().HighlightTxt, Theme::Current().Highlight);
    else
        Renderer::DrawTextBg(0, row, line, Theme::Current().Text, Theme::Current().Background);
}

int DrawSeparator(int row) {
    int cols = static_cast<int>(Renderer::Columns());
    char sep[128];
    int len = cols < 127 ? cols : 127;
    for (int i = 0; i < len; ++i) sep[i] = '-';
    sep[len] = '\0';
    Renderer::DrawText(0, row, sep, Theme::Current().Separator);
    return row + 1;
}

void DrawProgressBar(int row, UINTN current, UINTN total) {
    if (total == 0) return;
    int barWidth = static_cast<int>(Renderer::Columns()) - 6;
    int filled = static_cast<int>((current * static_cast<UINT64>(barWidth)) / total);

    char bar[128];
    bar[0] = '[';
    for (int i = 0; i < barWidth && i < 125; ++i)
        bar[i + 1] = (i < filled) ? '#' : '.';
    bar[barWidth + 1] = ']';
    bar[barWidth + 2] = '\0';

    Renderer::DrawText(2, row, bar, Theme::Current().Accent);
}

}  // namespace Ui
