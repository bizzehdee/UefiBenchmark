#pragma once
#include "Screens/Screen.h"
#include "ScrollViewport.h"

// Summary table for a single category run, with a weighted composite score and
// (for the AI category) LLM throughput estimates. Enter opens the full table.
class CategoryResultsScreen : public Screen {
public:
    const char* Title()  const override { return mTitle; }
    const char* Footer() const override {
        return mEmpty ? "[Any key] Back"
                      : "[Up/Dn/PgUp/PgDn] Scroll  [Enter] Detailed View  [Esc] Back";
    }

    void      OnEnter(Tui& tui) override;
    void      Draw(Tui& tui, int top, int bottom) override;
    NavResult HandleKey(Tui& tui, EFI_INPUT_KEY key) override;

private:
    ScrollViewport mVp;
    char mTitle[80] = {};
    bool mEmpty     = false;
    int  mViewRows  = 4;
};
