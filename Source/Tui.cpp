// TUI manager: menus, benchmark selection, run-count picker,
// progress display, results table, and system info screen.

#include "Tui.h"
#include "Renderer.h"
#include "BenchmarkRegistry.h"
#include "BenchmarkRunner.h"
#include "IBenchmark.h"
#include "Statistics.h"
#include "Timer.h"
#include "SystemInfo.h"

// ── String builder helper (uses static buffer) ───────────────
static char sBuf[256];

static const char* Concat2(const char* a, const char* b) {
    int p = 0;
    if (a) for (int i = 0; a[i] && p < 254; ++i) sBuf[p++] = a[i];
    if (b) for (int i = 0; b[i] && p < 254; ++i) sBuf[p++] = b[i];
    sBuf[p] = '\0';
    return sBuf;
}

static const char* Concat3(const char* a, const char* b, const char* c) {
    int p = 0;
    if (a) for (int i = 0; a[i] && p < 254; ++i) sBuf[p++] = a[i];
    if (b) for (int i = 0; b[i] && p < 254; ++i) sBuf[p++] = b[i];
    if (c) for (int i = 0; c[i] && p < 254; ++i) sBuf[p++] = c[i];
    sBuf[p] = '\0';
    return sBuf;
}

// ── UI widget helpers ────────────────────────────────────────

int Tui::DrawHeader(const char* title, int startRow) {
    int cols = static_cast<int>(Renderer::Columns());

    char border[128];
    int bLen = cols < 127 ? cols : 127;
    for (int i = 0; i < bLen; ++i) border[i] = '=';
    border[bLen] = '\0';

    Renderer::DrawText(0, startRow, border, Theme::HeaderBorder);

    int pad = (cols - static_cast<int>(StrLen(title))) / 2;
    char line[128];
    int p = 0;
    for (int i = 0; i < pad && p < 126; ++i) line[p++] = ' ';
    for (int i = 0; title[i] && p < 126; ++i) line[p++] = title[i];
    while (p < bLen) line[p++] = ' ';
    line[p] = '\0';
    Renderer::DrawText(0, startRow + 1, line, Theme::HeaderText);

    Renderer::DrawText(0, startRow + 2, border, Theme::HeaderBorder);
    return startRow + 3;
}

int Tui::DrawSeparator(int row) {
    int cols = static_cast<int>(Renderer::Columns());
    char sep[128];
    int len = cols < 127 ? cols : 127;
    for (int i = 0; i < len; ++i) sep[i] = '-';
    sep[len] = '\0';
    Renderer::DrawText(0, row, sep, Theme::Separator);
    return row + 1;
}

void Tui::DrawMenuItem(int row, const char* text, bool highlighted,
                       bool showCheckbox, bool isChecked) {
    char line[128];
    int p = 0;

    line[p++] = ' '; line[p++] = ' ';
    line[p++] = highlighted ? '>' : ' ';
    line[p++] = ' ';

    if (showCheckbox) {
        line[p++] = '[';
        line[p++] = isChecked ? 'X' : ' ';
        line[p++] = ']';
        line[p++] = ' ';
    }

    if (text) for (int i = 0; text[i] && p < 126; ++i) line[p++] = text[i];

    int cols = static_cast<int>(Renderer::Columns());
    while (p < cols && p < 127) line[p++] = ' ';
    line[p] = '\0';

    if (highlighted)
        Renderer::DrawTextBg(0, row, line, Theme::HighlightTxt, Theme::Highlight);
    else
        Renderer::DrawTextBg(0, row, line, Theme::Text, Theme::Background);
}

void Tui::DrawFooter(const char* text) {
    int rows = static_cast<int>(Renderer::Rows());
    int cols = static_cast<int>(Renderer::Columns());
    DrawSeparator(rows - 3);
    Renderer::DrawTextBg(0, rows - 2,
        Renderer::Pad("(c) 2026 Darren Horrocks | https://github.com/bizzehdee/UefiBenchmark | MIT License", cols),
        Theme::TextDim, Theme::Background);
    Renderer::DrawTextBg(0, rows - 1, Renderer::Pad(text, cols),
                         Theme::Footer, Theme::Background);
}

void Tui::DrawProgressBar(int row, UINTN current, UINTN total) {
    if (total == 0) return;
    int barWidth = static_cast<int>(Renderer::Columns()) - 6;
    int filled = static_cast<int>((current * static_cast<UINT64>(barWidth)) / total);

    char bar[128];
    bar[0] = '[';
    for (int i = 0; i < barWidth && i < 125; ++i)
        bar[i + 1] = (i < filled) ? '#' : '.';
    bar[barWidth + 1] = ']';
    bar[barWidth + 2] = '\0';

    Renderer::DrawText(2, row, bar, Theme::Accent);
}

// ── Main entry ───────────────────────────────────────────────

void Tui::Run() {
    ShowMainMenu();
}

// ── Main Menu ────────────────────────────────────────────────

void Tui::ShowMainMenu() {
    const char* options[] = {
        "Run All Short Benchmarks",
        "Run All Long Benchmarks",
        "Select Benchmarks",
        "View Last Results",
        "System Info",
        "Change Resolution",
        "Shutdown"
    };
    constexpr int OPT_COUNT = 7;
    int cursor = 0;

    while (true) {
        Renderer::Clear();
        int row = DrawHeader("UEFI BENCHMARK SUITE");
        row++;
        Renderer::DrawText(2, row, "C++ Freestanding UEFI Benchmark Tool", Theme::TextDim);
        row += 2;

        int menuStart = row;
        for (int i = 0; i < OPT_COUNT; ++i)
            DrawMenuItem(menuStart + i, options[i], i == cursor);

        DrawFooter("[Up/Down] Navigate  [Enter] Select");
        Renderer::Present();

        EFI_INPUT_KEY key = Renderer::WaitKey();
        if (key.ScanCode == SCAN_UP)
            cursor = (cursor - 1 + OPT_COUNT) % OPT_COUNT;
        else if (key.ScanCode == SCAN_DOWN)
            cursor = (cursor + 1) % OPT_COUNT;
        else if (key.UnicodeChar == '\r' || key.UnicodeChar == '\n') {
            switch (cursor) {
                case 0: { // Run All Short
                    IBenchmark** all  = BenchmarkRegistry::GetAll();
                    UINTN total       = BenchmarkRegistry::Count();
                    UINTN indices[32]; bool mc[32]; UINTN cnt = 0;
                    for (UINTN i = 0; i < total && cnt < 32; ++i) {
                        if (all[i]->GetDurationClass() == DurationClass::Short) {
                            indices[cnt] = i;
                            mc[cnt] = (all[i]->GetThreadingMode() == ThreadingMode::MultiOnly);
                            ++cnt;
                        }
                    }
                    if (cnt) ShowRunCountPicker(indices, mc, cnt);
                    break;
                }
                case 1: { // Run All Long
                    IBenchmark** all  = BenchmarkRegistry::GetAll();
                    UINTN total       = BenchmarkRegistry::Count();
                    UINTN indices[32]; bool mc[32]; UINTN cnt = 0;
                    for (UINTN i = 0; i < total && cnt < 32; ++i) {
                        if (all[i]->GetDurationClass() == DurationClass::Long) {
                            indices[cnt] = i;
                            mc[cnt] = (all[i]->GetThreadingMode() != ThreadingMode::SingleOnly);
                            ++cnt;
                        }
                    }
                    if (cnt) ShowRunCountPicker(indices, mc, cnt);
                    break;
                }
                case 2: ShowBenchmarkSelection(); break;
                case 3: ShowResults();            break;
                case 4: ShowSystemInfo();         break;
                case 5: ShowResolutionPicker();   break;
                case 6: return;
            }
        }
    }
}

// ── Benchmark Selection ──────────────────────────────────────

void Tui::ShowBenchmarkSelection() {
    UINTN bmCount = BenchmarkRegistry::Count();
    if (bmCount == 0) {
        Renderer::Clear();
        DrawHeader("Select Benchmarks");
        Renderer::DrawText(2, 5, "No benchmarks registered.", Theme::Error);
        DrawFooter("[Any key] Back");
        Renderer::Present();
        Renderer::WaitKey();
        return;
    }

    IBenchmark** all = BenchmarkRegistry::GetAll();
    bool selected[32] = {};
    bool multiCore[32] = {};
    int cursor = 0;

    bool mpAvail = SystemInfo::HasMpServices();
    UINT32 apCount = 0;
    if (mpAvail) {
        UINT32 enabled = SystemInfo::GetEnabledProcessorCount();
        apCount = (enabled > 1) ? enabled - 1 : 0;
        if (apCount == 0) mpAvail = false;
    }

    for (UINTN i = 0; i < bmCount; ++i) {
        ThreadingMode tm = all[i]->GetThreadingMode();
        multiCore[i] = (tm == ThreadingMode::MultiOnly);
    }

    while (true) {
        Renderer::Clear();
        int row = DrawHeader("Select Benchmarks");
        row++;
        if (mpAvail) {
            char hint[80];
            int p = 0;
            for (const char* s = "Space:Toggle  Left/Right:Mode ("; *s; ++s) hint[p++] = *s;
            const char* ns = UintToStr(apCount);
            for (int j = 0; ns[j]; ++j) hint[p++] = ns[j];
            for (const char* s = " APs available)"; *s; ++s) hint[p++] = *s;
            hint[p] = '\0';
            Renderer::DrawText(2, row, hint, Theme::TextDim);
        } else {
            Renderer::DrawText(2, row, "Space:Toggle  (single-core only — no MP Services)", Theme::TextDim);
        }
        row += 2;

        int menuStart = row;

        // Emit grouped list with section headers
        DurationClass lastDc = DurationClass::Long; // force header on first item
        int vRow = menuStart;
        for (UINTN i = 0; i < bmCount; ++i) {
            DurationClass dc = all[i]->GetDurationClass();
            if (dc != lastDc || i == 0) {
                // Section header row
                const char* hdr = (dc == DurationClass::Short)
                    ? "  -- Short running --"
                    : "  -- Long running --";
                Renderer::DrawText(0, vRow, hdr, Theme::TextDim);
                ++vRow;
                lastDc = dc;
            }

            ThreadingMode tm = all[i]->GetThreadingMode();
            const char* name = all[i]->GetName();
            const char* cat  = all[i]->GetCategory();
            char label[128];
            int p = 0;
            for (int j = 0; name[j] && p < 36; ++j) label[p++] = name[j];
            while (p < 38) label[p++] = ' ';
            label[p++] = '[';
            for (int j = 0; cat[j] && p < 48; ++j) label[p++] = cat[j];
            label[p++] = ']';
            while (p < 52) label[p++] = ' ';

            const char* modeStr;
            if (!mpAvail || tm == ThreadingMode::SingleOnly)
                modeStr = "Single";
            else if (tm == ThreadingMode::MultiOnly)
                modeStr = "Multi";
            else
                modeStr = multiCore[i] ? "Multi" : "Single";

            for (int j = 0; modeStr[j] && p < 64; ++j) label[p++] = modeStr[j];

            if (tm == ThreadingMode::Either && mpAvail) {
                for (const char* s = " \x11\x10"; *s && p < 80; ++s) label[p++] = *s;
            }
            label[p] = '\0';

            DrawMenuItem(vRow, label, static_cast<int>(i) == cursor, true, selected[i]);
            ++vRow;
        }

        // Description of current item
        int descRow = vRow + 1;
        DrawSeparator(descRow - 1);
        Renderer::DrawText(2, descRow, all[cursor]->GetDescription(), Theme::TextDim);

        DrawFooter("[Up/Dn] Move [Space] Toggle [L/R] Mode [Enter] Run [Esc] Back");
        Renderer::Present();

        EFI_INPUT_KEY key = Renderer::WaitKey();
        if (key.ScanCode == SCAN_UP)
            cursor = (cursor - 1 + static_cast<int>(bmCount)) % static_cast<int>(bmCount);
        else if (key.ScanCode == SCAN_DOWN)
            cursor = (cursor + 1) % static_cast<int>(bmCount);
        else if (key.UnicodeChar == ' ')
            selected[cursor] = !selected[cursor];
        else if (key.ScanCode == SCAN_LEFT || key.ScanCode == SCAN_RIGHT) {
            ThreadingMode tm = all[cursor]->GetThreadingMode();
            if (mpAvail && tm == ThreadingMode::Either)
                multiCore[cursor] = !multiCore[cursor];
        }
        else if (key.ScanCode == SCAN_ESC)
            return;
        else if (key.UnicodeChar == '\r' || key.UnicodeChar == '\n') {
            UINTN indices[32];
            bool mc[32];
            UINTN count = 0;
            for (UINTN i = 0; i < bmCount; ++i) {
                if (selected[i]) {
                    indices[count] = i;
                    mc[count] = multiCore[i];
                    ++count;
                }
            }
            if (count > 0)
                ShowRunCountPicker(indices, mc, count);
            return;
        }
    }
}

// ── Run Count Picker ─────────────────────────────────────────

void Tui::ShowRunCountPicker(const UINTN* indices, const bool* multiCore,
                             UINTN count) {
    int runs = 1; // default 1 for long benchmarks

    while (true) {
        Renderer::Clear();
        int row = DrawHeader("Set Run Count");
        row++;
        Renderer::DrawText(2, row, "How many times to run each benchmark?", Theme::TextDim);
        row += 2;

        Renderer::DrawText(2, row, "Runs: ", Theme::Text);
        Renderer::DrawText(8, row, UintToStr(static_cast<UINT64>(runs)), Theme::Accent);
        row += 2;

        Renderer::DrawText(2, row, "[Left/Right] Adjust  [Enter] Start  [Esc] Cancel", Theme::TextDim);

        DrawFooter(Concat3("Running ", UintToStr(count), " benchmark(s)"));
        Renderer::Present();

        EFI_INPUT_KEY key = Renderer::WaitKey();
        if (key.ScanCode == SCAN_LEFT && runs > 1) --runs;
        else if (key.ScanCode == SCAN_RIGHT && runs < 10) ++runs;
        else if (key.ScanCode == SCAN_ESC) return;
        else if (key.UnicodeChar == '\r' || key.UnicodeChar == '\n') {
            RunBenchmarks(indices, multiCore, count, static_cast<UINTN>(runs));
            return;
        }
    }
}

// ── Run Benchmarks ───────────────────────────────────────────

void Tui::RunBenchmarks(const UINTN* indices, const bool* multiCore,
                        UINTN count, UINTN runs) {
    mLastResults = BenchmarkRunner::RunSelected(indices, multiCore, count, runs);
    ShowResults();
}

// ── Results ──────────────────────────────────────────────────

void Tui::ShowResults() {
    if (mLastResults.Empty()) {
        Renderer::Clear();
        DrawHeader("Benchmark Results");
        Renderer::DrawText(2, 5, "No results available. Run benchmarks first.", Theme::Warning);
        DrawFooter("[Any key] Back");
        Renderer::Present();
        Renderer::WaitKey();
        return;
    }

    int scroll = 0;
    int resultCount = static_cast<int>(mLastResults.Size());

    while (true) {
        Renderer::Clear();
        int row = DrawHeader("Benchmark Results");
        row++;

        // Column headers — grid is 100 cols
        Renderer::DrawText(2,  row, Renderer::Pad("Benchmark", 22),   Theme::Accent);
        Renderer::DrawText(24, row, Renderer::Pad("Cat", 6),           Theme::Accent);
        Renderer::DrawText(30, row, Renderer::Pad("Cores", 7),         Theme::Accent);
        Renderer::DrawText(37, row, Renderer::Pad("Avg(us)", 12),      Theme::Accent);
        Renderer::DrawText(49, row, Renderer::Pad("Min(us)", 12),      Theme::Accent);
        Renderer::DrawText(61, row, Renderer::Pad("Max(us)", 12),      Theme::Accent);
        Renderer::DrawText(73, row, Renderer::Pad("Score", 11),        Theme::Accent);
        Renderer::DrawText(84, row, Renderer::Pad("Unit", 9),          Theme::Accent);
        row++;
        row = DrawSeparator(row);

        int viewRows = static_cast<int>(Renderer::Rows()) - row - 8;
        for (int i = 0; i < viewRows && (scroll + i) < resultCount; ++i) {
            auto& r = mLastResults[static_cast<UINTN>(scroll + i)];

            Renderer::DrawText(2,  row, Renderer::Pad(r.Name, 22),     Theme::Text);
            Renderer::DrawText(24, row, Renderer::Pad(r.Category, 6),  Theme::Accent);

            char coreStr[16];
            if (r.MultiCore) {
                const char* n = UintToStr(r.CoreCount);
                int p = 0;
                for (int j = 0; n[j] && p < 10; ++j) coreStr[p++] = n[j];
                coreStr[p++] = 'x'; coreStr[p] = '\0';
            } else {
                coreStr[0] = '1'; coreStr[1] = '\0';
            }
            Renderer::DrawText(30, row, Renderer::Pad(coreStr, 7),
                               r.MultiCore ? Theme::Warning : Theme::TextDim);

            UINT64 avg = Stats::GetAverage(r.RunTimesUs);
            UINT64 mn  = Stats::GetMin(r.RunTimesUs);
            UINT64 mx  = Stats::GetMax(r.RunTimesUs);

            Renderer::DrawText(37, row, Renderer::Pad(UintToStr(avg), 12), Theme::Success);
            Renderer::DrawText(49, row, Renderer::Pad(UintToStr(mn),  12), Theme::TextDim);
            Renderer::DrawText(61, row, Renderer::Pad(UintToStr(mx),  12), Theme::TextDim);

            if (r.Score > 0) {
                Renderer::DrawText(73, row, Renderer::Pad(UintToStr(r.Score), 11), Theme::Accent);
                Renderer::DrawText(84, row, Renderer::Pad(r.Unit,             9),  Theme::TextDim);
            } else {
                Renderer::DrawText(73, row, Renderer::Pad("---", 11), Theme::TextDim);
            }

            // Error count indicator for integrity test
            if (r.ErrorCount > 0) {
                Renderer::DrawText(93, row, "ERR", Theme::Error);
            }

            row++;
        }

        row++;
        row = DrawSeparator(row);
        UINT64 totalTime = 0;
        for (UINTN i = 0; i < mLastResults.Size(); ++i)
            totalTime += mLastResults[i].TotalTimeUs;

        Renderer::DrawText(2, row,
            Concat3("Total suite time: ", UintToStr(totalTime / 1000), " ms"),
            Theme::Text);
        row++;

        // Integrity error summary
        UINT64 totalErrors = 0;
        for (UINTN i = 0; i < mLastResults.Size(); ++i)
            totalErrors += mLastResults[i].ErrorCount;
        if (totalErrors > 0) {
            Renderer::DrawText(2, row,
                Concat3("!! RAM ERRORS DETECTED: ", UintToStr(totalErrors), " mismatches !!"),
                Theme::Error);
            row++;
        }

        if (Timer::IsCalibrated()) {
            Renderer::DrawText(2, row,
                Concat3("TSC: ", UintToStr(Timer::CyclesPerUs()), " cycles/us"),
                Theme::TextDim);
            if (!Timer::HasInvariantTSC()) {
                row++;
                Renderer::DrawText(2, row,
                    "Warning: Invariant TSC not detected; timing may be imprecise",
                    Theme::Warning);
            }
        }

        DrawFooter("[Up/Down] Scroll  [Esc] Back");
        Renderer::Present();

        EFI_INPUT_KEY key = Renderer::WaitKey();
        if (key.ScanCode == SCAN_UP && scroll > 0) --scroll;
        else if (key.ScanCode == SCAN_DOWN && scroll + viewRows < resultCount) ++scroll;
        else if (key.ScanCode == SCAN_ESC) return;
    }
}

// ── Resolution Picker ─────────────────────────────────────────

void Tui::ShowResolutionPicker() {
    Renderer::ModeDesc modes[64];
    UINT32 modeCount = Renderer::ListModes(modes, 64);

    if (modeCount == 0) {
        Renderer::Clear();
        DrawHeader("Change Resolution");
        Renderer::DrawText(2, 5, "No additional modes available.", Theme::Warning);
        DrawFooter("[Any key] Back");
        Renderer::Present();
        Renderer::WaitKey();
        return;
    }

    // Find which entry matches the current mode
    UINT32 current = Renderer::CurrentModeIndex();
    int cursor = 0;
    for (UINT32 i = 0; i < modeCount; ++i) {
        if (modes[i].ModeIndex == current) { cursor = static_cast<int>(i); break; }
    }

    while (true) {
        Renderer::Clear();
        int row = DrawHeader("Change Resolution");
        row++;
        Renderer::DrawText(2, row, "Select a display resolution:", Theme::TextDim);
        row += 2;

        int menuStart = row;
        for (UINT32 i = 0; i < modeCount; ++i) {
            // Build label: "1920 x 1080  [current]"
            char label[80];
            int p = 0;
            const char* ws = UintToStr(modes[i].Width);
            for (int j = 0; ws[j]; ++j) label[p++] = ws[j];
            label[p++] = ' '; label[p++] = 'x'; label[p++] = ' ';
            const char* hs = UintToStr(modes[i].Height);
            for (int j = 0; hs[j]; ++j) label[p++] = hs[j];
            if (modes[i].ModeIndex == current) {
                for (const char* s = "  [current]"; *s && p < 78; ++s) label[p++] = *s;
            }
            label[p] = '\0';
            DrawMenuItem(menuStart + static_cast<int>(i), label,
                         static_cast<int>(i) == cursor);
        }

        row = menuStart + static_cast<int>(modeCount) + 1;
        DrawSeparator(row - 1);
        Renderer::DrawText(2, row,
            Concat3("Current: ", UintToStr(Renderer::ScreenWidth()),
                    Concat2("x", UintToStr(Renderer::ScreenHeight()))),
            Theme::TextDim);

        DrawFooter("[Up/Down] Navigate  [Enter] Apply  [Esc] Cancel");
        Renderer::Present();

        EFI_INPUT_KEY key = Renderer::WaitKey();
        if (key.ScanCode == SCAN_UP)
            cursor = (cursor - 1 + static_cast<int>(modeCount)) % static_cast<int>(modeCount);
        else if (key.ScanCode == SCAN_DOWN)
            cursor = (cursor + 1) % static_cast<int>(modeCount);
        else if (key.ScanCode == SCAN_ESC)
            return;
        else if (key.UnicodeChar == '\r' || key.UnicodeChar == '\n') {
            UINT32 chosen = modes[static_cast<UINT32>(cursor)].ModeIndex;
            if (chosen == current) return; // no change
            if (Renderer::SetModeByIndex(chosen)) {
                current = Renderer::CurrentModeIndex();
                // Redraw immediately after mode switch
                Renderer::Clear();
                DrawHeader("Change Resolution");
                Renderer::DrawText(2, 5, "Resolution applied.", Theme::Success);
                Renderer::DrawText(2, 7,
                    Concat3(UintToStr(Renderer::ScreenWidth()), "x",
                            UintToStr(Renderer::ScreenHeight())),
                    Theme::Text);
                DrawFooter("[Any key] Continue");
                Renderer::Present();
                Renderer::WaitKey();
            }
            return;
        }
    }
}

// ── System Info ──────────────────────────────────────────────

void Tui::ShowSystemInfo() {
    Renderer::Clear();
    int row = DrawHeader("System Information");
    row++;

    auto DrawInfoLine = [&](const char* label, const char* value) {
        Renderer::DrawText(2, row, Renderer::Pad(label, 22), Theme::Accent);
        Renderer::DrawText(24, row, value, Theme::Text);
        row++;
    };

    DrawInfoLine("CPU Vendor:",        SystemInfo::GetCpuVendor());
    DrawInfoLine("CPU Brand:",         SystemInfo::GetCpuBrand());
    DrawInfoLine("Logical CPUs:",      UintToStr(SystemInfo::GetCpuCoreCount()));
    DrawInfoLine("MP Services:",       SystemInfo::HasMpServices() ? "Available" : "Not available");
    if (SystemInfo::HasMpServices()) {
        UINT32 enabled = SystemInfo::GetEnabledProcessorCount();
        char apStr[32];
        int p = 0;
        const char* ns = UintToStr(enabled > 1 ? enabled - 1 : 0);
        for (int j = 0; ns[j]; ++j) apStr[p++] = ns[j];
        for (const char* s = " APs + BSP"; *s; ++s) apStr[p++] = *s;
        apStr[p] = '\0';
        DrawInfoLine("Processors:",    apStr);
    }
    DrawInfoLine("Available Memory:",  Concat2(UintToStr(SystemInfo::GetTotalMemoryMB()), " MB"));
    DrawInfoLine("Display:",           Concat3(UintToStr(Renderer::ScreenWidth()), "x",
                                               UintToStr(Renderer::ScreenHeight())));
    DrawInfoLine("Text Grid:",         Concat3(UintToStr(Renderer::Columns()), "x",
                                               UintToStr(Renderer::Rows())));
    DrawInfoLine("Timer Calibrated:",  Timer::IsCalibrated() ? "Yes" : "No");
    if (Timer::IsCalibrated())
        DrawInfoLine("Cycles/us:",     UintToStr(Timer::CyclesPerUs()));
    DrawInfoLine("Invariant TSC:",     Timer::HasInvariantTSC() ? "Yes" : "No");
    DrawInfoLine("Registered Tests:",  UintToStr(BenchmarkRegistry::Count()));

    row++;
    row = DrawSeparator(row);

    IBenchmark** all = BenchmarkRegistry::GetAll();
    UINTN count = BenchmarkRegistry::Count();
    Renderer::DrawText(2, row, "Benchmarks:", Theme::TextDim);
    row++;

    DurationClass lastDc = DurationClass::Long;
    for (UINTN i = 0; i < count; ++i) {
        DurationClass dc = all[i]->GetDurationClass();
        if (dc != lastDc || i == 0) {
            const char* hdr = (dc == DurationClass::Short)
                ? "  [Short running]"
                : "  [Long running]";
            Renderer::DrawText(2, row, hdr, Theme::TextDim);
            row++;
            lastDc = dc;
        }

        char line[128];
        int p = 0;
        line[p++] = ' '; line[p++] = ' '; line[p++] = ' '; line[p++] = '-'; line[p++] = ' ';
        const char* n = all[i]->GetName();
        for (int j = 0; n[j] && p < 50; ++j) line[p++] = n[j];
        while (p < 52) line[p++] = ' ';
        line[p++] = '[';
        const char* c = all[i]->GetCategory();
        for (int j = 0; c[j] && p < 65; ++j) line[p++] = c[j];
        line[p++] = ']';
        while (p < 70) line[p++] = ' ';
        ThreadingMode tm = all[i]->GetThreadingMode();
        const char* tmStr = (tm == ThreadingMode::SingleOnly) ? "Single" :
                            (tm == ThreadingMode::MultiOnly)  ? "Multi" : "Either";
        for (int j = 0; tmStr[j] && p < 80; ++j) line[p++] = tmStr[j];
        line[p] = '\0';
        Renderer::DrawText(2, row, line, Theme::Text);
        row++;
    }

    DrawFooter("[Any key] Back");
    Renderer::Present();
    Renderer::WaitKey();
}
