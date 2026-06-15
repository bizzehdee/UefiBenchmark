#include "Screens/ResolutionPickerScreen.h"
#include "Screens/UiHelpers.h"
#include "Tui.h"
#include "Renderer.h"
#include "ColorTheme.h"
#include "Freestanding.h"

void ResolutionPickerScreen::OnEnter(Tui& /*tui*/) {
    mApplied   = false;
    mModeCount = Renderer::ListModes(mModes, 64);
    mEmpty     = (mModeCount == 0);
    mCurrent   = Renderer::CurrentModeIndex();
    mCursor    = 0;
    for (UINT32 i = 0; i < mModeCount; ++i)
        if (mModes[i].ModeIndex == mCurrent) { mCursor = static_cast<int>(i); break; }
}

void ResolutionPickerScreen::Draw(Tui& /*tui*/, int top, int /*bottom*/) {
    if (mEmpty) {
        Renderer::DrawText(2, 5, "No additional modes available.", Theme::Current().Warning);
        return;
    }
    if (mApplied) {
        Renderer::DrawText(2, top + 2, "Resolution applied.", Theme::Current().Success);
        Renderer::DrawText(2, top + 4,
            Ui::Concat3(UintToStr(Renderer::ScreenWidth()), "x",
                        UintToStr(Renderer::ScreenHeight())),
            Theme::Current().Text);
        return;
    }

    Renderer::DrawText(2, top + 1, "Select a display resolution:", Theme::Current().TextDim);

    int menuStart = top + 3;
    for (UINT32 i = 0; i < mModeCount; ++i) {
        char label[80];
        int p = 0;
        const char* ws = UintToStr(mModes[i].Width);
        for (int j = 0; ws[j]; ++j) label[p++] = ws[j];
        label[p++] = ' '; label[p++] = 'x'; label[p++] = ' ';
        const char* hs = UintToStr(mModes[i].Height);
        for (int j = 0; hs[j]; ++j) label[p++] = hs[j];
        if (mModes[i].ModeIndex == mCurrent) {
            for (const char* s = "  [current]"; *s && p < 78; ++s) label[p++] = *s;
        }
        label[p] = '\0';
        Ui::DrawMenuItem(menuStart + static_cast<int>(i), label,
                         static_cast<int>(i) == mCursor);
    }

    int row = menuStart + static_cast<int>(mModeCount) + 1;
    Ui::DrawSeparator(row - 1);
    Renderer::DrawText(2, row,
        Ui::Concat3("Current: ", UintToStr(Renderer::ScreenWidth()),
                    Ui::Concat2("x", UintToStr(Renderer::ScreenHeight()))),
        Theme::Current().TextDim);
}

NavResult ResolutionPickerScreen::HandleKey(Tui& /*tui*/, EFI_INPUT_KEY key) {
    if (mEmpty)   return NavBack();
    if (mApplied) return NavBack();   // any key continues

    if (key.ScanCode == SCAN_UP) {
        mCursor = (mCursor - 1 + static_cast<int>(mModeCount)) % static_cast<int>(mModeCount);
    } else if (key.ScanCode == SCAN_DOWN) {
        mCursor = (mCursor + 1) % static_cast<int>(mModeCount);
    } else if (key.ScanCode == SCAN_ESC) {
        return NavBack();
    } else if (key.UnicodeChar == '\r' || key.UnicodeChar == '\n') {
        UINT32 chosen = mModes[static_cast<UINT32>(mCursor)].ModeIndex;
        if (chosen == mCurrent) return NavBack();   // no change
        if (Renderer::SetModeByIndex(chosen)) {
            mCurrent = Renderer::CurrentModeIndex();
            mApplied = true;        // show confirmation on next frame
            return NavStay();
        }
        return NavBack();           // apply failed
    }
    return NavStay();
}
