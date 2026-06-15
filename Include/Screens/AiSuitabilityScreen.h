#pragma once
#include "Screens/Screen.h"
#include "ScrollViewport.h"

// CPU AI-readiness matrix: feature checklist, tier assessment, and LLM
// throughput estimates derived from the last AI benchmark run (if any).
class AiSuitabilityScreen : public Screen {
public:
    const char* Title()  const override { return "AI Suitability Matrix"; }
    const char* Footer() const override { return "[Up/Dn/PgUp/PgDn] Scroll  [Esc] Back"; }

    void      OnEnter(Tui& tui) override;
    void      Draw(Tui& tui, int top, int bottom) override;
    NavResult HandleKey(Tui& tui, EFI_INPUT_KEY key) override;

private:
    ScrollViewport mVp;
    int mViewRows = 4;
};
