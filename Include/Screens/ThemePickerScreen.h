#pragma once
#include "Screens/Screen.h"

// Colour-theme selector.
class ThemePickerScreen : public Screen {
public:
    const char* Title()  const override { return "Change Theme"; }
    const char* Footer() const override { return "[Up/Down] Navigate  [Enter] Apply  [Esc] Cancel"; }

    void      OnEnter(Tui& tui) override;
    void      Draw(Tui& tui, int top, int bottom) override;
    NavResult HandleKey(Tui& tui, EFI_INPUT_KEY key) override;

private:
    int mCursor = 0;
};
