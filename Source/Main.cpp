// UEFI application entry point.

#include "UefiTypes.h"
#include "Freestanding.h"
#include "Timer.h"
#include "SystemInfo.h"
#include "CoreSelection.h"
#include "Renderer.h"
#include "BenchmarkRegistry.h"
#include "CpuFeatures.h"
#include "Tui.h"

// Long CPU benchmarks
#include "Benchmarks/IntThroughputBenchmark.h"
#include "Benchmarks/IntLatencyBenchmark.h"
#include "Benchmarks/FpScalarBenchmark.h"
#include "Benchmarks/FpVectorBenchmark.h"
#include "Benchmarks/BranchBenchmark.h"
#include "Benchmarks/AesBenchmark.h"
#include "Benchmarks/HashBenchmark.h"
#include "Benchmarks/MandelbrotBenchmark.h"

// Long memory benchmarks
#include "Benchmarks/MemBandwidthBenchmark.h"
#include "Benchmarks/MemLatencyBenchmark.h"
#include "Benchmarks/MemIntegrityBenchmark.h"

// AI readiness benchmarks
#include "Benchmarks/AiInt8Benchmark.h"
#include "Benchmarks/AiInt4Benchmark.h"
#include "Benchmarks/AiMemBenchmark.h"
#include "Benchmarks/AiCacheBenchmark.h"

// Stress benchmarks (overclock validation)
#include "Benchmarks/StressMemClockBenchmark.h"
#include "Benchmarks/StressMemLatencyBenchmark.h"
#include "Benchmarks/StressCpuPowerBenchmark.h"
#include "Benchmarks/StressCpuVerifyBenchmark.h"

extern "C" EFI_STATUS EFIAPI EfiMain(
    EFI_HANDLE        ImageHandle,
    EFI_SYSTEM_TABLE* SystemTable)
{
    // ── 1. Store globals ─────────────────────────────────────
    gST          = SystemTable;
    gBS          = SystemTable->BootServices;
    gImageHandle = ImageHandle;

    gBS->SetWatchdogTimer(0, 0, 0, nullptr);

    if (gST->ConIn)
        gST->ConIn->Reset(gST->ConIn, FALSE);

    // ── 2. Early text output ─────────────────────────────────
    ConPrintLine("UEFI Benchmark Suite - Initialising...");

    // ── 3. Initialise graphics ───────────────────────────────
    bool gopOk = Renderer::Init(1024, 768);

    if (gopOk) {
        Renderer::Clear();
        Renderer::DrawText(2, 1, "UEFI Benchmark Suite", Theme::Current().Accent);
        Renderer::DrawText(2, 3, "Calibrating timer...", Theme::Current().TextDim);
        Renderer::Present();
    } else {
        ConPrintLine("GOP unavailable - using text mode.");
        ConPrintLine("Calibrating timer...");
    }

    // ── 4. Calibrate TSC timer ───────────────────────────────
    Timer::Calibrate();

    if (gopOk) {
        Renderer::DrawText(2, 4, Timer::IsCalibrated() ? "Timer calibrated." : "Timer calibration failed!",
                           Timer::IsCalibrated() ? Theme::Current().Success : Theme::Current().Error);

        if (Timer::IsCalibrated()) {
            char tbuf[64];
            StrCopy(tbuf, UintToStr(Timer::CyclesPerUs()), sizeof(tbuf));
            char line[80];
            int p = 0;
            for (const char* s = "  "; *s; ++s) line[p++] = *s;
            for (int i = 0; tbuf[i]; ++i) line[p++] = tbuf[i];
            for (const char* s = " cycles/us"; *s; ++s) line[p++] = *s;
            line[p] = '\0';
            Renderer::DrawText(2, 5, line, Theme::Current().TextDim);
        }

        if (!Timer::HasInvariantTSC())
            Renderer::DrawText(2, 6, "Warning: Invariant TSC not detected", Theme::Current().Warning);
    } else {
        ConPrintLine(Timer::IsCalibrated() ? "Timer calibrated." : "Timer calibration FAILED.");
    }

    // ── 5. Detect system info + CPU features ─────────────────
    SystemInfo::Detect();
    CpuFeatures::Detect();
    CoreSelection::Init();

    if (gopOk) {
        Renderer::DrawText(2, 8, "Detecting system resources...", Theme::Current().TextDim);
        char info[128];
        int p = 0;
        const char* cpu = SystemInfo::GetCpuBrand();
        for (int i = 0; cpu[i] && p < 120; ++i) info[p++] = cpu[i];
        info[p] = '\0';
        Renderer::DrawText(2, 9, info, Theme::Current().Text);

        p = 0;
        for (const char* s = "Memory: "; *s; ++s) info[p++] = *s;
        char mbuf[24];
        StrCopy(mbuf, UintToStr(SystemInfo::GetTotalMemoryMB()), sizeof(mbuf));
        for (int i = 0; mbuf[i]; ++i) info[p++] = mbuf[i];
        for (const char* s = " MB"; *s; ++s) info[p++] = *s;
        info[p] = '\0';
        Renderer::DrawText(2, 10, info, Theme::Current().Text);

        // Show detected CPU features
        const auto& feat = CpuFeatures::Get();
        char fline[80];
        p = 0;
        for (const char* s = "Features: "; *s; ++s) fline[p++] = *s;
        if (feat.HasAVX2)  { for (const char* s = "AVX2 "; *s; ++s) fline[p++] = *s; }
        if (feat.HasFMA)   { for (const char* s = "FMA "; *s; ++s) fline[p++] = *s; }
        if (feat.HasAESNI) { for (const char* s = "AES "; *s; ++s) fline[p++] = *s; }
        if (feat.HasSSE42) { for (const char* s = "SSE4.2 "; *s; ++s) fline[p++] = *s; }
        fline[p] = '\0';
        Renderer::DrawText(2, 11, fline, Theme::Current().TextDim);

        Renderer::Present();
    }

    // ── 6. Register benchmarks ───────────────────────────────

    // Long CPU benchmarks
    IntThroughputBenchmark  intThroughput;
    IntLatencyBenchmark     intLatency;
    FpScalarBenchmark       fpScalar;
    FpVectorBenchmark       fpVector;
    BranchBenchmark         branchBench;
    AesBenchmark            aesBench;
    HashBenchmark           hashBench;
    MandelbrotBenchmark     mandelbrot;

    // Long memory benchmarks
    MemSeqWriteBenchmark    memSeqWrite;
    MemSeqReadBenchmark     memSeqRead;
    MemCopyBenchmark        memCopy;
    MemLatencyBenchmark     memLatency;
    MemIntegrityBenchmark   memIntegrity;

    // AI readiness benchmarks
    AiInt8Benchmark         aiInt8;
    AiInt4Benchmark         aiInt4;
    AiMemBenchmark          aiMem;
    AiCacheBenchmark        aiCache;

    // Stress benchmarks (overclock validation)
    StressMemClockBenchmark   stressMemClock;
    StressMemLatencyBenchmark stressMemLatency;
    StressCpuPowerBenchmark   stressCpuPower;
    StressCpuVerifyBenchmark  stressCpuVerify;

    // Register long CPU
    BenchmarkRegistry::Register(&intThroughput);
    BenchmarkRegistry::Register(&intLatency);
    BenchmarkRegistry::Register(&fpScalar);
    BenchmarkRegistry::Register(&fpVector);
    BenchmarkRegistry::Register(&branchBench);
    BenchmarkRegistry::Register(&aesBench);
    BenchmarkRegistry::Register(&hashBench);
    BenchmarkRegistry::Register(&mandelbrot);

    // Register long memory
    BenchmarkRegistry::Register(&memSeqWrite);
    BenchmarkRegistry::Register(&memSeqRead);
    BenchmarkRegistry::Register(&memCopy);
    BenchmarkRegistry::Register(&memLatency);
    BenchmarkRegistry::Register(&memIntegrity);

    // Register AI benchmarks
    BenchmarkRegistry::Register(&aiInt8);
    BenchmarkRegistry::Register(&aiInt4);
    BenchmarkRegistry::Register(&aiMem);
    BenchmarkRegistry::Register(&aiCache);

    // Register stress benchmarks
    BenchmarkRegistry::Register(&stressMemClock);
    BenchmarkRegistry::Register(&stressMemLatency);
    BenchmarkRegistry::Register(&stressCpuPower);
    BenchmarkRegistry::Register(&stressCpuVerify);

    if (gopOk) {
        char msg[64];
        int p = 0;
        char nbuf[8];
        StrCopy(nbuf, UintToStr(BenchmarkRegistry::Count()), sizeof(nbuf));
        for (int i = 0; nbuf[i]; ++i) msg[p++] = nbuf[i];
        for (const char* s = " benchmarks loaded."; *s; ++s) msg[p++] = *s;
        msg[p] = '\0';
        Renderer::DrawText(2, 13, msg, Theme::Current().TextDim);
        Renderer::DrawText(2, 15, "Press any key to continue...", Theme::Current().Warning);
        Renderer::Present();
        Renderer::WaitKey();
    } else {
        ConPrintLine("Benchmarks loaded. Press any key...");
        Renderer::WaitKey();
    }

    // ── 7. Launch TUI ────────────────────────────────────────
    Tui tui;
    tui.Run();

    return EFI_SUCCESS;
}
