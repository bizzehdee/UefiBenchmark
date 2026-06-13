// UEFI application entry point.
// Initialises globals, calibrates timer, detects system info,
// registers benchmarks, and launches the TUI.

#include "UefiTypes.h"
#include "Freestanding.h"
#include "Timer.h"
#include "SystemInfo.h"
#include "Renderer.h"
#include "BenchmarkRegistry.h"
#include "Tui.h"

// Benchmarks
#include "Benchmarks/CpuBenchmark.h"
#include "Benchmarks/MemoryBenchmark.h"
#include "Benchmarks/PiBenchmark.h"

extern "C" EFI_STATUS EFIAPI EfiMain(
    EFI_HANDLE        ImageHandle,
    EFI_SYSTEM_TABLE* SystemTable)
{
    // ── 1. Store globals ─────────────────────────────────────
    gST          = SystemTable;
    gBS          = SystemTable->BootServices;
    gImageHandle = ImageHandle;

    // Disable watchdog timer (default 5-min timeout kills us)
    gBS->SetWatchdogTimer(0, 0, 0, nullptr);

    // Reset keyboard input
    if (gST->ConIn)
        gST->ConIn->Reset(gST->ConIn, FALSE);

    // ── 2. Early text output while initialising ──────────────
    ConPrintLine("UEFI Benchmark Suite - Initialising...");

    // ── 3. Initialise graphics (GOP) ─────────────────────────
    bool gopOk = Renderer::Init(800, 600);

    if (gopOk) {
        Renderer::Clear();
        Renderer::DrawText(2, 1, "UEFI Benchmark Suite", Theme::Accent);
        Renderer::DrawText(2, 3, "Calibrating timer...", Theme::TextDim);
        Renderer::Present();
    } else {
        ConPrintLine("GOP unavailable - using text mode.");
        ConPrintLine("Calibrating timer...");
    }

    // ── 4. Calibrate TSC timer ───────────────────────────────
    Timer::Calibrate();

    if (gopOk) {
        Renderer::DrawText(2, 4, Timer::IsCalibrated() ? "Timer calibrated." : "Timer calibration failed!",
                           Timer::IsCalibrated() ? Theme::Success : Theme::Error);

        if (Timer::IsCalibrated()) {
            char tbuf[64];
            StrCopy(tbuf, UintToStr(Timer::CyclesPerUs()), sizeof(tbuf));
            char line[80];
            int p = 0;
            for (const char* s = "  "; *s; ++s) line[p++] = *s;
            for (int i = 0; tbuf[i]; ++i) line[p++] = tbuf[i];
            for (const char* s = " cycles/us"; *s; ++s) line[p++] = *s;
            line[p] = '\0';
            Renderer::DrawText(2, 5, line, Theme::TextDim);
        }

        if (!Timer::HasInvariantTSC()) {
            Renderer::DrawText(2, 6, "Warning: Invariant TSC not detected", Theme::Warning);
        }
    } else {
        ConPrintLine(Timer::IsCalibrated() ? "Timer calibrated." : "Timer calibration FAILED.");
    }

    // ── 5. Detect system info ────────────────────────────────
    SystemInfo::Detect();

    if (gopOk) {
        Renderer::DrawText(2, 8, "Detecting system resources...", Theme::TextDim);
        char info[128];
        int p = 0;
        const char* cpu = SystemInfo::GetCpuBrand();
        for (int i = 0; cpu[i] && p < 120; ++i) info[p++] = cpu[i];
        info[p] = '\0';
        Renderer::DrawText(2, 9, info, Theme::Text);

        p = 0;
        for (const char* s = "Memory: "; *s; ++s) info[p++] = *s;
        char mbuf[24];
        StrCopy(mbuf, UintToStr(SystemInfo::GetTotalMemoryMB()), sizeof(mbuf));
        for (int i = 0; mbuf[i]; ++i) info[p++] = mbuf[i];
        for (const char* s = " MB"; *s; ++s) info[p++] = *s;
        info[p] = '\0';
        Renderer::DrawText(2, 10, info, Theme::Text);

        Renderer::Present();
    }

    // ── 6. Register benchmarks ───────────────────────────────
    CpuBenchmark          cpuBench;
    MemoryBenchmarkSeq    memSeqBench;
    MemoryBenchmarkRandom memRndBench;
    PiBenchmarkScalar     piScalarBench;
    PiBenchmarkSimd       piSimdBench;

    BenchmarkRegistry::Register(&cpuBench);
    BenchmarkRegistry::Register(&memSeqBench);
    BenchmarkRegistry::Register(&memRndBench);
    BenchmarkRegistry::Register(&piScalarBench);
    BenchmarkRegistry::Register(&piSimdBench);

    if (gopOk) {
        char msg[64];
        int p = 0;
        char nbuf[8];
        StrCopy(nbuf, UintToStr(BenchmarkRegistry::Count()), sizeof(nbuf));
        for (int i = 0; nbuf[i]; ++i) msg[p++] = nbuf[i];
        for (const char* s = " benchmarks loaded."; *s; ++s) msg[p++] = *s;
        msg[p] = '\0';
        Renderer::DrawText(2, 12, msg, Theme::TextDim);
        Renderer::DrawText(2, 14, "Press any key to continue...", Theme::Warning);
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
