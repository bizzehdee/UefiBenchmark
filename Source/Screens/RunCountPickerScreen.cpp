#include "Screens/RunCountPickerScreen.h"
#include "Tui.h"
#include "Renderer.h"
#include "ColorTheme.h"
#include "Freestanding.h"

void RunCountPickerScreen::OnEnter(Tui& tui) {
    Tui::PendingRun& pr = tui.Pending();
    mRuns              = 1;
    mCoreCycleAllCores = true;
    mCursor            = 0;

    mHasCoreCycle = false;
    for (UINTN i = 0; i < pr.count; ++i)
        if (pr.modes[i] == RunMode::CoreCycle) { mHasCoreCycle = true; break; }
    mPickerRows = mHasCoreCycle ? 2 : 1;

    // Footer: "Running <count> benchmark(s)".
    int p = 0;
    for (const char* s = "Running "; *s; ++s) mFooter[p++] = *s;
    const char* ns = UintToStr(pr.count);
    for (int j = 0; ns[j] && p < 44; ++j) mFooter[p++] = ns[j];
    for (const char* s = " benchmark(s)"; *s && p < 47; ++s) mFooter[p++] = *s;
    mFooter[p] = '\0';
}

void RunCountPickerScreen::Draw(Tui& /*tui*/, int top, int /*bottom*/) {
    int row = top + 1;  // blank row after header

    const char* prompt = mHasCoreCycle
        ? "How many times to run on each core?"
        : "How many times to run each benchmark?";
    Renderer::DrawText(2, row, prompt, Theme::Current().TextDim);
    row += 2;

    // Runs row
    {
        bool runsHi = (mCursor == 0);
        char line[64];
        int p = 0;
        line[p++] = runsHi ? '>' : ' '; line[p++] = ' ';
        for (const char* s = "Runs:  "; *s; ++s) line[p++] = *s;
        const char* ns = UintToStr(static_cast<UINT64>(mRuns));
        for (int j = 0; ns[j]; ++j) line[p++] = ns[j];
        line[p] = '\0';
        Renderer::DrawText(2, row, line,
            runsHi ? Theme::Current().Accent : Theme::Current().Text);
    }
    row++;

    // Core scope row (core-cycle only)
    if (mHasCoreCycle) {
        row++;
        bool scopeHi = (mCursor == 1);
        char line[80];
        int p = 0;
        line[p++] = scopeHi ? '>' : ' '; line[p++] = ' ';
        for (const char* s = "Core scope:  "; *s; ++s) line[p++] = *s;
        const char* sv = mCoreCycleAllCores ? "[All Cores]" : "[Selected]";
        for (int j = 0; sv[j]; ++j) line[p++] = sv[j];
        line[p] = '\0';
        Renderer::DrawText(2, row, line,
            scopeHi ? Theme::Current().Accent : Theme::Current().Text);
        row++;
    }

    row++;
    Renderer::DrawText(2, row,
        "[Up/Dn] Field  [Left/Right] Adjust  [Enter] Start  [Esc] Cancel",
        Theme::Current().TextDim);
}

NavResult RunCountPickerScreen::HandleKey(Tui& tui, EFI_INPUT_KEY key) {
    if (key.ScanCode == SCAN_UP) {
        mCursor = (mCursor - 1 + mPickerRows) % mPickerRows;
    } else if (key.ScanCode == SCAN_DOWN) {
        mCursor = (mCursor + 1) % mPickerRows;
    } else if (key.ScanCode == SCAN_LEFT || key.ScanCode == SCAN_RIGHT) {
        if (mCursor == 0) {
            if (key.ScanCode == SCAN_LEFT  && mRuns > 1)  --mRuns;
            if (key.ScanCode == SCAN_RIGHT && mRuns < 10) ++mRuns;
        } else {
            mCoreCycleAllCores = !mCoreCycleAllCores;
        }
    } else if (key.ScanCode == SCAN_ESC) {
        return NavBack();
    } else if (key.UnicodeChar == '\r' || key.UnicodeChar == '\n') {
        Tui::PendingRun& pr = tui.Pending();
        pr.runs              = static_cast<UINTN>(mRuns);
        pr.coreCycleAllCores = mCoreCycleAllCores;
        tui.RunPending();   // BenchmarkRunner owns the display during the run
        return NavReplace(pr.category ? ScreenId::CategoryResults : ScreenId::Results);
    }
    return NavStay();
}
