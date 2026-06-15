#include "Screens/AiSuitabilityScreen.h"
#include "Tui.h"
#include "Renderer.h"
#include "ColorTheme.h"
#include "CpuFeatures.h"
#include "AiSuitability.h"
#include "AiScore.h"
#include "BenchmarkResult.h"
#include "Freestanding.h"

namespace {
// Word-wrap `text` into the viewport with `indent` leading spaces.
void VpAddWrapped(ScrollViewport& vp, const char* text, int indent, Color color) {
    int cols = (int)Renderer::Columns() - indent - 1;
    if (cols < 20) cols = 20;
    char buf[ScrollViewport::MAX_WIDTH];
    const char* p = text;
    while (*p) {
        const char* lineStart = p;
        const char* brk       = nullptr;
        int         count     = 0;
        while (*p && count < cols) {
            if (*p == ' ') brk = p;
            ++p; ++count;
        }
        if (*p && brk && brk > lineStart)
            p = brk + 1;
        int out = 0;
        for (int i = 0; i < indent && out < ScrollViewport::MAX_WIDTH - 1; ++i)
            buf[out++] = ' ';
        for (const char* q = lineStart; q < p && out < ScrollViewport::MAX_WIDTH - 1; ++q)
            if (*q != '\n') buf[out++] = *q;
        buf[out] = '\0';
        vp.AddLine(buf, color);
        while (*p == ' ') ++p;
    }
}
}  // namespace

void AiSuitabilityScreen::OnEnter(Tui& tui) {
    ScrollViewport& vp = mVp;
    vp.Clear();

    const auto& f    = CpuFeatures::Get();
    AiSuitability::Tier tier = AiSuitability::Evaluate(f);
    const char* tierName     = AiSuitability::TierName(tier);

    Color tierColor;
    switch (tier) {
        case AiSuitability::Tier::Excellent: tierColor = Theme::Current().Accent;  break;
        case AiSuitability::Tier::VeryGood:  tierColor = Theme::Current().Accent;  break;
        case AiSuitability::Tier::Good:      tierColor = Theme::Current().Text;    break;
        default:                             tierColor = Theme::Current().Warning; break;
    }

    // Tier banner
    {
        char buf[64]; int p = 0;
        buf[p++] = ' '; buf[p++] = ' ';
        for (const char* s = "Overall Tier:  "; *s; ++s) buf[p++] = *s;
        for (const char* s = tierName; *s; ++s) buf[p++] = *s;
        buf[p] = '\0';
        vp.AddLine(buf, tierColor);
    }

    // Tier number indicator
    {
        char buf[80]; int p = 0;
        buf[p++] = ' '; buf[p++] = ' ';
        buf[p++] = 'T'; buf[p++] = 'i'; buf[p++] = 'e'; buf[p++] = 'r'; buf[p++] = ' ';
        buf[p++] = '1' + (char)(UINT32)tier;
        buf[p++] = '/'; buf[p++] = '4';
        for (const char* s = "  -  "; *s; ++s) buf[p++] = *s;
        const char* subLabel =
            tier == AiSuitability::Tier::Excellent ? "AVX-512F + AVX-512VNNI" :
            tier == AiSuitability::Tier::VeryGood  ? "AVX2 + FMA + AVX-VNNI"  :
            tier == AiSuitability::Tier::Good       ? "AVX2 + FMA (baseline)"   :
                                                     "No AVX2";
        for (const char* s = subLabel; *s; ++s) buf[p++] = *s;
        buf[p] = '\0';
        vp.AddLine(buf, Theme::Current().TextDim);
    }

    // Feature checklist helper
    auto AddFeature = [&](const char* name, const char* description, bool ok) {
        char buf[80]; int p = 0;
        buf[p++] = ' '; buf[p++] = ' ';
        buf[p++] = '[';
        if (ok) { buf[p++] = 'O'; buf[p++] = 'K'; }
        else    { buf[p++] = '-'; buf[p++] = '-'; }
        buf[p++] = ']'; buf[p++] = ' ';
        for (const char* s = name; *s && p < 22; ++s) buf[p++] = *s;
        while (p < 24) buf[p++] = ' ';
        for (const char* s = description; *s && p < 79; ++s) buf[p++] = *s;
        buf[p] = '\0';
        Color c = ok ? Theme::Current().Accent : Theme::Current().TextDim;
        vp.AddLine(buf, c);
    };

    // ── Required features ─────────────────────────────────────────
    vp.AddLine();
    vp.AddLine("  -- Required features --", Theme::Current().TextDim);
    AddFeature("SSE4.2",   "Required base for quantized inference",   f.HasSSE42);
    AddFeature("AVX",      "256-bit vector support",                  f.HasAVX);
    AddFeature("AVX2",     "8-bit integer SIMD (MADDUBS)",            f.HasAVX2);
    AddFeature("FMA",      "Fused multiply-add for FP32 attention",   f.HasFMA);
    AddFeature("XSAVE",    "OS AVX state save/restore",               f.HasXSave);

    // ── Accelerator features ──────────────────────────────────────
    vp.AddLine();
    vp.AddLine("  -- Accelerator features --", Theme::Current().TextDim);
    AddFeature("AVX-VNNI", "INT8 dot-product (Alder Lake+, Zen4+)",   f.HasAVXVNNI);
    AddFeature("AVX-512F", "512-bit vectors (server/Sapphire Rapids)", f.HasAVX512F);
    AddFeature("AVX512VNNI","Native INT8/INT4 inference kernel",       f.HasAVX512VNNI);
    AddFeature("AES-NI",   "Hardware AES acceleration",               f.HasAESNI);

    // ── Architecture assessment ───────────────────────────────────
    vp.AddLine();
    vp.AddLine("  -- Assessment --", Theme::Current().TextDim);
    VpAddWrapped(vp, AiSuitability::TierSummary(tier), 4, Theme::Current().Text);

    // ── LLM performance estimates ─────────────────────────────────
    vp.AddLine();
    vp.AddLine("  -- LLM Performance Estimates --", Theme::Current().TextDim);

    Vector<BenchmarkResult>& results = tui.LastResults();
    UINT64 weightedSum = 0, totalWeight = 0;
    UINTN  resultCount = results.Size();
    for (UINTN i = 0; i < resultCount; ++i) {
        const BenchmarkResult& r = results[i];
        if (StrCmp(r.Category, "AI") != 0 || !r.IncludeInScore) continue;
        weightedSum += r.Score * r.CategoryWeight;
        totalWeight += r.CategoryWeight;
    }

    auto FmtToks = [](char* buf, UINT64 score, UINT32 refX10) -> const char* {
        UINT64 t = score * refX10 / 1000;
        int p = 0;
        const char* n = UintToStr(t / 10);
        while (n[p]) { buf[p] = n[p]; ++p; }
        buf[p++] = '.';
        buf[p++] = '0' + (char)(t % 10);
        for (const char* s = " t/s"; *s; ++s) buf[p++] = *s;
        buf[p] = '\0';
        return buf;
    };

    if (totalWeight > 0) {
        UINT64 aiScore = weightedSum / totalWeight;
        char tok7[24], tok14[24], tok32[24];
        char infoLine[ScrollViewport::MAX_WIDTH];

        auto AddEst = [&](const char* model, const char* toks) {
            int p = 0;
            infoLine[p++] = ' '; infoLine[p++] = ' '; infoLine[p++] = ' '; infoLine[p++] = ' ';
            for (const char* s = model; *s && p < 26; ++s) infoLine[p++] = *s;
            while (p < 28) infoLine[p++] = ' ';
            for (const char* s = toks; *s && p < 79; ++s) infoLine[p++] = *s;
            infoLine[p] = '\0';
            vp.AddLine(infoLine, Theme::Current().Text);
        };

        FmtToks(tok7,  aiScore, AI_LLM_7B_Q4_TOKS_X10);
        FmtToks(tok14, aiScore, AI_LLM_14B_Q4_TOKS_X10);
        FmtToks(tok32, aiScore, AI_LLM_32B_Q4_TOKS_X10);

        char scoreLine[96]; int sp = 0;
        for (const char* s = "  AI Score: "; *s; ++s) scoreLine[sp++] = *s;
        const char* sv = UintToStr(aiScore);
        for (int i = 0; sv[i] && sp < 95; ++i) scoreLine[sp++] = sv[i];
        for (const char* s = " AI pts  (Ryzen 7 5800X baseline = 1000)"; *s && sp < 95; ++s)
            scoreLine[sp++] = *s;
        scoreLine[sp] = '\0';
        vp.AddLine(scoreLine, Theme::Current().Accent);

        AddEst("LLM  7B Q4:",  tok7);
        AddEst("LLM 14B Q4:",  tok14);
        AddEst("LLM 32B Q4:",  tok32);
        vp.AddLine("  (Estimates based on CPU-only inference; real performance varies)", Theme::Current().TextDim);
    } else {
        vp.AddLine("  Run the AI Benchmark Suite for personalized estimates.", Theme::Current().TextDim);
        vp.AddLine();
        vp.AddLine("  Reference (Ryzen 7 5800X = 1000 AI pts):", Theme::Current().TextDim);

        char refLine[64];
        auto AddRef = [&](const char* model, UINT32 refX10) {
            char tok[24]; int p = 0;
            UINT64 t = (UINT64)refX10;
            refLine[p++] = ' '; refLine[p++] = ' '; refLine[p++] = ' '; refLine[p++] = ' ';
            for (const char* s = model; *s && p < 26; ++s) refLine[p++] = *s;
            while (p < 28) refLine[p++] = ' ';
            (void)tok;
            refLine[p++] = '0' + (char)(t / 10);
            refLine[p++] = '.';
            refLine[p++] = '0' + (char)(t % 10);
            for (const char* s = " t/s  (reference)"; *s && p < 63; ++s) refLine[p++] = *s;
            refLine[p] = '\0';
            vp.AddLine(refLine, Theme::Current().TextDim);
        };
        AddRef("LLM  7B Q4:", AI_LLM_7B_Q4_TOKS_X10);
        AddRef("LLM 14B Q4:", AI_LLM_14B_Q4_TOKS_X10);
        AddRef("LLM 32B Q4:", AI_LLM_32B_Q4_TOKS_X10);
    }
}

void AiSuitabilityScreen::Draw(Tui& /*tui*/, int top, int /*bottom*/) {
    int renderStart = top + 1;
    int footerRows  = 1;
    mViewRows = static_cast<int>(Renderer::Rows()) - renderStart - footerRows;
    if (mViewRows < 1) mViewRows = 1;
    mVp.Render(renderStart, mViewRows);
}

NavResult AiSuitabilityScreen::HandleKey(Tui& /*tui*/, EFI_INPUT_KEY key) {
    if (key.ScanCode == SCAN_ESC) return NavBack();
    mVp.HandleKey(key, mViewRows);
    return NavStay();
}
