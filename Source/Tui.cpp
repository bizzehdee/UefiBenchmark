// TUI controller: owns the header/footer chrome, the navigation stack, and the
// shared model. Individual screens live under Source/Screens/ and are swapped
// in/out by the navigation loop below.

#include "Tui.h"
#include "Renderer.h"
#include "ColorTheme.h"
#include "BenchmarkRunner.h"
#include "Screens/UiHelpers.h"

// ── Chrome (controller-owned) ─────────────────────────────────

int Tui::DrawHeader(const char* title, int startRow) {
    int cols = static_cast<int>(Renderer::Columns());

    char border[128];
    int bLen = cols < 127 ? cols : 127;
    for (int i = 0; i < bLen; ++i) border[i] = '=';
    border[bLen] = '\0';

    Renderer::DrawText(0, startRow, border, Theme::Current().HeaderBorder);

    int pad = (cols - static_cast<int>(StrLen(title))) / 2;
    char line[128];
    int p = 0;
    for (int i = 0; i < pad && p < 126; ++i) line[p++] = ' ';
    for (int i = 0; title[i] && p < 126; ++i) line[p++] = title[i];
    while (p < bLen) line[p++] = ' ';
    line[p] = '\0';
    Renderer::DrawText(0, startRow + 1, line, Theme::Current().HeaderText);

    Renderer::DrawText(0, startRow + 2, border, Theme::Current().HeaderBorder);
    return startRow + 3;
}

void Tui::DrawFooter(const char* text) {
    int rows = static_cast<int>(Renderer::Rows());
    int cols = static_cast<int>(Renderer::Columns());
    Ui::DrawSeparator(rows - 3);
    Renderer::DrawTextBg(0, rows - 2,
        Renderer::Pad("(c) 2026 Darren Horrocks | https://github.com/bizzehdee/UefiBenchmark | MIT License", cols),
        Theme::Current().TextDim, Theme::Current().Background);
    Renderer::DrawTextBg(0, rows - 1, Renderer::Pad(text, cols),
                         Theme::Current().Footer, Theme::Current().Background);
}

// ── Navigation stack ──────────────────────────────────────────

Screen* Tui::ScreenById(ScreenId id) {
    switch (id) {
        case ScreenId::MainMenu:           return &mMainMenu;
        case ScreenId::BenchmarkSelection: return &mBenchSelect;
        case ScreenId::RunCountPicker:     return &mRunCount;
        case ScreenId::Results:            return &mResults;
        case ScreenId::CategoryResults:    return &mCatResults;
        case ScreenId::SystemInfo:         return &mSysInfo;
        case ScreenId::AiSuitability:      return &mAiSuit;
        case ScreenId::SmbusDebug:         return &mSmbus;
        case ScreenId::ResolutionPicker:   return &mResPicker;
        case ScreenId::ThemePicker:        return &mThemePicker;
        case ScreenId::CorePicker:         return &mCorePicker;
    }
    return &mMainMenu;
}

void    Tui::Push(Screen* s) { if (s && mDepth < kMaxDepth) mStack[mDepth++] = s; }
void    Tui::Pop()           { if (mDepth > 0) --mDepth; }
Screen* Tui::Top()           { return mDepth > 0 ? mStack[mDepth - 1] : nullptr; }

// ── Run dispatch (model side) ─────────────────────────────────

void Tui::RunPending() {
    if (mPending.category) {
        mLastCategory = mPending.category;
        mLastResults  = BenchmarkRunner::RunCategory(mPending.category, mPending.runs);
    } else {
        mLastCategory = nullptr;
        mLastResults  = BenchmarkRunner::RunSelected(mPending.indices, mPending.modes,
                            mPending.count, mPending.runs, mPending.coreCycleAllCores);
    }
}

// ── Navigation loop ───────────────────────────────────────────

void Tui::Run() {
    Push(&mMainMenu);
    bool entering = true;

    while (mDepth > 0) {
        Screen* s = Top();
        if (entering) {
            s->OnEnter(*this);
            Renderer::FlushInput();
            entering = false;
        }

        // Frame: header (controller) → body (screen) → footer (controller).
        Renderer::Clear();
        int top    = DrawHeader(s->Title());
        int bottom = static_cast<int>(Renderer::Rows()) - 3;  // first footer row
        s->Draw(*this, top, bottom);
        DrawFooter(s->Footer());
        Renderer::Present();

        // Drain buffered keys, dispatching each to the active screen, stopping
        // at the first key that produces a navigation action.
        EFI_INPUT_KEY key = Renderer::WaitKey();
        NavResult r = NavStay();
        bool acted = false;
        do {
            r = s->HandleKey(*this, key);
            if (r.op != NavOp::Stay) { acted = true; break; }
        } while (Renderer::PollKey(&key));

        if (!acted) continue;  // redraw with updated state
        switch (r.op) {
            case NavOp::Stay:                                                  break;
            case NavOp::Push:    Push(ScreenById(r.target));   entering = true; break;
            case NavOp::Replace: Pop(); Push(ScreenById(r.target)); entering = true; break;
            case NavOp::Back:    Pop();                         entering = true; break;
            case NavOp::Exit:    mDepth = 0;                                   break;
        }
    }
}
