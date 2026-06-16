#include "Screens/ResultsScreen.h"
#include "Screens/UiHelpers.h"
#include "Tui.h"
#include "Renderer.h"
#include "ColorTheme.h"
#include "BenchmarkResult.h"
#include "CoreSelection.h"
#include "Statistics.h"
#include "Timer.h"
#include "Freestanding.h"

// Flat display list: each entry is (resultIdx, coreIdx). coreIdx == -1 → summary
// row; coreIdx >= 0 → per-core sub-row. File-scope POD array (no init guard).
namespace {
// coreIdx >= 0 -> per-core sub-row; sweepIdx >= 0 -> sweep sub-row; both -1 -> summary.
struct DisplayRow { int resIdx; int coreIdx; int sweepIdx; };
DisplayRow sFlat[32 + 32 * MAX_CYCLE_CORES + 32 * (int)MAX_SWEEP_POINTS];

// Write text into a fixed-width field within a line buffer.
void PadAt(char* buf, int col, const char* text, int w) {
    int len = text ? static_cast<int>(StrLen(text)) : 0;
    for (int i = 0; i < w && col + i < ScrollViewport::MAX_WIDTH - 1; ++i)
        buf[col + i] = (i < len) ? text[i] : ' ';
}
}  // namespace

void ResultsScreen::OnEnter(Tui& tui) {
    Vector<BenchmarkResult>& results = tui.LastResults();
    mEmpty = results.Empty();
    mVp.Clear();
    if (mEmpty) return;

    const int kFlatCap = 32 + 32 * (int)MAX_CYCLE_CORES + 32 * (int)MAX_SWEEP_POINTS;
    int flatCount = 0;
    for (int ri = 0; ri < static_cast<int>(results.Size()); ++ri) {
        sFlat[flatCount++] = { ri, -1, -1 };
        auto& r = results[static_cast<UINTN>(ri)];
        if (r.RunModeUsed == RunMode::CoreCycle) {
            for (UINT32 c = 0; c < r.PerCoreSampleCount && flatCount < kFlatCap; ++c)
                sFlat[flatCount++] = { ri, static_cast<int>(c), -1 };
        }
        for (UINT32 s = 0; s < r.SweepCount && flatCount < kFlatCap; ++s)
            sFlat[flatCount++] = { ri, -1, static_cast<int>(s) };
    }

    for (int i = 0; i < flatCount; ++i) {
        auto& dr = sFlat[i];
        auto& r  = results[static_cast<UINTN>(dr.resIdx)];

        char line[ScrollViewport::MAX_WIDTH];
        for (int k = 0; k < ScrollViewport::MAX_WIDTH - 1; ++k) line[k] = ' ';
        line[ScrollViewport::MAX_WIDTH - 1] = '\0';

        Color lineColor = Theme::Current().Text;

        if (dr.sweepIdx >= 0) {
            // ── Working-set sweep sub-row (e.g. L3 Cache Cliff) ──
            UINT32 s = static_cast<UINT32>(dr.sweepIdx);
            char lbl[24];
            int p = 0;
            lbl[p++] = ' '; lbl[p++] = ' '; lbl[p++] = '+'; lbl[p++] = '-'; lbl[p++] = ' ';
            const char* mb = UintToStr(r.SweepSizeMB[s]);
            for (int j = 0; mb[j] && p < 18; ++j) lbl[p++] = mb[j];
            lbl[p++] = ' '; lbl[p++] = 'M'; lbl[p++] = 'B'; lbl[p] = '\0';
            PadAt(line,  2, lbl, 22);
            PadAt(line, 73, UintToStr(r.SweepValue[s]), 11);
            PadAt(line, 84, "ns/acc", 9);
            // Highlight the 64 MB working set — the 5800X-vs-5800X3D discriminator.
            lineColor = (r.SweepSizeMB[s] == 64)
                            ? Theme::Current().Accent : Theme::Current().TextDim;
        } else if (dr.coreIdx < 0) {
            // ── Summary row ──
            PadAt(line,  2, r.Name,     22);
            PadAt(line, 24, r.Category,  6);

            char coreStr[16];
            if (r.RunModeUsed == RunMode::CoreCycle) {
                int p = 0;
                coreStr[p++] = 'C'; coreStr[p++] = 'C';
                const char* n = UintToStr(r.CoreCount);
                for (int j = 0; n[j] && p < 14; ++j) coreStr[p++] = n[j];
                coreStr[p] = '\0';
            } else if (r.MultiCore) {
                int p = 0;
                const char* n = UintToStr(r.CoreCount);
                for (int j = 0; n[j] && p < 10; ++j) coreStr[p++] = n[j];
                coreStr[p++] = 'x'; coreStr[p] = '\0';
            } else {
                coreStr[0] = '1'; coreStr[1] = '\0';
            }
            PadAt(line, 30, coreStr, 7);

            UINT64 avg = Stats::GetAverage(r.RunTimesUs);
            UINT64 mn  = Stats::GetMin(r.RunTimesUs);
            UINT64 mx  = Stats::GetMax(r.RunTimesUs);
            PadAt(line, 37, UintToStr(avg), 12);
            PadAt(line, 49, UintToStr(mn),  12);
            PadAt(line, 61, UintToStr(mx),  12);

            if (r.ErrorCount > 0) {
                PadAt(line, 73, "ERR", 11);
                lineColor = Theme::Current().Error;
            } else if (r.Score > 0) {
                PadAt(line, 73, UintToStr(r.Score), 11);
                PadAt(line, 84, r.Unit,              9);
                lineColor = Theme::Current().Success;
            } else if (r.Note) {
                // Recoverable failure (OOM, unsupported feature, no RAM buffer):
                // report the reason instead of a bare "---" so it isn't mistaken
                // for a legitimate zero score.
                PadAt(line, 73, "SKIP", 5);
                PadAt(line, 78, r.Note, ScrollViewport::MAX_WIDTH - 1 - 78);
                lineColor = Theme::Current().Warning;
            } else {
                PadAt(line, 73, "---", 11);
                lineColor = Theme::Current().TextDim;
            }

            // Which ISA path the kernel took (fallback-capable tests only).
            if (r.IsaPath) PadAt(line, 94, r.IsaPath, 14);
        } else {
            // ── Per-core sub-row ──
            int c = dr.coreIdx;

            char label[32]; label[0] = '\0';
            {
                CoreSelection::ApInfo* roster = CoreSelection::GetAll();
                UINT32 total = CoreSelection::Count();
                UINT32 apIdx = r.PerCoreApIndex[static_cast<UINT32>(c)];
                int p = 0;
                label[p++] = ' '; label[p++] = ' ';
                label[p++] = '+'; label[p++] = '-';
                label[p++] = 'A'; label[p++] = 'P';
                const char* as = UintToStr(apIdx);
                for (int j = 0; as[j] && p < 8; ++j) label[p++] = as[j];
                for (UINT32 ri2 = 0; ri2 < total; ++ri2) {
                    if (roster[ri2].ProcIndex == apIdx) {
                        label[p++] = ' ';
                        label[p++] = 'P'; label[p++] = '0'; label[p++] = '+';
                        label[p++] = 'C';
                        const char* cs2 = UintToStr(roster[ri2].Core);
                        for (int j = 0; cs2[j] && p < 28; ++j) label[p++] = cs2[j];
                        label[p++] = 'T';
                        const char* ts = UintToStr(roster[ri2].Thread);
                        for (int j = 0; ts[j] && p < 30; ++j) label[p++] = ts[j];
                        break;
                    }
                }
                label[p] = '\0';
            }
            PadAt(line, 2, label, 22);

            bool outlier = false;
            if (r.Score > 0) {
                UINT64 sc  = r.PerCoreScore[static_cast<UINT32>(c)];
                UINT64 dev = (sc > r.Score) ? sc - r.Score : r.Score - sc;
                outlier    = (dev * 100 / r.Score) > 5;
            }
            lineColor = outlier ? Theme::Current().Warning : Theme::Current().TextDim;

            UINT64 sc = r.PerCoreScore[static_cast<UINT32>(c)];
            UINT64 mn = r.PerCoreMin[static_cast<UINT32>(c)];
            UINT64 mx = r.PerCoreMax[static_cast<UINT32>(c)];
            if (sc > 0 || mn > 0 || mx > 0) {
                PadAt(line, 73, UintToStr(sc), 11);
                PadAt(line, 49, UintToStr(mn), 12);
                PadAt(line, 61, UintToStr(mx), 12);
                PadAt(line, 84, r.Unit,         9);
            } else {
                PadAt(line, 37, UintToStr(r.PerCoreTimeUs[static_cast<UINT32>(c)]), 12);
                PadAt(line, 84, "us", 9);
            }
        }

        mVp.AddLine(line, lineColor);
    }

    // Trailing summary lines.
    mVp.AddSeparator();

    // Configured time-box duration(s) that were used (test vs stress).
    UINT64 testBudget = 0, stressBudget = 0;
    for (UINTN i = 0; i < results.Size(); ++i) {
        if (StrCmp(results[i].Category, "Stress") == 0) {
            if (!stressBudget) stressBudget = results[i].BudgetUs;
        } else if (!testBudget) {
            testBudget = results[i].BudgetUs;
        }
    }
    if (testBudget)
        mVp.AddLine(Ui::Concat2("  Test duration:   ", Ui::DurationStr(testBudget)),
                    Theme::Current().TextDim);
    if (stressBudget)
        mVp.AddLine(Ui::Concat2("  Stress duration: ", Ui::DurationStr(stressBudget)),
                    Theme::Current().TextDim);

    UINT64 totalTime = 0;
    for (UINTN i = 0; i < results.Size(); ++i) totalTime += results[i].TotalTimeUs;
    mVp.AddLine(Ui::Concat3("  Total suite time: ", UintToStr(totalTime / 1000), " ms"),
                Theme::Current().Text);

    UINT64 totalErrors = 0;
    for (UINTN i = 0; i < results.Size(); ++i) totalErrors += results[i].ErrorCount;
    if (totalErrors > 0)
        mVp.AddLine(
            Ui::Concat3("  !! RAM ERRORS DETECTED: ", UintToStr(totalErrors), " mismatches !!"),
            Theme::Current().Error);

    // Tests skipped (no usable result) carry a reason in their SKIP row above;
    // summarise the count here so it's obvious at a glance.
    UINT64 skipped = 0;
    for (UINTN i = 0; i < results.Size(); ++i)
        if (results[i].Note && results[i].Score == 0 && results[i].ErrorCount == 0)
            ++skipped;
    if (skipped > 0)
        mVp.AddLine(
            Ui::Concat3("  ", UintToStr(skipped), " test(s) skipped - see the SKIP rows above for the reason"),
            Theme::Current().Warning);

    // Machine-check events caught by MCA polling during the run. Uncorrected
    // errors (the CPU could not fix them but survived) are serious; corrected
    // errors mean the hardware auto-recovered (ECC scrub / cache parity).
    UINT32 mcCorr = 0, mcUnc = 0;
    for (UINTN i = 0; i < results.Size(); ++i) {
        mcUnc  += results[i].McUncorrected;
        mcCorr += results[i].McCorrected;
    }
    if (mcUnc > 0)
        mVp.AddLine(
            Ui::Concat3("  !! UNCORRECTED MACHINE CHECKS: ", UintToStr(mcUnc), " events !!"),
            Theme::Current().Error);
    if (mcCorr > 0)
        mVp.AddLine(
            Ui::Concat3("  Corrected machine-check events: ", UintToStr(mcCorr),
                        " (hardware auto-recovered)"),
            Theme::Current().Warning);

    if (Timer::IsCalibrated()) {
        mVp.AddLine(
            Ui::Concat3("  TSC: ", UintToStr(Timer::CyclesPerUs()), " cycles/us"),
            Theme::Current().TextDim);
        if (!Timer::HasInvariantTSC())
            mVp.AddLine(
                "  Warning: Invariant TSC not detected; timing may be imprecise",
                Theme::Current().Warning);
    }
}

void ResultsScreen::Draw(Tui& /*tui*/, int top, int /*bottom*/) {
    if (mEmpty) {
        Renderer::DrawText(2, 5, "No results available. Run benchmarks first.",
                           Theme::Current().Warning);
        return;
    }

    int contentStart = top + 3;
    int footerRows   = 3;
    mViewRows = static_cast<int>(Renderer::Rows()) - contentStart - footerRows;
    if (mViewRows < 4) mViewRows = 4;

    int hdr = top + 1;  // column headers
    Renderer::DrawText(2,  hdr, Renderer::Pad("Benchmark", 22), Theme::Current().Accent);
    Renderer::DrawText(24, hdr, Renderer::Pad("Cat",        6), Theme::Current().Accent);
    Renderer::DrawText(30, hdr, Renderer::Pad("Cores",      7), Theme::Current().Accent);
    Renderer::DrawText(37, hdr, Renderer::Pad("Avg(us)",   12), Theme::Current().Accent);
    Renderer::DrawText(49, hdr, Renderer::Pad("Min(us)",   12), Theme::Current().Accent);
    Renderer::DrawText(61, hdr, Renderer::Pad("Max(us)",   12), Theme::Current().Accent);
    Renderer::DrawText(73, hdr, Renderer::Pad("Score",     11), Theme::Current().Accent);
    Renderer::DrawText(84, hdr, Renderer::Pad("Unit",       9), Theme::Current().Accent);
    Renderer::DrawText(94, hdr, Renderer::Pad("ISA",       14), Theme::Current().Accent);
    Ui::DrawSeparator(top + 2);

    mVp.Render(contentStart, mViewRows);
}

NavResult ResultsScreen::HandleKey(Tui& /*tui*/, EFI_INPUT_KEY key) {
    if (mEmpty) return NavBack();
    if (key.ScanCode == SCAN_ESC) return NavBack();
    mVp.HandleKey(key, mViewRows);
    return NavStay();
}
