#pragma once
#include "Screens/Screen.h"
#include "ScrollViewport.h"
#include "IBenchmark.h"   // RunMode

// Scrollable benchmark picker with per-benchmark threading-mode toggles.
class BenchmarkSelectionScreen : public Screen {
public:
    const char* Title()  const override { return "Select Benchmarks"; }
    const char* Footer() const override {
        return mEmpty ? "[Any key] Back"
                      : "[Up/Dn] Move  [Space] Toggle  [L/R] Mode  [Enter] Run  [Esc] Back";
    }

    void      OnEnter(Tui& tui) override;
    void      Draw(Tui& tui, int top, int bottom) override;
    NavResult HandleKey(Tui& tui, EFI_INPUT_KEY key) override;

private:
    ScrollViewport mVp;
    bool    mEmpty    = false;
    bool    mMpAvail  = false;
    UINT32  mApCount  = 0;
    UINTN   mBmCount  = 0;
    int     mCursor   = 0;
    bool    mSelected[32] = {};
    RunMode mModes[32]    = {};
};
