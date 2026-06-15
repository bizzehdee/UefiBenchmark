#pragma once
#include "Screens/Screen.h"
#include "ScrollViewport.h"

// Scrollable system information: CPU, cache, memory, display, and the registered
// benchmark list. [A] opens AI suitability, [D] opens SMBus debug.
class SystemInfoScreen : public Screen {
public:
    const char* Title()  const override { return "System Information"; }
    const char* Footer() const override {
        return "[Up/Dn/PgUp/PgDn] Scroll  [A] AI Suitability  [Esc] Back";
    }

    void      OnEnter(Tui& tui) override;
    void      Draw(Tui& tui, int top, int bottom) override;
    NavResult HandleKey(Tui& tui, EFI_INPUT_KEY key) override;

private:
    ScrollViewport mVp;
    int mViewRows = 4;
};
