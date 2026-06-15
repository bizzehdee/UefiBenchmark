#pragma once
#include "Screens/Screen.h"
#include "ScrollViewport.h"

// AP selection list with BSP-inclusion toggle and quick-select shortcuts.
class CorePickerScreen : public Screen {
public:
    const char* Title()  const override { return "Select Cores"; }
    const char* Footer() const override {
        return mEmpty ? "[Any key] Back"
                      : "[Up/Dn] Move  [Space] Toggle  [A]ll  [P]hysical  [1]PerPkg  [B]SP  [Esc] Done";
    }

    void      OnEnter(Tui& tui) override;
    void      Draw(Tui& tui, int top, int bottom) override;
    NavResult HandleKey(Tui& tui, EFI_INPUT_KEY key) override;

private:
    ScrollViewport mVp;
    bool   mEmpty   = false;
    int    mCursor  = 0;
    UINT32 mApCount = 0;
    int    mViewRows = 4;
};
