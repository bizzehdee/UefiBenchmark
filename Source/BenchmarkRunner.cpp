// Benchmark runner with multi-run and multi-core (MP Services) support.

#include "BenchmarkRunner.h"
#include "BenchmarkRegistry.h"
#include "Timer.h"
#include "Statistics.h"
#include "Renderer.h"
#include "SystemInfo.h"

// ── AP dispatch context ──────────────────────────────────────

struct ApContext {
    IBenchmark* Benchmark;
    UINT32      TotalWorkers;
    volatile UINT32 WorkerIndex;  // atomic counter for compact worker IDs
};

// AP procedure: each AP atomically claims a worker index, then runs.
// Must be extern "C" with EFIAPI calling convention.
extern "C" VOID EFIAPI ApBenchmarkProc(VOID* Buffer) {
    auto* ctx = static_cast<ApContext*>(Buffer);
    UINT32 myIndex = __atomic_fetch_add(&ctx->WorkerIndex, 1, __ATOMIC_SEQ_CST);
    ctx->Benchmark->RunCore(myIndex, ctx->TotalWorkers);
}

// ── Helpers ──────────────────────────────────────────────────

static void DrawProgress(const char* title, const char* name,
                         UINTN current, UINTN total, bool multiCore) {
    Renderer::Clear();
    Renderer::DrawText(2, 1, title, Theme::Accent);
    Renderer::DrawText(2, 3, "Progress:", Theme::Text);

    // "N / M"
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

    // Resolve multi-core capability
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

    benchmark->Setup();

    for (UINTN r = 0; r < runs; ++r) {
        if (multiCore) {
            ApContext ctx;
            ctx.Benchmark    = benchmark;
            ctx.TotalWorkers = apCount;
            ctx.WorkerIndex  = 0;

            Timer::Start();

            // Blocking dispatch: all APs run concurrently, BSP waits.
            EFI_STATUS status = mp->StartupAllAPs(
                mp,
                ApBenchmarkProc,
                FALSE,       // SingleThread = FALSE → all APs at once
                NULL,        // WaitEvent = NULL → blocking
                0,           // timeout = 0 → infinite
                &ctx,
                NULL         // no FailedCpuList
            );

            UINT64 elapsed = Timer::ElapsedUs();

            if (EFI_ERROR(status)) {
                // AP dispatch failed — fall back to single-core for this run
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

    result.TotalTimeUs = Stats::GetSum(result.RunTimesUs);
    return result;
}

// ── RunAll ───────────────────────────────────────────────────

Vector<BenchmarkResult> BenchmarkRunner::RunAll(UINTN runs) {
    UINTN count = BenchmarkRegistry::Count();
    UINTN indices[32];
    bool mc[32];
    for (UINTN i = 0; i < count && i < 32; ++i) {
        indices[i] = i;
        mc[i] = false;  // default single-core
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
