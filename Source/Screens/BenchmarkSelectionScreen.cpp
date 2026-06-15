#include "Screens/BenchmarkSelectionScreen.h"
#include "Screens/UiHelpers.h"
#include "Tui.h"
#include "Renderer.h"
#include "ColorTheme.h"
#include "BenchmarkRegistry.h"
#include "IBenchmark.h"
#include "SystemInfo.h"
#include "Freestanding.h"

void BenchmarkSelectionScreen::OnEnter(Tui& /*tui*/) {
    mBmCount = BenchmarkRegistry::Count();
    mEmpty   = (mBmCount == 0);
    mCursor  = 0;
    for (int i = 0; i < 32; ++i) mSelected[i] = false;

    mMpAvail = SystemInfo::HasMpServices();
    mApCount = 0;
    if (mMpAvail) {
        UINT32 enabled = SystemInfo::GetEnabledProcessorCount();
        mApCount = (enabled > 1) ? enabled - 1 : 0;
        if (mApCount == 0) mMpAvail = false;
    }

    IBenchmark** all = BenchmarkRegistry::GetAll();
    for (UINTN i = 0; i < mBmCount && i < 32; ++i) {
        ThreadingMode tm = all[i]->GetThreadingMode();
        mModes[i] = (tm == ThreadingMode::MultiOnly) ? RunMode::MultiCore : RunMode::SingleCore;
    }

    mVp.Clear();  // scroll persists across frames
}

void BenchmarkSelectionScreen::Draw(Tui& /*tui*/, int top, int /*bottom*/) {
    if (mEmpty) {
        Renderer::DrawText(2, 5, "No benchmarks registered.", Theme::Current().Error);
        return;
    }

    IBenchmark** all = BenchmarkRegistry::GetAll();

    // Layout (relative to header): hint at top+1, blank at top+2, list at top+3.
    const int contentStart  = top + 3;
    const int kBelowContent  = 5;  // sep + desc + footer(3)

    int viewRows = static_cast<int>(Renderer::Rows()) - contentStart - kBelowContent;
    if (viewRows < 4) viewRows = 4;

    mVp.ClearContent();
    int cursorVpRow = 0;

    for (UINTN i = 0; i < mBmCount; ++i) {
        ThreadingMode tm = all[i]->GetThreadingMode();
        const char* name = all[i]->GetName();
        const char* cat  = all[i]->GetCategory();

        char label[128];
        int p = 0;
        label[p++] = ' '; label[p++] = ' ';
        label[p++] = (static_cast<int>(i) == mCursor) ? '>' : ' ';
        label[p++] = ' ';
        label[p++] = '[';
        label[p++] = mSelected[i] ? 'X' : ' ';
        label[p++] = ']';
        label[p++] = ' ';

        for (int j = 0; name[j] && p < 36; ++j) label[p++] = name[j];
        while (p < 38) label[p++] = ' ';
        label[p++] = '[';
        for (int j = 0; cat[j] && p < 48; ++j) label[p++] = cat[j];
        label[p++] = ']';
        while (p < 52) label[p++] = ' ';

        const char* modeStr;
        if (!mMpAvail || tm == ThreadingMode::SingleOnly)
            modeStr = "Single";
        else if (mModes[i] == RunMode::CoreCycle)
            modeStr = "Cycle ";
        else if (mModes[i] == RunMode::MultiCore)
            modeStr = "Multi ";
        else
            modeStr = "Single";

        for (int j = 0; modeStr[j] && p < 64; ++j) label[p++] = modeStr[j];
        if (mMpAvail && tm != ThreadingMode::SingleOnly) {
            for (const char* s = " \x11\x10"; *s && p < 80; ++s) label[p++] = *s;
        }

        int cols = static_cast<int>(Renderer::Columns());
        while (p < cols && p < 126) label[p++] = ' ';
        label[p] = '\0';

        bool hi = (static_cast<int>(i) == mCursor);
        if (hi) cursorVpRow = mVp.TotalLines();

        if (hi)
            mVp.AddLine(label, Theme::Current().HighlightTxt, Theme::Current().Highlight);
        else
            mVp.AddLine(label, Theme::Current().Text, Theme::Current().Background);
    }

    mVp.ScrollToLine(cursorVpRow, viewRows);

    // Hint line (top+1) describing controls / MP availability.
    if (mMpAvail) {
        char hint[80]; int p = 0;
        for (const char* s = "Space:Toggle  Left/Right:Mode ("; *s; ++s) hint[p++] = *s;
        const char* ns = UintToStr(mApCount);
        for (int j = 0; ns[j]; ++j) hint[p++] = ns[j];
        for (const char* s = " APs available)"; *s; ++s) hint[p++] = *s;
        hint[p] = '\0';
        Renderer::DrawText(2, top + 1, hint, Theme::Current().TextDim);
    } else {
        Renderer::DrawText(2, top + 1, "Space:Toggle  (single-core only — no MP Services)",
                           Theme::Current().TextDim);
    }

    mVp.Render(contentStart, viewRows);

    int sepRow  = contentStart + viewRows;
    int descRow = sepRow + 1;
    Ui::DrawSeparator(sepRow);
    Renderer::DrawText(2, descRow, all[mCursor]->GetDescription(), Theme::Current().TextDim);
}

NavResult BenchmarkSelectionScreen::HandleKey(Tui& tui, EFI_INPUT_KEY key) {
    if (mEmpty) return NavBack();

    IBenchmark** all = BenchmarkRegistry::GetAll();
    int n = static_cast<int>(mBmCount);

    if (key.ScanCode == SCAN_UP) {
        mCursor = (mCursor - 1 + n) % n;
    } else if (key.ScanCode == SCAN_DOWN) {
        mCursor = (mCursor + 1) % n;
    } else if (key.UnicodeChar == ' ') {
        mSelected[mCursor] = !mSelected[mCursor];
    } else if (key.ScanCode == SCAN_LEFT || key.ScanCode == SCAN_RIGHT) {
        ThreadingMode tm = all[mCursor]->GetThreadingMode();
        if (mMpAvail && tm != ThreadingMode::SingleOnly) {
            if (key.ScanCode == SCAN_RIGHT) {
                if (tm == ThreadingMode::MultiOnly) {
                    mModes[mCursor] = (mModes[mCursor] == RunMode::MultiCore)
                                      ? RunMode::CoreCycle : RunMode::MultiCore;
                } else {
                    if      (mModes[mCursor] == RunMode::SingleCore) mModes[mCursor] = RunMode::MultiCore;
                    else if (mModes[mCursor] == RunMode::MultiCore)  mModes[mCursor] = RunMode::CoreCycle;
                    else                                              mModes[mCursor] = RunMode::SingleCore;
                }
            } else {
                if (tm == ThreadingMode::MultiOnly) {
                    mModes[mCursor] = (mModes[mCursor] == RunMode::MultiCore)
                                      ? RunMode::CoreCycle : RunMode::MultiCore;
                } else {
                    if      (mModes[mCursor] == RunMode::SingleCore) mModes[mCursor] = RunMode::CoreCycle;
                    else if (mModes[mCursor] == RunMode::CoreCycle)  mModes[mCursor] = RunMode::MultiCore;
                    else                                              mModes[mCursor] = RunMode::SingleCore;
                }
            }
        }
    } else if (key.ScanCode == SCAN_ESC) {
        return NavBack();
    } else if (key.UnicodeChar == '\r' || key.UnicodeChar == '\n') {
        Tui::PendingRun& pr = tui.Pending();
        pr.count = 0;
        pr.category = nullptr;
        for (UINTN i = 0; i < mBmCount; ++i) {
            if (mSelected[i]) { pr.indices[pr.count] = i; pr.modes[pr.count] = mModes[i]; ++pr.count; }
        }
        // Replace self so cancelling/finishing the picker returns to the main menu.
        if (pr.count > 0) return NavReplace(ScreenId::RunCountPicker);
        return NavBack();
    }
    return NavStay();
}
