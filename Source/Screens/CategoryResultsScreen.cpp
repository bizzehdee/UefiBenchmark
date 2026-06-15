#include "Screens/CategoryResultsScreen.h"
#include "Screens/UiHelpers.h"
#include "Tui.h"
#include "Renderer.h"
#include "ColorTheme.h"
#include "BenchmarkResult.h"
#include "Statistics.h"
#include "AiScore.h"
#include "Freestanding.h"

namespace {
// Fill buf[col..col+w-1] with text padded by spaces.
void PadAt(char* buf, int col, const char* text, int w) {
    int len = text ? (int)StrLen(text) : 0;
    for (int i = 0; i < w && col + i < ScrollViewport::MAX_WIDTH - 1; ++i)
        buf[col + i] = (i < len) ? text[i] : ' ';
}
// Format X.Y tok/s into out.
void FmtTok(char* out, UINT64 score, UINT32 refX10) {
    UINT64 t = score * refX10 / 1000;
    int p = 0;
    const char* n = UintToStr(t / 10);
    for (int i = 0; n[i]; ++i) out[p++] = n[i];
    out[p++] = '.'; out[p++] = '0' + (char)(t % 10);
    for (const char* s = " t/s"; *s; ++s) out[p++] = *s;
    out[p] = '\0';
}
}  // namespace

void CategoryResultsScreen::OnEnter(Tui& tui) {
    Vector<BenchmarkResult>& results = tui.LastResults();
    const char* category = tui.LastCategory();
    mEmpty = results.Empty() || category == nullptr;
    mVp.Clear();
    if (mEmpty) { mTitle[0] = '\0'; return; }

    // Header title: "<Category> BENCHMARK RESULTS".
    {
        int p = 0;
        for (const char* s = category; *s && p < 50; ++s) mTitle[p++] = *s;
        for (const char* s = " BENCHMARK RESULTS"; *s && p < 78; ++s) mTitle[p++] = *s;
        mTitle[p] = '\0';
    }

    UINT64 weightedSum = 0, totalWeight = 0;
    bool sameUnit = true;
    const char* firstUnit = nullptr;

    for (UINTN i = 0; i < results.Size(); ++i) {
        auto& r = results[i];
        if (StrCmp(r.Category, category) != 0) continue;

        char row[ScrollViewport::MAX_WIDTH];
        for (int k = 0; k < ScrollViewport::MAX_WIDTH - 1; ++k) row[k] = ' ';
        row[ScrollViewport::MAX_WIDTH - 1] = '\0';

        PadAt(row,  2, r.Name, 36);

        Color lineColor = Theme::Current().Text;
        if (r.ErrorCount > 0) {
            static char errStr[32]; int p = 0;
            for (const char* s = "ERRORS: "; *s; ++s) errStr[p++] = *s;
            const char* ns = UintToStr(r.ErrorCount);
            for (int j = 0; ns[j]; ++j) errStr[p++] = ns[j];
            errStr[p] = '\0';
            PadAt(row, 38, errStr, 14);
            lineColor = Theme::Current().Error;
        } else if (r.Score > 0) {
            PadAt(row, 38, UintToStr(r.Score), 14);
            PadAt(row, 52, r.Unit,             12);
            if (r.IncludeInScore) {
                if (!firstUnit) firstUnit = r.Unit;
                else if (StrCmp(firstUnit, r.Unit) != 0) sameUnit = false;
                weightedSum += r.Score * r.CategoryWeight;
                totalWeight += r.CategoryWeight;
            }
            lineColor = Theme::Current().Success;
        } else if (!r.IncludeInScore) {
            PadAt(row, 38, "--",          14);
            PadAt(row, 52, "[pass/fail]", 12);
        } else {
            PadAt(row, 38, "--", 14);
        }

        UINT64 avgUs = Stats::GetAverage(r.RunTimesUs);
        PadAt(row, 64, UintToStr(avgUs / 1000), 12);

        mVp.AddLine(row, lineColor);
    }

    mVp.AddSeparator();

    UINT64 composite = (totalWeight > 0) ? weightedSum / totalWeight : 0;
    if (composite > 0) {
        char compRow[ScrollViewport::MAX_WIDTH];
        for (int k = 0; k < ScrollViewport::MAX_WIDTH - 1; ++k) compRow[k] = ' ';
        compRow[ScrollViewport::MAX_WIDTH - 1] = '\0';
        const char* compLabel = (totalWeight != (UINT64)100 * (totalWeight / 100))
                                ? "Weighted Score:" : "Composite Score:";
        PadAt(compRow,  2, compLabel,               36);
        PadAt(compRow, 38, UintToStr(composite),    14);
        if (sameUnit && firstUnit)
            PadAt(compRow, 52, firstUnit,            12);
        else
            PadAt(compRow, 52, "(mixed units)",      12);
        mVp.AddLine(compRow, Theme::Current().Accent);

        if (StrCmp(category, "AI") == 0) {
            mVp.AddLine();
            mVp.AddLine("  LLM Performance Estimate (llama.cpp, approx.):",
                        Theme::Current().Accent);

            static char t7[24], t14[24], t32[24];
            FmtTok(t7,  composite, AI_LLM_7B_Q4_TOKS_X10);
            FmtTok(t14, composite, AI_LLM_14B_Q4_TOKS_X10);
            FmtTok(t32, composite, AI_LLM_32B_Q4_TOKS_X10);

            auto AddLlm = [&](const char* model, const char* toks) {
                char lr[ScrollViewport::MAX_WIDTH];
                for (int k = 0; k < ScrollViewport::MAX_WIDTH - 1; ++k) lr[k] = ' ';
                lr[ScrollViewport::MAX_WIDTH - 1] = '\0';
                PadAt(lr,  4, model, 16); PadAt(lr, 20, toks, 20);
                mVp.AddLine(lr, Theme::Current().Success);
            };
            AddLlm("7B Q4:",  t7);
            AddLlm("14B Q4:", t14);
            AddLlm("32B Q4:", t32);
        }
    }

    mVp.AddLine();
    UINT64 totalUs = 0;
    for (UINTN i = 0; i < results.Size(); ++i) totalUs += results[i].TotalTimeUs;
    mVp.AddLine(Ui::Concat3("  Total suite time: ", UintToStr(totalUs / 1000), " ms"),
                Theme::Current().Text);
}

void CategoryResultsScreen::Draw(Tui& /*tui*/, int top, int /*bottom*/) {
    if (mEmpty) {
        Renderer::DrawText(2, 5, "No results available.", Theme::Current().Warning);
        return;
    }

    int contentStart = top + 3;
    int footerRows   = 3;
    mViewRows = static_cast<int>(Renderer::Rows()) - contentStart - footerRows;
    if (mViewRows < 4) mViewRows = 4;

    int hdr = top + 1;
    Renderer::DrawText(2,  hdr, Renderer::Pad("Benchmark", 36), Theme::Current().Accent);
    Renderer::DrawText(38, hdr, Renderer::Pad("Score",     14), Theme::Current().Accent);
    Renderer::DrawText(52, hdr, Renderer::Pad("Unit",      12), Theme::Current().Accent);
    Renderer::DrawText(64, hdr, "Avg time (ms)",                Theme::Current().Accent);
    Ui::DrawSeparator(top + 2);

    mVp.Render(contentStart, mViewRows);
}

NavResult CategoryResultsScreen::HandleKey(Tui& /*tui*/, EFI_INPUT_KEY key) {
    if (mEmpty) return NavBack();
    if (key.ScanCode == SCAN_ESC) return NavBack();
    if (key.UnicodeChar == '\r' || key.UnicodeChar == '\n')
        return NavPush(ScreenId::Results);
    mVp.HandleKey(key, mViewRows);
    return NavStay();
}
