// Benchmark runner with multi-run and multi-core (MP Services) support.

#include "BenchmarkRunner.h"
#include "BenchmarkRegistry.h"
#include "Timer.h"
#include "Statistics.h"
#include "Renderer.h"
#include "SystemInfo.h"
#include "Freestanding.h"

// ── AP dispatch context ──────────────────────────────────────

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

// ── Live progress display ─────────────────────────────────────

struct ProgressCtx {
    const char* Name;
    bool        MultiCore;
    UINT32      CoreCount;
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
    Renderer::FillRow(0, Theme::HeaderBorder);
    Renderer::DrawText(2, 0, "BENCHMARK IN PROGRESS", Theme::HeaderText);
    Renderer::FillRow(1, Theme::Separator);

    // Benchmark name (left) + mode (right) on row 3
    {
        static char nb[128];
        int p = 0;
        nb[p++] = ' '; nb[p++] = ' ';
        p = ProgAppend(nb, p, pc->Name);
        nb[p] = '\0';
        Renderer::DrawText(0, 3, nb, Theme::Accent);

        static char mb[48];
        p = 0;
        if (pc->MultiCore && pc->CoreCount > 0) {
            p = ProgAppend(mb, p, "Multi-core (");
            p = ProgAppend(mb, p, UintToStr(pc->CoreCount));
            p = ProgAppend(mb, p, " APs)");
        } else {
            p = ProgAppend(mb, p, "Single-core (BSP)");
        }
        mb[p] = '\0';
        Renderer::DrawText(cols - p - 2, 3, mb, Theme::TextDim);
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
        Renderer::DrawText(0, 5, bar, Theme::CheckMark);

        static char tb[32];
        char el[12], bd[12];
        FmtMinSec(r.ElapsedUs, el);
        FmtMinSec(r.BudgetUs, bd);
        p = ProgAppend(tb, 0, el);
        p = ProgAppend(tb, p, " / ");
        p = ProgAppend(tb, p, bd);
        tb[p] = '\0';
        Renderer::DrawText(cols - p - 2, 5, tb, Theme::TextDim);
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
        Renderer::DrawText(0, 7, sb, Theme::Text);
    }

    // Reassurance
    Renderer::DrawText(2, 9, "System is working normally.  Please wait...", Theme::TextDim);

    // Footer (matches Tui::DrawFooter layout)
    Renderer::FillRow(rows - 3, Theme::Separator);
    const char* copy = "(c) 2026 Darren Horrocks | https://github.com/bizzehdee/UefiBenchmark | MIT License";
    Renderer::DrawTextBg(0, rows - 2, Renderer::Pad(copy, cols), Theme::Footer, Theme::Background);
    Renderer::DrawTextBg(0, rows - 1, Renderer::Pad("", cols), Theme::TextDim, Theme::Background);

    Renderer::Present();
}

// ── Helpers ──────────────────────────────────────────────────

static void DrawProgress(const char* title, const char* name,
                         UINTN current, UINTN total, bool multiCore) {
    Renderer::Clear();
    Renderer::DrawText(2, 1, title, Theme::Accent);
    Renderer::DrawText(2, 3, "Progress:", Theme::Text);

    char buf[64];
    const char* cs = UintToStr(current);
    int p = 0;
    for (int i = 0; cs[i]; ++i) buf[p++] = cs[i];
    buf[p++] = ' '; buf[p++] = '/'; buf[p++] = ' ';
    const char* ts = UintToStr(total);
    for (int i = 0; ts[i]; ++i) buf[p++] = ts[i];
    buf[p] = '\0';

    Renderer::DrawText(12, 3, buf, Theme::Accent);
    Renderer::DrawText(2, 5, "Current:", Theme::Text);
    Renderer::DrawText(12, 5, name, Theme::Warning);
    Renderer::DrawText(2, 7, multiCore ? "Running (multi-core)..." : "Running...", Theme::TextDim);
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
        UINT32 enabled = SystemInfo::GetEnabledProcessorCount();
        apCount = (enabled > 1) ? enabled - 1 : 0;
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

    if (isLong) {
        benchmark->SetProgressCallback(nullptr, nullptr);
    }

    result.TotalTimeUs  = Stats::GetSum(result.RunTimesUs);
    result.Score        = benchmark->GetScore();
    result.Unit         = benchmark->GetUnit();
    return result;
}

// ── RunAll ───────────────────────────────────────────────────

Vector<BenchmarkResult> BenchmarkRunner::RunAll(UINTN runs) {
    UINTN count = BenchmarkRegistry::Count();
    UINTN indices[32];
    bool mc[32];
    for (UINTN i = 0; i < count && i < 32; ++i) {
        indices[i] = i;
        mc[i] = false;
    }
    return RunSelected(indices, mc, count, runs);
}

// ── RunSelected ──────────────────────────────────────────────

Vector<BenchmarkResult> BenchmarkRunner::RunSelected(
    const UINTN* indices, const bool* multiCore, UINTN count, UINTN runs) {

    Vector<BenchmarkResult> results;
    IBenchmark** all = BenchmarkRegistry::GetAll();
    UINTN totalBench = BenchmarkRegistry::Count();

    results.Reserve(count);
    for (UINTN i = 0; i < count; ++i) {
        if (indices[i] >= totalBench) continue;
        IBenchmark* bm = all[indices[i]];
        DrawProgress("Running Benchmarks", bm->GetName(),
                     i + 1, count, multiCore[i]);
        BenchmarkResult r = RunSingle(bm, runs, multiCore[i]);
        results.PushBack(static_cast<BenchmarkResult&&>(r));
    }
    return results;
}
