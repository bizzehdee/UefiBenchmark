#include "Screens/MainMenuScreen.h"
#include "Screens/UiHelpers.h"
#include "Tui.h"
#include "Renderer.h"
#include "ColorTheme.h"
#include "BenchmarkRegistry.h"
#include "IBenchmark.h"
#include "Freestanding.h"

// Static entries shown after the dynamic category block.
static const char* kPostCat[] = {
    "Select Benchmarks",
    "View Last Results",
    "System Info",
    "Change Resolution",
    "Change Theme",
    "Select Cores",
    "Shutdown",
};

void MainMenuScreen::Draw(Tui& /*tui*/, int top, int /*bottom*/) {
    // Build dynamic category labels ("Run All <Category>"), max 8.
    mCatCount = BenchmarkRegistry::GetCategoryCount();
    if (mCatCount > 8) mCatCount = 8;
    for (UINT32 i = 0; i < mCatCount; ++i) {
        int p = 0;
        for (const char* s = "Run All "; *s; ++s) mCatLabels[i][p++] = *s;
        const char* cn = BenchmarkRegistry::GetCategoryName(i);
        for (int j = 0; cn[j] && p < 46; ++j) mCatLabels[i][p++] = cn[j];
        mCatLabels[i][p] = '\0';
    }

    // Flat option array (max 8 categories + 7 fixed).
    const char* opts[15];
    for (UINT32 i = 0; i < mCatCount; ++i) opts[i] = mCatLabels[i];
    for (int i = 0; i < kPostCount; ++i) opts[mCatCount + i] = kPostCat[i];
    mTotalOpts = static_cast<int>(mCatCount) + kPostCount;
    if (mCursor >= mTotalOpts) mCursor = mTotalOpts - 1;

    int menuStart = top + 1;  // blank row after header
    for (int i = 0; i < mTotalOpts; ++i)
        Ui::DrawMenuItem(menuStart + i, opts[i], i == mCursor);
}

NavResult MainMenuScreen::HandleKey(Tui& tui, EFI_INPUT_KEY key) {
    if (key.ScanCode == SCAN_UP) {
        mCursor = (mCursor - 1 + mTotalOpts) % mTotalOpts;
        return NavStay();
    }
    if (key.ScanCode == SCAN_DOWN) {
        mCursor = (mCursor + 1) % mTotalOpts;
        return NavStay();
    }
    if (key.UnicodeChar == '\r' || key.UnicodeChar == '\n') {
        if (mCursor < static_cast<int>(mCatCount)) {
            // Run All <Category>: gather the category's benchmarks for the picker.
            const char* catName = BenchmarkRegistry::GetCategoryName(static_cast<UINT32>(mCursor));
            IBenchmark** all = BenchmarkRegistry::GetAll();
            UINTN total = BenchmarkRegistry::Count();
            Tui::PendingRun& pr = tui.Pending();
            pr.count = 0;
            pr.category = catName;
            for (UINTN i = 0; i < total && pr.count < 32; ++i) {
                if (StrCmp(all[i]->GetCategory(), catName) != 0) continue;
                pr.indices[pr.count] = i;
                ThreadingMode tm = all[i]->GetThreadingMode();
                pr.modes[pr.count] = (tm == ThreadingMode::SingleOnly)
                                     ? RunMode::SingleCore : RunMode::MultiCore;
                ++pr.count;
            }
            if (pr.count) return NavPush(ScreenId::RunCountPicker);
            return NavStay();
        }
        int postIdx = mCursor - static_cast<int>(mCatCount);
        switch (postIdx) {
            case 0: return NavPush(ScreenId::BenchmarkSelection);
            case 1: return NavPush(ScreenId::Results);
            case 2: return NavPush(ScreenId::SystemInfo);
            case 3: return NavPush(ScreenId::ResolutionPicker);
            case 4: return NavPush(ScreenId::ThemePicker);
            case 5: return NavPush(ScreenId::CorePicker);
            case 6: return NavExit();  // Shutdown
        }
    }
    return NavStay();
}
