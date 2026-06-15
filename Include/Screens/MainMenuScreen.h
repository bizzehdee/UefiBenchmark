#pragma once
#include "Screens/Screen.h"

// Top-level menu: dynamic "Run All <Category>" entries followed by the fixed
// actions (select, results, system info, pickers, shutdown).
class MainMenuScreen : public Screen {
public:
    const char* Title()  const override { return "UEFI BENCHMARK SUITE"; }
    const char* Footer() const override { return "[Up/Down] Navigate  [Enter] Select"; }

    // No OnEnter: the cursor persists across visits to submenus, matching the
    // original single long-lived main-menu loop.
    void      Draw(Tui& tui, int top, int bottom) override;
    NavResult HandleKey(Tui& tui, EFI_INPUT_KEY key) override;

private:
    static constexpr int kPostCount = 7;

    int    mCursor    = 0;
    UINT32 mCatCount  = 0;   // dynamic category entries (recomputed each Draw)
    int    mTotalOpts = 0;
    char   mCatLabels[8][48];
};
