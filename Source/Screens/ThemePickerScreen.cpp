#include "Screens/ThemePickerScreen.h"
#include "Screens/UiHelpers.h"
#include "Tui.h"
#include "Renderer.h"
#include "ColorTheme.h"

namespace {
struct ThemeEntry { ThemeId id; const char* name; };
const ThemeEntry kThemes[] = {
    { ThemeId::Dark,             "Dark (default)"     },
    { ThemeId::Light,            "Light"              },
    { ThemeId::HighContrastDark, "High-contrast dark" },
};
constexpr int kCount = 3;
}  // namespace

void ThemePickerScreen::OnEnter(Tui& /*tui*/) {
    mCursor = 0;
    for (int i = 0; i < kCount; ++i)
        if (kThemes[i].id == Theme::CurrentId()) { mCursor = i; break; }
}

void ThemePickerScreen::Draw(Tui& /*tui*/, int top, int /*bottom*/) {
    Renderer::DrawText(2, top + 1, "Select a colour theme:", Theme::Current().TextDim);

    int menuStart = top + 3;
    for (int i = 0; i < kCount; ++i) {
        char label[64];
        int p = 0;
        for (int j = 0; kThemes[i].name[j] && p < 60; ++j) label[p++] = kThemes[i].name[j];
        if (kThemes[i].id == Theme::CurrentId()) {
            for (const char* s = "  [current]"; *s && p < 62; ++s) label[p++] = *s;
        }
        label[p] = '\0';
        Ui::DrawMenuItem(menuStart + i, label, i == mCursor);
    }

    int row = menuStart + kCount + 1;
    Ui::DrawSeparator(row - 1);
    Renderer::DrawText(2, row, Ui::Concat2("Active: ", Theme::CurrentName()),
                       Theme::Current().TextDim);
}

NavResult ThemePickerScreen::HandleKey(Tui& /*tui*/, EFI_INPUT_KEY key) {
    if (key.ScanCode == SCAN_UP) {
        mCursor = (mCursor - 1 + kCount) % kCount;
    } else if (key.ScanCode == SCAN_DOWN) {
        mCursor = (mCursor + 1) % kCount;
    } else if (key.ScanCode == SCAN_ESC) {
        return NavBack();
    } else if (key.UnicodeChar == '\r' || key.UnicodeChar == '\n') {
        Theme::Set(kThemes[mCursor].id);
        return NavBack();
    }
    return NavStay();
}
