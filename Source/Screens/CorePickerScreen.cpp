#include "Screens/CorePickerScreen.h"
#include "Screens/UiHelpers.h"
#include "Tui.h"
#include "Renderer.h"
#include "ColorTheme.h"
#include "SystemInfo.h"
#include "CoreSelection.h"
#include "Freestanding.h"

void CorePickerScreen::OnEnter(Tui& /*tui*/) {
    mEmpty   = (!SystemInfo::HasMpServices() || CoreSelection::Count() == 0);
    mCursor  = 0;
    mApCount = CoreSelection::Count();
}

void CorePickerScreen::Draw(Tui& /*tui*/, int top, int /*bottom*/) {
    if (mEmpty) {
        Renderer::DrawText(2, 5, "MP Services not available; single-core only.",
                           Theme::Current().Warning);
        return;
    }

    CoreSelection::ApInfo* roster = CoreSelection::GetAll();
    UINT32 apCount = mApCount;

    const int contentStart = top + 3;
    const int footerRows   = 3;
    mViewRows = static_cast<int>(Renderer::Rows()) - contentStart - footerRows;
    if (mViewRows < 4) mViewRows = 4;

    // Info line at top+1
    UINT32 selCount = CoreSelection::SelectedCount();
    bool   bspOn    = CoreSelection::GetIncludeBsp();
    {
        char info[96]; int p = 0;
        const char* ns = UintToStr(selCount);
        for (int j = 0; ns[j]; ++j) info[p++] = ns[j];
        for (const char* s = " of "; *s; ++s) info[p++] = *s;
        ns = UintToStr(apCount);
        for (int j = 0; ns[j]; ++j) info[p++] = ns[j];
        for (const char* s = " APs selected"; *s; ++s) info[p++] = *s;
        if (bspOn) for (const char* s = " + BSP (Core 0)"; *s; ++s) info[p++] = *s;
        info[p] = '\0';
        Renderer::DrawText(2, top + 1, info, Theme::Current().TextDim);
    }

    // BSP toggle at top+2 — stays outside the viewport
    Ui::DrawMenuItem(top + 2, "Include BSP (Core 0) as benchmark worker", false, true, bspOn);

    // Rebuild AP list each frame (selection state changes)
    mVp.ClearContent();
    for (UINT32 i = 0; i < apCount; ++i) {
        auto& ap = roster[i];
        char label[ScrollViewport::MAX_WIDTH];
        int p = 0;
        label[p++] = ' '; label[p++] = ' ';
        label[p++] = (static_cast<int>(i) == mCursor) ? '>' : ' ';
        label[p++] = ' ';
        label[p++] = '[';
        label[p++] = ap.Selected ? 'X' : ' ';
        label[p++] = ']';
        label[p++] = ' ';
        for (const char* s = "AP "; *s; ++s) label[p++] = *s;
        const char* ns = UintToStr(ap.ProcIndex);
        for (int j = 0; ns[j] && p < 14; ++j) label[p++] = ns[j];
        while (p < 14) label[p++] = ' ';
        for (const char* s = "  Pkg:"; *s && p < 96; ++s) label[p++] = *s;
        ns = UintToStr(ap.Package);
        for (int j = 0; ns[j] && p < 96; ++j) label[p++] = ns[j];
        for (const char* s = "  Core:"; *s && p < 96; ++s) label[p++] = *s;
        ns = UintToStr(ap.Core);
        for (int j = 0; ns[j] && p < 96; ++j) label[p++] = ns[j];
        for (const char* s = "  Thr:"; *s && p < 96; ++s) label[p++] = *s;
        ns = UintToStr(ap.Thread);
        for (int j = 0; ns[j] && p < 96; ++j) label[p++] = ns[j];
        if (!ap.Available)
            for (const char* s = "  [disabled by firmware]"; *s && p < 100; ++s) label[p++] = *s;
        int cols = static_cast<int>(Renderer::Columns());
        while (p < cols && p < ScrollViewport::MAX_WIDTH - 1) label[p++] = ' ';
        label[p] = '\0';

        bool hi = (static_cast<int>(i) == mCursor);
        if (hi)
            mVp.AddLine(label, Theme::Current().HighlightTxt, Theme::Current().Highlight);
        else
            mVp.AddLine(label, Theme::Current().Text, Theme::Current().Background);
    }

    mVp.ScrollToLine(mCursor, mViewRows);
    mVp.Render(contentStart, mViewRows);
}

NavResult CorePickerScreen::HandleKey(Tui& /*tui*/, EFI_INPUT_KEY key) {
    if (mEmpty) return NavBack();

    CoreSelection::ApInfo* roster = CoreSelection::GetAll();

    if (key.ScanCode == SCAN_UP && mCursor > 0)
        --mCursor;
    else if (key.ScanCode == SCAN_DOWN && mCursor < static_cast<int>(mApCount) - 1)
        ++mCursor;
    else if (key.UnicodeChar == ' ' && roster[mCursor].Available)
        roster[mCursor].Selected = !roster[mCursor].Selected;
    else if (key.UnicodeChar == 'b' || key.UnicodeChar == 'B')
        CoreSelection::SetIncludeBsp(!CoreSelection::GetIncludeBsp());
    else if (key.UnicodeChar == 'a' || key.UnicodeChar == 'A')
        CoreSelection::SelectAll();
    else if (key.UnicodeChar == 'p' || key.UnicodeChar == 'P')
        CoreSelection::SelectPhysicalCoresOnly();
    else if (key.UnicodeChar == '1')
        CoreSelection::SelectOnePerPackage();
    else if (key.ScanCode == SCAN_ESC)
        return NavBack();

    return NavStay();
}
