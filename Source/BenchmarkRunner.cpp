// Benchmark runner with multi-run and multi-core (MP Services) support.

#include "BenchmarkRunner.h"
#include "BenchmarkRegistry.h"
#include "CoreSelection.h"
#include "Timer.h"
#include "Statistics.h"
#include "Renderer.h"
#include "SystemInfo.h"
#include "Freestanding.h"
#include "BenchmarkConstants.h"
#include "MachineCheck.h"
#include "Screens/UiHelpers.h"

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

// ── Category run context ──────────────────────────────────────
// Set by RunCategory so both DrawProgress and DrawLiveProgress can show
// "CPU Suite (3/8)" context. Null when not in a category run.
static struct {
    const char* Name;
    UINT32      Current;  // 1-based benchmark index within the category
    UINT32      Total;
} sCatCtx = { nullptr, 0, 0 };

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
    UINT32      CoreCount;    // total workers (APs + BSP if included)
    bool        IncludeBsp;   // BSP is also running as a worker (sequential phase)
    bool        IsBspPhase;   // currently in the BSP-only sequential phase
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
    UINT64 s = us / US_PER_SECOND;
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

    // Category context (row 2) when running a category suite
    if (sCatCtx.Name) {
        static char catLine[96];
        int p = ProgAppend(catLine, 0, "  ");
        p = ProgAppend(catLine, p, sCatCtx.Name);
        p = ProgAppend(catLine, p, " Suite  (");
        p = ProgAppend(catLine, p, UintToStr(sCatCtx.Current));
        p = ProgAppend(catLine, p, " / ");
        p = ProgAppend(catLine, p, UintToStr(sCatCtx.Total));
        p = ProgAppend(catLine, p, ")");
        catLine[p] = '\0';
        Renderer::DrawText(0, 2, catLine, Theme::Current().TextDim);
    }

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
        } else if (pc->MultiCore && pc->IncludeBsp && pc->IsBspPhase) {
            p = ProgAppend(mb, p, "BSP Phase (Core 0)");
        } else if (pc->MultiCore && pc->IncludeBsp && pc->CoreCount > 1) {
            p = ProgAppend(mb, p, "Multi-core (");
            p = ProgAppend(mb, p, UintToStr(pc->CoreCount - 1));
            p = ProgAppend(mb, p, " APs + BSP)");
        } else if (pc->MultiCore && pc->CoreCount > 0) {
            p = ProgAppend(mb, p, "Multi-core (");
            p = ProgAppend(mb, p, UintToStr(pc->CoreCount));
            p = ProgAppend(mb, p, " APs)");
        } else {
            p = ProgAppend(mb, p, "Single-core (1 AP)");
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

    // Configured duration on row 6
    if (r.BudgetUs > 0) {
        static char db[48];
        int p = 0;
        db[p++] = ' '; db[p++] = ' ';
        p = ProgAppend(db, p, "Duration: ");
        p = ProgAppend(db, p, Ui::DurationStr(r.BudgetUs));
        db[p] = '\0';
        Renderer::DrawText(0, 6, db, Theme::Current().TextDim);
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

    // Optional sub-phase status (e.g. current test pattern) on row 8
    if (r.Status) {
        static char st[96];
        int p = 0;
        st[p++] = ' '; st[p++] = ' ';
        p = ProgAppend(st, p, "Testing: ");
        p = ProgAppend(st, p, r.Status);
        st[p] = '\0';
        Renderer::DrawText(0, 8, st, Theme::Current().Accent);
    }

    // Reassurance (row 9 used by core-cycle info when active, so shift to row 11 then)
    int reassureRow = pc->IsCoreCycle ? 11 : 9;
    Renderer::DrawText(2, reassureRow, "System is working normally.  Please wait...", Theme::Current().TextDim);

    // Footer (matches Tui::DrawFooter layout)
    Renderer::FillRow(rows - 3, Theme::Current().Separator);
    const char* copy = "(c) 2026 Darren Horrocks | https://github.com/bizzehdee/RootBench | MIT License";
    Renderer::DrawTextBg(0, rows - 2, Renderer::Pad(copy, cols), Theme::Current().Footer, Theme::Current().Background);
    Renderer::DrawTextBg(0, rows - 1, Renderer::Pad("", cols), Theme::Current().TextDim, Theme::Current().Background);

    Renderer::Present();
}

// ── BSP-driven AP dispatch helpers ───────────────────────────
// APs must not call Boot Services (GOP Blt). We dispatch them non-blocking with
// a completion event, then render from the BSP while they run. These wrap the
// VOID*-typed boot services with the right signatures.

static EFI_EVENT CreatePollEvent() {
    using CreateEventFn = EFI_STATUS (EFIAPI*)(UINT32, UINTN, VOID*, VOID*, EFI_EVENT*);
    auto Create = reinterpret_cast<CreateEventFn>(gBS->CreateEvent);
    EFI_EVENT evt = nullptr;
    if (EFI_ERROR(Create(0, 0, nullptr, nullptr, &evt))) return nullptr;  // plain, pollable
    return evt;
}

static bool EventSignaled(EFI_EVENT evt) {
    using CheckEventFn = EFI_STATUS (EFIAPI*)(EFI_EVENT);
    auto Check = reinterpret_cast<CheckEventFn>(gBS->CheckEvent);
    return Check(evt) == EFI_SUCCESS;
}

static void ClosePollEvent(EFI_EVENT evt) {
    using CloseEventFn = EFI_STATUS (EFIAPI*)(EFI_EVENT);
    auto Close = reinterpret_cast<CloseEventFn>(gBS->CloseEvent);
    Close(evt);
}

// Render live progress from the BSP until `doneEvt` signals (APs finished) or a
// safety cap elapses. Timer must already be started. Returns elapsed µs.
static UINT64 BspRenderWhileRunning(IBenchmark* bm, ProgressCtx* pc, EFI_EVENT doneEvt) {
    const UINT64 cap = bm->GetBudgetUs() + 5000000ULL;  // budget + 5s safety
    ProgressReport rep = {};
    rep.BudgetUs = bm->GetBudgetUs();
    rep.Unit     = bm->GetUnit();
    while (!EventSignaled(doneEvt)) {
        // BSP machine-check poll: covers the BSP's own core banks plus the
        // shared package banks (memory controller / LLC) while the APs run.
        MachineCheck::PollLocal();
        rep.ElapsedUs = Timer::ElapsedUs();
        rep.Score     = bm->GetScore();
        rep.Status    = bm->GetStatus();
        DrawLiveProgress(rep, pc);
        if (rep.ElapsedUs > cap) break;
        gBS->Stall(150000);  // ~150 ms between BSP renders
    }
    return Timer::ElapsedUs();
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

    bool includeBsp = multiCore && CoreSelection::GetIncludeBsp()
                      && (benchmark->GetThreadingMode() != ThreadingMode::SingleOnly);
    if (includeBsp) result.CoreCount = apCount + 1;  // APs + BSP

    // Set up live progress callback
    ProgressCtx pCtx = {};
    pCtx.Name       = benchmark->GetName();
    pCtx.MultiCore  = multiCore && result.CoreCount > 1;
    pCtx.CoreCount  = result.CoreCount;
    pCtx.IncludeBsp = includeBsp;
    pCtx.IsBspPhase = false;
    benchmark->SetProgressCallback(DrawLiveProgress, &pCtx);

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
    MachineCheck::BeginRun();   // re-baseline MCA banks for this benchmark

    for (UINTN r = 0; r < runs; ++r) {
        benchmark->PreRun();

        if (multiCore) {
            ApContext ctx;
            ctx.Benchmark    = benchmark;
            ctx.TotalWorkers = includeBsp ? apCount + 1 : apCount;
            ctx.WorkerIndex  = 0;

            // APs render nothing (Boot Services are BSP-only); they just run and
            // publish counters. Dispatch them non-blocking and render from here.
            benchmark->SetProgressCallback(nullptr, nullptr);

            EFI_EVENT doneEvt = CreatePollEvent();
            EFI_STATUS status  = (EFI_STATUS)(EFI_ERROR_BIT);
            if (doneEvt) {
                Timer::Start();
                status = mp->StartupAllAPs(mp, ApBenchmarkProc, FALSE, doneEvt, 0, &ctx, NULL);
            }

            UINT64 elapsed;
            if (EFI_ERROR(status)) {
                // Non-blocking dispatch unavailable/failed — fall back to a
                // blocking BSP-only run (BSP rendering is safe).
                if (doneEvt) ClosePollEvent(doneEvt);
                benchmark->SetProgressCallback(DrawLiveProgress, &pCtx);
                Timer::Start();
                benchmark->Run();
                elapsed = Timer::ElapsedUs();
            } else {
                elapsed = BspRenderWhileRunning(benchmark, &pCtx, doneEvt);
                ClosePollEvent(doneEvt);

                if (includeBsp) {
                    // BSP sequential phase: BSP runs as the last worker slot and
                    // renders itself (Boot Services on the BSP are safe).
                    pCtx.IsBspPhase = true;
                    benchmark->SetProgressCallback(DrawLiveProgress, &pCtx);
                    Timer::Start();
                    benchmark->RunCore(apCount, ctx.TotalWorkers);
                    elapsed += Timer::ElapsedUs();
                    pCtx.IsBspPhase = false;
                    benchmark->SetProgressCallback(nullptr, nullptr);
                }
            }

            result.RunTimesUs.PushBack(elapsed);
        } else {
            // Single-core: run on a randomly chosen available AP (never the BSP)
            // so the measured core isn't perturbed by the BSP's firmware/SMI
            // handling, and so the same physical core isn't always used. Falls
            // back to the BSP when MP Services or APs are unavailable.
            UINTN apProc  = 0;
            bool  haveAp  = false;
            if (mp && CoreSelection::Count() > 0) {
                CoreSelection::ApInfo* roster = CoreSelection::GetAll();
                UINT32 total = CoreSelection::Count();
                UINTN  avail[CoreSelection::MAX_APS];
                UINT32 navail = 0;
                for (UINT32 i = 0; i < total; ++i)
                    if (roster[i].Available) avail[navail++] = roster[i].ProcIndex;
                if (navail > 0) {
                    UINT32 pick = static_cast<UINT32>(Timer::ReadTSC() % navail);
                    apProc = avail[pick];
                    haveAp = true;
                }
            }

            ApSingleContext apCtx;
            apCtx.Benchmark = benchmark;

            UINT64 elapsed = 0;
            bool   ranOnAp = false;
            if (haveAp) {
                EFI_EVENT doneEvt = CreatePollEvent();
                if (doneEvt) {
                    benchmark->SetProgressCallback(nullptr, nullptr);  // BSP renders
                    Timer::Start();
                    EFI_STATUS st = mp->StartupThisAP(mp, ApSingleProc, apProc,
                                                      doneEvt, 0, &apCtx, NULL);
                    if (!EFI_ERROR(st)) {
                        elapsed = BspRenderWhileRunning(benchmark, &pCtx, doneEvt);
                        ranOnAp = true;
                    }
                    ClosePollEvent(doneEvt);
                }
            }
            if (!ranOnAp) {
                // Fallback: run on the BSP (rendering from the BSP is safe).
                benchmark->SetProgressCallback(DrawLiveProgress, &pCtx);
                Timer::Start();
                benchmark->Run();
                elapsed = Timer::ElapsedUs();
            }
            result.RunTimesUs.PushBack(elapsed);
        }
    }

    benchmark->Teardown();

    // Re-enable any APs we parked for this run
    if (mp) {
        for (UINT32 i = 0; i < disabledCount; ++i)
            mp->EnableDisableAP(mp, disabledAPs[i], TRUE, nullptr);
    }

    benchmark->SetProgressCallback(nullptr, nullptr);

    result.TotalTimeUs   = Stats::GetSum(result.RunTimesUs);
    result.Score         = benchmark->GetScore();
    result.Unit          = benchmark->GetUnit();
    result.IncludeInScore = benchmark->IncludeInCategoryScore();
    result.CategoryWeight = benchmark->GetCategoryWeight();
    result.BudgetUs       = benchmark->GetBudgetUs();
    result.RawMetric      = benchmark->GetRawMetric();
    result.RawUnit        = benchmark->GetRawUnit();
    result.Note           = benchmark->GetStatusNote();
    result.IsaPath        = benchmark->GetIsaPath();
    result.SweepCount     = benchmark->GetSweep(result.SweepSizeMB, result.SweepValue, MAX_SWEEP_POINTS);
    result.McCorrected    = MachineCheck::CorrectedCount();
    result.McUncorrected  = MachineCheck::UncorrectedCount();
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
    result.BudgetUs          = benchmark->GetBudgetUs();
    result.RawMetric         = benchmark->GetRawMetric();
    result.RawUnit           = benchmark->GetRawUnit();
    result.MultiCore         = false;
    result.CoreCount         = apCount;
    result.RunModeUsed       = RunMode::CoreCycle;
    result.PerCoreSampleCount = apCount;
    result.RunTimesUs.Reserve(apCount * runs);

    // Progress context for live display
    ProgressCtx pCtx;
    pCtx.Name             = benchmark->GetName();
    pCtx.MultiCore        = false;
    pCtx.CoreCount        = 1;
    pCtx.IsCoreCycle      = true;
    pCtx.CycleTotalCores  = apCount;
    pCtx.CycleTotalRuns   = static_cast<UINT32>(runs);
    pCtx.CycleCurrentCore = 1;
    pCtx.CycleCurrentRun  = 1;
    // BSP renders; APs must not call Boot Services.
    benchmark->SetProgressCallback(nullptr, nullptr);

    benchmark->Setup();
    MachineCheck::BeginRun();   // re-baseline MCA banks for this benchmark

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

            benchmark->PreRun();
            EFI_EVENT doneEvt = CreatePollEvent();
            if (!doneEvt) break;
            Timer::Start();
            EFI_STATUS st = mp->StartupThisAP(
                mp, ApSingleProc, apList[c], doneEvt, 0, &apCtx, NULL);
            if (EFI_ERROR(st)) { ClosePollEvent(doneEvt); break; }
            UINT64 elapsed = BspRenderWhileRunning(benchmark, &pCtx, doneEvt);
            ClosePollEvent(doneEvt);

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

    benchmark->SetProgressCallback(nullptr, nullptr);

    result.TotalTimeUs = Stats::GetSum(result.RunTimesUs);

    // Aggregate score: median across cycled cores (robust to one outlier)
    UINT64 sortBuf[MAX_CYCLE_CORES];
    for (UINT32 i = 0; i < apCount; ++i) sortBuf[i] = perCoreAvg[i];
    result.Score = Median64(sortBuf, apCount);
    result.Unit  = benchmark->GetUnit();
    result.Note  = benchmark->GetStatusNote();
    result.IsaPath = benchmark->GetIsaPath();
    result.SweepCount = benchmark->GetSweep(result.SweepSizeMB, result.SweepValue, MAX_SWEEP_POINTS);
    result.McCorrected   = MachineCheck::CorrectedCount();
    result.McUncorrected = MachineCheck::UncorrectedCount();

    // If no run completed, every AP dispatch failed — surface that as the skip
    // reason instead of a bare zero score.
    if (!result.Note && result.RunTimesUs.Empty())
        result.Note = "AP dispatch failed";

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

// ── RunCategory ──────────────────────────────────────────────

Vector<BenchmarkResult> BenchmarkRunner::RunCategory(const char* category, UINTN runs) {
    IBenchmark** all   = BenchmarkRegistry::GetAll();
    UINTN        total = BenchmarkRegistry::Count();

    UINTN   indices[32];
    RunMode modes[32];
    UINTN   cnt = 0;

    for (UINTN i = 0; i < total && cnt < 32; ++i) {
        if (StrCmp(all[i]->GetCategory(), category) != 0) continue;
        indices[cnt] = i;
        ThreadingMode tm = all[i]->GetThreadingMode();
        modes[cnt] = (tm == ThreadingMode::SingleOnly)
                     ? RunMode::SingleCore : RunMode::MultiCore;
        ++cnt;
    }

    if (cnt == 0) return Vector<BenchmarkResult>();

    sCatCtx.Name    = category;
    sCatCtx.Current = 1;
    sCatCtx.Total   = static_cast<UINT32>(cnt);

    Vector<BenchmarkResult> results = RunSelected(indices, modes, cnt, runs);

    sCatCtx.Name = nullptr;
    return results;
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
        if (sCatCtx.Name) sCatCtx.Current = static_cast<UINT32>(i + 1);
        bool isCC = (modes[i] == RunMode::CoreCycle);
        BenchmarkResult r = isCC
            ? RunCoreCycle(bm, runs, coreCycleAllCores)
            : RunSingle(bm, runs, modes[i] == RunMode::MultiCore);
        results.PushBack(static_cast<BenchmarkResult&&>(r));
    }
    return results;
}
