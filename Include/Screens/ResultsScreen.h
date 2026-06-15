#pragma once
#include "Screens/Screen.h"
#include "ScrollViewport.h"

// Detailed results table: one summary row per benchmark plus per-core sub-rows
// for core-cycle runs, with trailing totals.
class ResultsScreen : public Screen {
public:
    const char* Title()  const override { return "Benchmark Results"; }
    const char* Footer() const override {
        return mEmpty ? "[Any key] Back"
                      : "[Up/Dn/PgUp/PgDn/Home/End] Scroll  [Esc] Back";
    }

    void      OnEnter(Tui& tui) override;
    void      Draw(Tui& tui, int top, int bottom) override;
    NavResult HandleKey(Tui& tui, EFI_INPUT_KEY key) override;

private:
    ScrollViewport mVp;
    bool mEmpty    = false;
    int  mViewRows = 4;
};
