#pragma once
// TUI controller. Owns the header, footer, navigation stack, and the shared
// model (last results, the pending run request). Each individual screen lives
// in its own Screen subclass under Screens/; the controller swaps between them.

#include "UefiTypes.h"
#include "Freestanding.h"
#include "BenchmarkResult.h"
#include "IBenchmark.h"          // RunMode

#include "Screens/Screen.h"
#include "Screens/MainMenuScreen.h"
#include "Screens/BenchmarkSelectionScreen.h"
#include "Screens/RunCountPickerScreen.h"
#include "Screens/ResultsScreen.h"
#include "Screens/CategoryResultsScreen.h"
#include "Screens/SystemInfoScreen.h"
#include "Screens/AiSuitabilityScreen.h"
#include "Screens/SmbusDebugScreen.h"
#include "Screens/ResolutionPickerScreen.h"
#include "Screens/ThemePickerScreen.h"
#include "Screens/CorePickerScreen.h"

class Tui {
public:
    void Run();   // enters the navigation loop (returns when the user shuts down)

    // ── Shared model (accessed by screens) ───────────────────────
    // A run request handed from a picker screen to RunPending(). category != null
    // means "Run All <category>"; otherwise indices/modes describe the selection.
    struct PendingRun {
        UINTN       indices[32];
        RunMode     modes[32];
        UINTN       count             = 0;
        const char* category          = nullptr;
        UINTN       runs              = 1;
        bool        coreCycleAllCores = true;
    };

    Vector<BenchmarkResult>& LastResults()        { return mLastResults; }
    const char*              LastCategory() const  { return mLastCategory; }
    PendingRun&              Pending()             { return mPending; }

    // Runs the pending request via BenchmarkRunner (which owns the display during
    // the run) and stores the results. Called by RunCountPickerScreen.
    void RunPending();

private:
    // ── Chrome (controller-owned) ────────────────────────────────
    int  DrawHeader(const char* title, int startRow = 0);
    void DrawFooter(const char* text);

    // ── Navigation stack ─────────────────────────────────────────
    static constexpr int kMaxDepth = 8;
    Screen* ScreenById(ScreenId id);
    void    Push(Screen* s);
    void    Pop();
    Screen* Top();

    Screen* mStack[kMaxDepth] = {};
    int     mDepth = 0;

    // ── Shared model storage ─────────────────────────────────────
    Vector<BenchmarkResult> mLastResults;
    const char*             mLastCategory = nullptr;
    PendingRun              mPending;

    // ── Screen instances (static lifetime; pushed by pointer) ────
    MainMenuScreen           mMainMenu;
    BenchmarkSelectionScreen mBenchSelect;
    RunCountPickerScreen     mRunCount;
    ResultsScreen            mResults;
    CategoryResultsScreen    mCatResults;
    SystemInfoScreen         mSysInfo;
    AiSuitabilityScreen      mAiSuit;
    SmbusDebugScreen         mSmbus;
    ResolutionPickerScreen   mResPicker;
    ThemePickerScreen        mThemePicker;
    CorePickerScreen         mCorePicker;
};
