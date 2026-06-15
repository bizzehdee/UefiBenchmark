#pragma once
#include "Screens/Screen.h"

// Asks how many times to run, and (when any selection is core-cycle) the core
// scope. On confirm it runs the pending request and shows the results.
class RunCountPickerScreen : public Screen {
public:
    const char* Title()  const override { return "Set Run Count"; }
    const char* Footer() const override { return mFooter; }

    void      OnEnter(Tui& tui) override;
    void      Draw(Tui& tui, int top, int bottom) override;
    NavResult HandleKey(Tui& tui, EFI_INPUT_KEY key) override;

private:
    int  mRuns              = 1;
    bool mCoreCycleAllCores = true;
    bool mHasCoreCycle      = false;
    int  mCursor            = 0;
    int  mPickerRows        = 1;
    char mFooter[48]        = {};
};
