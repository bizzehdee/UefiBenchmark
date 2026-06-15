#pragma once
#include "Screens/Screen.h"
#include "Renderer.h"   // Renderer::ModeDesc

// Display-resolution selector. After applying a mode it shows a brief
// confirmation, then any key returns to the menu.
class ResolutionPickerScreen : public Screen {
public:
    const char* Title()  const override { return "Change Resolution"; }
    const char* Footer() const override {
        if (mEmpty)   return "[Any key] Back";
        if (mApplied) return "[Any key] Continue";
        return "[Up/Down] Navigate  [Enter] Apply  [Esc] Cancel";
    }

    void      OnEnter(Tui& tui) override;
    void      Draw(Tui& tui, int top, int bottom) override;
    NavResult HandleKey(Tui& tui, EFI_INPUT_KEY key) override;

private:
    Renderer::ModeDesc mModes[64];
    UINT32 mModeCount = 0;
    UINT32 mCurrent   = 0;
    int    mCursor    = 0;
    bool   mEmpty     = false;
    bool   mApplied   = false;
};
