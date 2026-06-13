// Benchmark runner with multi-run and multi-core (MP Services) support.

#include "BenchmarkRunner.h"
#include "BenchmarkRegistry.h"
#include "CoreSelection.h"
#include "Timer.h"
#include "Statistics.h"
#include "Renderer.h"
#include "SystemInfo.h"
#include "Freestanding.h"

// ── AP dispatch contexts ─────────────────────────────────────

struct ApContext {
    IBenchmark* Benchmark;
    UINT32      TotalWorkers;
    volatile UINT32 WorkerIndex;
};

extern "C" VOID EFIAPI ApBenchmarkProc(VOID* Buffer) {
    auto* ctx = static_cast<ApContext*>(Buffer);
    UINT32 myIndex = __atomic_fetch_add(&ctx->WorkerIndex, 1, __ATOMIC_SEQ_CST);
    ctx->Benchmark->RunCore(myIndex, ctx->TotalWorkers);
}

// Single-AP context for core-cycle: always runs as sole worker (index 0 of 1)
struct ApSingleContext {
    IBenchmark* Benchmark;
};

extern "C" VOID EFIAPI ApSingleProc(VOID* Buffer) {
    auto* ctx = static_cast<ApSingleContext*>(Buffer);
    ctx->Benchmark->RunCore(0, 1);
}

// ── Helpers ──────────────────────────────────────────────────

static UINT64 Median64(UINT64* arr, UINT32 count) {
    if (count == 0) return 0;
    // Insertion sort
    for (UINT32 i = 1; i < count; ++i) {
        UINT64 key = arr[i];
        UINT32 j = i;
        while (j > 0 && arr[j - 1] > key) { arr[j] = arr[j - 1]; --j; }
        arr[j] = key;
    }
    if (count % 2 == 1) return arr[count / 2];
    return (arr[count / 2 - 1] + arr[count / 2]) / 2;
}

// ── Live progress display ─────────────────────────────────────

struct ProgressCtx {
    const char* Name;
    bool        MultiCore;
    UINT32      CoreCount;
    // Core-cycle extra info (IsCoreCycle == true activates alternate display)
    bool        IsCoreCycle;
    UINT32      CycleCurrentCore;  // 1-based
    UINT32      CycleTotalCores;
    UINT32      CycleCurrentRun;   // 1-based
    UINT32      CycleTotalRuns;
};

static int ProgAppend(char* buf, int pos, const char* s) {
    for (; *s; ++s) buf[pos++] = *s;
    return pos;
}

static void FmtMinSec(UINT64 us, char* out) {
    UINT64 s = us / 1000000ULL;
    UINT64 m = s / 60;
    s = s % 60;
    int p = ProgAppend(out, 0, UintToStr(m));
    out[p++] = ':';
    out[p++] = (char)('0' + s / 10);
    out[p++] = (char)('0' + s % 10);
    out[p] = '\0';
}

static void DrawLiveProgress(const ProgressReport& r, void* vctx) {
    auto* pc = static_cast<ProgressCtx*>(vctx);

    Renderer::Clear();
    const int cols = static_cast<int>(Renderer::Columns());
    const int rows = static_cast<int>(Renderer::Rows());

    // Header
    Renderer::FillRow(0, Theme::Current().HeaderBorder);
    Renderer::DrawText(2, 0, "BENCHMARK IN PROGRESS", Theme::Current().HeaderText);
    Renderer::FillRow(1, Theme::Current().Separator);

    // Benchmark name (left) + mode (right) on row 3
    {
        static char nb[128];
        int p = 0;
        nb[p++] = ' '; nb[p++] = ' ';
        p = ProgAppend(nb, p, pc->Name);
        nb[p] = '\0';
        Renderer::DrawText(0, 3, nb, Theme::Current().Accent);

        static char mb[64];
        p = 0;
        if (pc->IsCoreCycle) {
            p = ProgAppend(mb, p, "Core-cycle (");
            p = ProgAppend(mb, p, UintToStr(pc->CycleTotalCores));
            p = ProgAppend(mb, p, " cores x ");
            p = ProgAppend(mb, p, UintToStr(pc->CycleTotalRuns));
            p = ProgAppend(mb, p, " runs)");
        } else if (pc->MultiCore && pc->CoreCount > 0) {
            p = ProgAppend(mb, p, "Multi-core (");
            p = ProgAppend(mb, p, UintToStr(pc->CoreCount));
            p = ProgAppend(mb, p, " APs)");
        } else {
            p = ProgAppend(mb, p, "Single-core (BSP)");
        }
        mb[p] = '\0';
        Renderer::DrawText(cols - p - 2, 3, mb, Theme::Current().TextDim);
    }

    // Core-cycle: show current core + run on row 9 (below score)
    if (pc->IsCoreCycle) {
        static char cc[80];
        int p = 0;
        cc[p++] = ' '; cc[p++] = ' ';
        p = ProgAppend(cc, p, "Core ");
        p = ProgAppend(cc, p, UintToStr(pc->CycleCurrentCore));
        p = ProgAppend(cc, p, " of ");
        p = ProgAppend(cc, p, UintToStr(pc->CycleTotalCores));
        p = ProgAppend(cc, p, "   Run ");
        p = ProgAppend(cc, p, UintToStr(pc->CycleCurrentRun));
        p = ProgAppend(cc, p, " of ");
        p = ProgAppend(cc, p, UintToStr(pc->CycleTotalRuns));
        cc[p] = '\0';
        Renderer::DrawText(0, 9, cc, Theme::Current().Warning);
    }

    // Progress bar + elapsed/budget on row 5
    {
        UINT64 pct = r.BudgetUs > 0 ? (r.ElapsedUs * 100ULL / r.BudgetUs) : 0;
        if (pct > 100) pct = 100;

        static char bar[80];
        int p = 0;
        bar[p++] = ' '; bar[p++] = ' ';
        bar[p++] = '[';
        const int W = 32;
        int filled = (int)(pct * W / 100);
        for (int i = 0; i < W; ++i) bar[p++] = (i < filled) ? '#' : '.';
        bar[p++] = ']';
        bar[p++] = ' ';
        if (pct >= 100) {
            bar[p++] = '1'; bar[p++] = '0'; bar[p++] = '0';
        } else if (pct >= 10) {
            bar[p++] = ' ';
            bar[p++] = (char)('0' + pct / 10);
            bar[p++] = (char)('0' + pct % 10);
        } else {
            bar[p++] = ' '; bar[p++] = ' ';
            bar[p++] = (char)('0' + pct);
        }
        bar[p++] = '%';
        bar[p] = '\0';
        Renderer::DrawText(0, 5, bar, Theme::Current().CheckMark);

        static char tb[32];
        char el[12], bd[12];
        FmtMinSec(r.ElapsedUs, el);
        FmtMinSec(r.BudgetUs, bd);
        p = ProgAppend(tb, 0, el);
        p = ProgAppend(tb, p, " / ");
        p = ProgAppend(tb, p, bd);
        tb[p] = '\0';
        Renderer::DrawText(cols - p - 2, 5, tb, Theme::Current().TextDim);
    }

    // Score on row 7
    {
        static char sb[80];
        int p = 0;
        sb[p++] = ' '; sb[p++] = ' ';
        p = ProgAppend(sb, p, "Score:  ");
        if (r.Score > 0) {
            p = ProgAppend(sb, p, UintToStr(r.Score));
            sb[p++] = ' ';
            p = ProgAppend(sb, p, r.Unit);
        } else {
            p = ProgAppend(sb, p, "-- ");
            p = ProgAppend(sb, p, r.Unit);
        }
        sb[p] = '\0';
        Renderer::DrawText(0, 7, sb, Theme::Current().Text);
    }

    // Reassurance (row 9 used by core-cycle info when active, so shift to row 11 then)
    int reassureRow = pc->IsCoreCycle ? 11 : 9;
    Renderer::DrawText(2, reassureRow, "System is working normally.  Please wait...", Theme::Current().TextDim);

    // Footer (matches Tui::DrawFooter layout)
    Renderer::FillRow(rows - 3, Theme::Current().Separator);
    const char* copy = "(c) 2026 Darren Horrocks | https://github.com/bizzehdee/UefiBenchmark | MIT License";
    Renderer::DrawTextBg(0, rows - 2, Renderer::Pad(copy, cols), Theme::Current().Footer, Theme::Current().Background);
    Renderer::DrawTextBg(0, rows - 1, Renderer::Pad("", cols), Theme::Current().TextDim, Theme::Current().Background);

    Renderer::Present();
}

// ── Progress (short benchmarks) ──────────────────────────────

static void DrawProgress(const char* title, const char* name,
                         UINTN current, UINTN total, bool multiCore) {
    Renderer::Clear();
    Renderer::DrawText(2, 1, title, Theme::Current().Accent);
    Renderer::DrawText(2, 3, "Progress:", Theme::Current().Text);

    char buf[64];
    const char* cs = UintToStr(current);
    int p = 0;
    for (int i = 0; cs[i]; ++i) buf[p++] = cs[i];
    buf[p++] = ' '; buf[p++] = '/'; buf[p++] = ' ';
    const char* ts = UintToStr(total);
    for (int i = 0; ts[i]; ++i) buf[p++] = ts[i];
    buf[p] = '\0';

    Renderer::DrawText(12, 3, buf, Theme::Current().Accent);
    Renderer::DrawText(2, 5, "Current:", Theme::Current().Text);
    Renderer::DrawText(12, 5, name, Theme::Current().Warning);
    Renderer::DrawText(2, 7, multiCore ? "Running (multi-core)..." : "Running...", Theme::Current().TextDim);
    Renderer::Present();
}

// ── RunSingle ────────────────────────────────────────────────

BenchmarkResult BenchmarkRunner::RunSingle(IBenchmark* benchmark, UINTN runs,
                                           bool multiCore) {
    BenchmarkResult result;
    result.Name      = benchmark->GetName();
    result.Category  = benchmark->GetCategory();
    result.MultiCore = multiCore;
    result.RunTimesUs.Reserve(runs);

    auto* mp = SystemInfo::GetMpServices();
    UINT32 apCount = 0;

    if (multiCore && mp) {
        if (CoreSelection::Count() > 0) {
            apCount = CoreSelection::SelectedCount();
        } else {
            UINT32 enabled = SystemInfo::GetEnabledProcessorCount();
            apCount = (enabled > 1) ? enabled - 1 : 0;
        }
    }

    if (multiCore && apCount == 0) {
        multiCore = false;
        result.MultiCore = false;
    }

    result.CoreCount = multiCore ? apCount : 1;

    // Set up live progress callback for long benchmarks
    bool isLong = (benchmark->GetDurationClass() == DurationClass::Long);
    ProgressCtx pCtx;
    if (isLong) {
        pCtx.Name      = benchmark->GetName();
        pCtx.MultiCore = multiCore && result.CoreCount > 1;
        pCtx.CoreCount = result.CoreCount;
        benchmark->SetProgressCallback(DrawLiveProgress, &pCtx);
    }

    // Build list of APs to temporarily disable (unselected but available APs)
    UINTN disabledAPs[CoreSelection::MAX_APS];
    UINT32 disabledCount = 0;

    if (multiCore && mp && CoreSelection::Count() > 0) {
        CoreSelection::ApInfo* roster = CoreSelection::GetAll();
        UINT32 total = CoreSelection::Count();
        for (UINT32 i = 0; i < total; ++i) {
            if (roster[i].Available && !roster[i].Selected) {
                EFI_STATUS es = mp->EnableDisableAP(mp, roster[i].ProcIndex, FALSE, nullptr);
                if (!EFI_ERROR(es)) disabledAPs[disabledCount++] = roster[i].ProcIndex;
            }
        }
    }

    benchmark->Setup();

    for (UINTN r = 0; r < runs; ++r) {
        benchmark->PreRun();

        if (multiCore) {
            ApContext ctx;
            ctx.Benchmark    = benchmark;
            ctx.TotalWorkers = apCount;
            ctx.WorkerIndex  = 0;

            Timer::Start();

            EFI_STATUS status = mp->StartupAllAPs(
                mp, ApBenchmarkProc, FALSE, NULL, 0, &ctx, NULL);

            UINT64 elapsed = Timer::ElapsedUs();

            if (EFI_ERROR(status)) {
                Timer::Start();
                benchmark->Run();
                elapsed = Timer::ElapsedUs();
            }

            result.RunTimesUs.PushBack(elapsed);
        } else {
            Timer::Start();
            benchmark->Run();
            UINT64 elapsed = Timer::ElapsedUs();
            result.RunTimesUs.PushBack(elapsed);
        }
    }

    benchmark->Teardown();

    // Re-enable any APs we parked for this run
    if (mp) {
        for (UINT32 i = 0; i < disabledCount; ++i)
            mp->EnableDisableAP(mp, disabledAPs[i], TRUE, nullptr);
    }

    if (isLong) {
        benchmark->SetProgressCallback(nullptr, nullptr);
    }

    result.TotalTimeUs  = Stats::GetSum(result.RunTimesUs);
    result.Score        = benchmark->GetScore();
    result.Unit         = benchmark->GetUnit();
    return result;
}

// ── RunCoreCycle ─────────────────────────────────────────────

BenchmarkResult BenchmarkRunner::RunCoreCycle(IBenchmark* benchmark, UINTN runs,
                                               bool allCores) {
    auto* mp = SystemInfo::GetMpServices();
    if (!mp || CoreSelection::Count() == 0) {
        // No MP Services or no APs — fall back to single-core
        BenchmarkResult r = RunSingle(benchmark, runs, false);
        r.RunModeUsed = RunMode::CoreCycle;
        return r;
    }

    // Build the AP list to cycle over
    UINTN apList[MAX_CYCLE_CORES];
    UINT32 apCount = 0;

    if (allCores) {
        CoreSelection::ApInfo* roster = CoreSelection::GetAll();
        UINT32 total = CoreSelection::Count();
        for (UINT32 i = 0; i < total && apCount < MAX_CYCLE_CORES; ++i) {
            if (roster[i].Available)
                apList[apCount++] = roster[i].ProcIndex;
        }
    } else {
        apCount = CoreSelection::GetSelectedIndices(apList, MAX_CYCLE_CORES);
    }

    if (apCount == 0) {
        BenchmarkResult r = RunSingle(benchmark, runs, false);
        r.RunModeUsed = RunMode::CoreCycle;
        return r;
    }

    BenchmarkResult result;
    result.Name              = benchmark->GetName();
    result.Category          = benchmark->GetCategory();
    result.Unit              = benchmark->GetUnit();
    result.MultiCore         = false;
    result.CoreCount         = apCount;
    result.RunModeUsed       = RunMode::CoreCycle;
    result.PerCoreSampleCount = apCount;
    result.RunTimesUs.Reserve(apCount * runs);

    // Progress context for live display — always populated for core-cycle
    bool isLong = (benchmark->GetDurationClass() == DurationClass::Long);
    ProgressCtx pCtx;
    pCtx.Name             = benchmark->GetName();
    pCtx.MultiCore        = false;
    pCtx.CoreCount        = 1;
    pCtx.IsCoreCycle      = true;
    pCtx.CycleTotalCores  = apCount;
    pCtx.CycleTotalRuns   = static_cast<UINT32>(runs);
    pCtx.CycleCurrentCore = 1;
    pCtx.CycleCurrentRun  = 1;

    if (isLong) {
        benchmark->SetProgressCallback(DrawLiveProgress, &pCtx);
    }

    benchmark->Setup();

    ApSingleContext apCtx;
    apCtx.Benchmark = benchmark;

    UINT64 perCoreAvg[MAX_CYCLE_CORES] = {};

    for (UINT32 c = 0; c < apCount; ++c) {
        pCtx.CycleCurrentCore = c + 1;
        pCtx.CycleCurrentRun  = 1;

        UINT64 scoreSum = 0;
        UINT64 coreMin  = ~0ULL;
        UINT64 coreMax  = 0;
        UINT64 timeSum  = 0;
        UINT32 runsDone = 0;

        for (UINTN r = 0; r < runs; ++r) {
            pCtx.CycleCurrentRun = static_cast<UINT32>(r + 1);

            if (!isLong) {
                ProgressReport pr;
                pr.ElapsedUs = 0;
                pr.BudgetUs  = 0;
                pr.Score     = 0;
                pr.Unit      = benchmark->GetUnit();
                DrawLiveProgress(pr, &pCtx);
            }

            benchmark->PreRun();
            Timer::Start();
            EFI_STATUS st = mp->StartupThisAP(
                mp, ApSingleProc, apList[c], NULL, 0, &apCtx, NULL);
            UINT64 elapsed = Timer::ElapsedUs();

            if (EFI_ERROR(st)) break;

            result.RunTimesUs.PushBack(elapsed);
            timeSum += elapsed;
            ++runsDone;
            UINT64 score = benchmark->GetScore();
            scoreSum += score;
            if (score < coreMin) coreMin = score;
            if (score > coreMax) coreMax = score;
        }

        result.PerCoreScore[c]   = runsDone > 0 ? scoreSum / runsDone : 0;
        result.PerCoreMin[c]     = (coreMin == ~0ULL) ? 0 : coreMin;
        result.PerCoreMax[c]     = coreMax;
        result.PerCoreTimeUs[c]  = runsDone > 0 ? timeSum / runsDone : 0;
        result.PerCoreApIndex[c] = static_cast<UINT32>(apList[c]);
        perCoreAvg[c]            = result.PerCoreScore[c];
    }

    benchmark->Teardown();

    if (isLong) benchmark->SetProgressCallback(nullptr, nullptr);

    result.TotalTimeUs = Stats::GetSum(result.RunTimesUs);

    // Aggregate score: median across cycled cores (robust to one outlier)
    UINT64 sortBuf[MAX_CYCLE_CORES];
    for (UINT32 i = 0; i < apCount; ++i) sortBuf[i] = perCoreAvg[i];
    result.Score = Median64(sortBuf, apCount);
    result.Unit  = benchmark->GetUnit();

    return result;
}

// ── RunAll ───────────────────────────────────────────────────

Vector<BenchmarkResult> BenchmarkRunner::RunAll(UINTN runs) {
    UINTN count = BenchmarkRegistry::Count();
    UINTN indices[32];
    RunMode modes[32];
    for (UINTN i = 0; i < count && i < 32; ++i) {
        indices[i] = i;
        modes[i]   = RunMode::SingleCore;
    }
    return RunSelected(indices, modes, count, runs);
}

// ── RunSelected ──────────────────────────────────────────────

Vector<BenchmarkResult> BenchmarkRunner::RunSelected(
    const UINTN* indices, const RunMode* modes, UINTN count, UINTN runs,
    bool coreCycleAllCores) {

    Vector<BenchmarkResult> results;
    IBenchmark** all = BenchmarkRegistry::GetAll();
    UINTN totalBench = BenchmarkRegistry::Count();

    results.Reserve(count);
    for (UINTN i = 0; i < count; ++i) {
        if (indices[i] >= totalBench) continue;
        IBenchmark* bm = all[indices[i]];
        bool isCC = (modes[i] == RunMode::CoreCycle);
        DrawProgress("Running Benchmarks", bm->GetName(),
                     i + 1, count, modes[i] == RunMode::MultiCore);
        BenchmarkResult r = isCC
            ? RunCoreCycle(bm, runs, coreCycleAllCores)
            : RunSingle(bm, runs, modes[i] == RunMode::MultiCore);
        results.PushBack(static_cast<BenchmarkResult&&>(r));
    }
    return results;
}
