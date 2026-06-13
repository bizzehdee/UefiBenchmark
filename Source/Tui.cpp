// TUI manager: menus, benchmark selection, run-count picker,
// progress display, results table, and system info screen.

#include "Tui.h"
#include "AiScore.h"
#include "CoreSelection.h"
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

    Renderer::DrawText(0, startRow, border, Theme::Current().HeaderBorder);

    int pad = (cols - static_cast<int>(StrLen(title))) / 2;
    char line[128];
    int p = 0;
    for (int i = 0; i < pad && p < 126; ++i) line[p++] = ' ';
    for (int i = 0; title[i] && p < 126; ++i) line[p++] = title[i];
    while (p < bLen) line[p++] = ' ';
    line[p] = '\0';
    Renderer::DrawText(0, startRow + 1, line, Theme::Current().HeaderText);

    Renderer::DrawText(0, startRow + 2, border, Theme::Current().HeaderBorder);
    return startRow + 3;
}

int Tui::DrawSeparator(int row) {
    int cols = static_cast<int>(Renderer::Columns());
    char sep[128];
    int len = cols < 127 ? cols : 127;
    for (int i = 0; i < len; ++i) sep[i] = '-';
    sep[len] = '\0';
    Renderer::DrawText(0, row, sep, Theme::Current().Separator);
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
        Renderer::DrawTextBg(0, row, line, Theme::Current().HighlightTxt, Theme::Current().Highlight);
    else
        Renderer::DrawTextBg(0, row, line, Theme::Current().Text, Theme::Current().Background);
}

void Tui::DrawFooter(const char* text) {
    int rows = static_cast<int>(Renderer::Rows());
    int cols = static_cast<int>(Renderer::Columns());
    DrawSeparator(rows - 3);
    Renderer::DrawTextBg(0, rows - 2,
        Renderer::Pad("(c) 2026 Darren Horrocks | https://github.com/bizzehdee/UefiBenchmark | MIT License", cols),
        Theme::Current().TextDim, Theme::Current().Background);
    Renderer::DrawTextBg(0, rows - 1, Renderer::Pad(text, cols),
                         Theme::Current().Footer, Theme::Current().Background);
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

    Renderer::DrawText(2, row, bar, Theme::Current().Accent);
}

// ── Main entry ───────────────────────────────────────────────

void Tui::Run() {
    ShowMainMenu();
}

// ── Main Menu ────────────────────────────────────────────────

void Tui::ShowMainMenu() {
    // Static entries before the dynamic category block
    static const char* kPreCat[] = {
        "Run All Short Benchmarks",
        "Run All Long Benchmarks",
    };
    constexpr int kPreCount = 2;

    // Static entries after the dynamic category block
    static const char* kPostCat[] = {
        "Select Benchmarks",
        "View Last Results",
        "System Info",
        "Change Resolution",
        "Change Theme",
        "Select Cores",
        "Shutdown",
    };
    constexpr int kPostCount = 7;
    constexpr int kShutdownPost = 6; // index within kPostCat

    // Build dynamic category label storage (max 8 discovered categories)
    static char catLabels[8][48];
    UINT32 catCount = BenchmarkRegistry::GetCategoryCount();
    if (catCount > 8) catCount = 8;
    for (UINT32 i = 0; i < catCount; ++i) {
        int p = 0;
        for (const char* s = "Run All "; *s; ++s) catLabels[i][p++] = *s;
        const char* cn = BenchmarkRegistry::GetCategoryName(i);
        for (int j = 0; cn[j] && p < 46; ++j) catLabels[i][p++] = cn[j];
        catLabels[i][p] = '\0';
    }

    // Flat option array (max 2 + 8 + 7 = 17)
    const char* opts[17];
    for (int i = 0; i < kPreCount; ++i) opts[i] = kPreCat[i];
    for (UINT32 i = 0; i < catCount; ++i) opts[kPreCount + i] = catLabels[i];
    for (int i = 0; i < kPostCount; ++i) opts[kPreCount + catCount + i] = kPostCat[i];
    int totalOpts = kPreCount + (int)catCount + kPostCount;

    int cursor = 0;
    Renderer::FlushInput();
    while (true) {
        // Rebuild category block each iteration in case registry changed
        catCount = BenchmarkRegistry::GetCategoryCount();
        if (catCount > 8) catCount = 8;
        for (UINT32 i = 0; i < catCount; ++i) {
            int p = 0;
            for (const char* s = "Run All "; *s; ++s) catLabels[i][p++] = *s;
            const char* cn = BenchmarkRegistry::GetCategoryName(i);
            for (int j = 0; cn[j] && p < 46; ++j) catLabels[i][p++] = cn[j];
            catLabels[i][p] = '\0';
            opts[kPreCount + i] = catLabels[i];
        }
        for (int i = 0; i < kPostCount; ++i) opts[kPreCount + catCount + i] = kPostCat[i];
        totalOpts = kPreCount + (int)catCount + kPostCount;
        if (cursor >= totalOpts) cursor = totalOpts - 1;

        Renderer::Clear();
        int row = DrawHeader("UEFI BENCHMARK SUITE");
        row++;

        int menuStart = row;
        for (int i = 0; i < totalOpts; ++i)
            DrawMenuItem(menuStart + i, opts[i], i == cursor);

        DrawFooter("[Up/Down] Navigate  [Enter] Select");
        Renderer::Present();

        EFI_INPUT_KEY key = Renderer::WaitKey();
        if (key.ScanCode == SCAN_UP)
            cursor = (cursor - 1 + totalOpts) % totalOpts;
        else if (key.ScanCode == SCAN_DOWN)
            cursor = (cursor + 1) % totalOpts;
        else if (key.UnicodeChar == '\r' || key.UnicodeChar == '\n') {
            if (cursor == 0) {
                // Run All Short
                IBenchmark** all = BenchmarkRegistry::GetAll();
                UINTN total = BenchmarkRegistry::Count();
                UINTN indices[32]; RunMode modes[32]; UINTN cnt = 0;
                for (UINTN i = 0; i < total && cnt < 32; ++i) {
                    if (all[i]->GetDurationClass() == DurationClass::Short) {
                        indices[cnt] = i;
                        modes[cnt] = (all[i]->GetThreadingMode() == ThreadingMode::MultiOnly)
                                      ? RunMode::MultiCore : RunMode::SingleCore;
                        ++cnt;
                    }
                }
                if (cnt) ShowRunCountPicker(indices, modes, cnt);
            } else if (cursor == 1) {
                // Run All Long
                IBenchmark** all = BenchmarkRegistry::GetAll();
                UINTN total = BenchmarkRegistry::Count();
                UINTN indices[32]; RunMode modes[32]; UINTN cnt = 0;
                for (UINTN i = 0; i < total && cnt < 32; ++i) {
                    if (all[i]->GetDurationClass() == DurationClass::Long) {
                        indices[cnt] = i;
                        modes[cnt] = (all[i]->GetThreadingMode() != ThreadingMode::SingleOnly)
                                      ? RunMode::MultiCore : RunMode::SingleCore;
                        ++cnt;
                    }
                }
                if (cnt) ShowRunCountPicker(indices, modes, cnt);
            } else if (cursor >= kPreCount && cursor < kPreCount + (int)catCount) {
                // Run All <Category>
                int catIdx = cursor - kPreCount;
                const char* catName = BenchmarkRegistry::GetCategoryName((UINT32)catIdx);
                IBenchmark** all = BenchmarkRegistry::GetAll();
                UINTN total = BenchmarkRegistry::Count();
                UINTN indices[32]; RunMode modes[32]; UINTN cnt = 0;
                for (UINTN i = 0; i < total && cnt < 32; ++i) {
                    if (StrCmp(all[i]->GetCategory(), catName) != 0) continue;
                    indices[cnt] = i;
                    ThreadingMode tm = all[i]->GetThreadingMode();
                    if (tm == ThreadingMode::SingleOnly) {
                        modes[cnt] = RunMode::SingleCore;
                    } else if (tm == ThreadingMode::MultiOnly ||
                               all[i]->GetDurationClass() == DurationClass::Long) {
                        modes[cnt] = RunMode::MultiCore;
                    } else {
                        modes[cnt] = RunMode::SingleCore;
                    }
                    ++cnt;
                }
                if (cnt) {
                    mLastCategory = catName;
                    ShowRunCountPicker(indices, modes, cnt);
                    mLastCategory = nullptr;
                }
            } else {
                int postIdx = cursor - kPreCount - (int)catCount;
                switch (postIdx) {
                    case 0: ShowBenchmarkSelection(); break;
                    case 1: ShowResults();            break;
                    case 2: ShowSystemInfo();         break;
                    case 3: ShowResolutionPicker();   break;
                    case 4: ShowThemePicker();        break;
                    case 5: ShowCorePicker();         break;
                    case kShutdownPost: return;
                }
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
        Renderer::DrawText(2, 5, "No benchmarks registered.", Theme::Current().Error);
        DrawFooter("[Any key] Back");
        Renderer::Present();
        Renderer::WaitKey();
        return;
    }

    IBenchmark** all = BenchmarkRegistry::GetAll();
    bool selected[32] = {};
    RunMode modes[32] = {};
    int cursor = 0;

    Renderer::FlushInput();
    bool mpAvail = SystemInfo::HasMpServices();
    UINT32 apCount = 0;
    if (mpAvail) {
        UINT32 enabled = SystemInfo::GetEnabledProcessorCount();
        apCount = (enabled > 1) ? enabled - 1 : 0;
        if (apCount == 0) mpAvail = false;
    }

    for (UINTN i = 0; i < bmCount; ++i) {
        ThreadingMode tm = all[i]->GetThreadingMode();
        modes[i] = (tm == ThreadingMode::MultiOnly) ? RunMode::MultiCore : RunMode::SingleCore;
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
            Renderer::DrawText(2, row, hint, Theme::Current().TextDim);
        } else {
            Renderer::DrawText(2, row, "Space:Toggle  (single-core only — no MP Services)", Theme::Current().TextDim);
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
                Renderer::DrawText(0, vRow, hdr, Theme::Current().TextDim);
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
            else if (modes[i] == RunMode::CoreCycle)
                modeStr = "Cycle ";
            else if (modes[i] == RunMode::MultiCore)
                modeStr = "Multi ";
            else
                modeStr = "Single";

            for (int j = 0; modeStr[j] && p < 64; ++j) label[p++] = modeStr[j];

            if (mpAvail && tm != ThreadingMode::SingleOnly) {
                for (const char* s = " \x11\x10"; *s && p < 80; ++s) label[p++] = *s;
            }
            label[p] = '\0';

            DrawMenuItem(vRow, label, static_cast<int>(i) == cursor, true, selected[i]);
            ++vRow;
        }

        // Description of current item
        int descRow = vRow + 1;
        DrawSeparator(descRow - 1);
        Renderer::DrawText(2, descRow, all[cursor]->GetDescription(), Theme::Current().TextDim);

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
            if (mpAvail && tm != ThreadingMode::SingleOnly) {
                // Cycle: Single → Multi → Cycle → Single (Either)
                //        Multi  → Cycle → Multi         (MultiOnly)
                if (key.ScanCode == SCAN_RIGHT) {
                    if (tm == ThreadingMode::MultiOnly) {
                        modes[cursor] = (modes[cursor] == RunMode::MultiCore)
                                        ? RunMode::CoreCycle : RunMode::MultiCore;
                    } else {
                        if      (modes[cursor] == RunMode::SingleCore) modes[cursor] = RunMode::MultiCore;
                        else if (modes[cursor] == RunMode::MultiCore)  modes[cursor] = RunMode::CoreCycle;
                        else                                            modes[cursor] = RunMode::SingleCore;
                    }
                } else { // SCAN_LEFT
                    if (tm == ThreadingMode::MultiOnly) {
                        modes[cursor] = (modes[cursor] == RunMode::MultiCore)
                                        ? RunMode::CoreCycle : RunMode::MultiCore;
                    } else {
                        if      (modes[cursor] == RunMode::SingleCore) modes[cursor] = RunMode::CoreCycle;
                        else if (modes[cursor] == RunMode::CoreCycle)  modes[cursor] = RunMode::MultiCore;
                        else                                            modes[cursor] = RunMode::SingleCore;
                    }
                }
            }
        }
        else if (key.ScanCode == SCAN_ESC)
            return;
        else if (key.UnicodeChar == '\r' || key.UnicodeChar == '\n') {
            UINTN indices[32];
            RunMode selModes[32];
            UINTN count = 0;
            for (UINTN i = 0; i < bmCount; ++i) {
                if (selected[i]) {
                    indices[count]  = i;
                    selModes[count] = modes[i];
                    ++count;
                }
            }
            if (count > 0)
                ShowRunCountPicker(indices, selModes, count);
            return;
        }
    }
}

// ── Run Count Picker ─────────────────────────────────────────

void Tui::ShowRunCountPicker(const UINTN* indices, const RunMode* modes,
                             UINTN count) {
    int runs = 1;
    bool coreCycleAllCores = true;  // default: cycle all APs

    // Determine whether any selected benchmark is CoreCycle
    bool hasCoreCycle = false;
    for (UINTN i = 0; i < count; ++i)
        if (modes[i] == RunMode::CoreCycle) { hasCoreCycle = true; break; }

    // cursor: 0 = Runs row, 1 = Scope row (only when hasCoreCycle)
    int pickerCursor = 0;
    const int pickerRows = hasCoreCycle ? 2 : 1;

    Renderer::FlushInput();
    while (true) {
        Renderer::Clear();
        int row = DrawHeader("Set Run Count");
        row++;

        const char* prompt = hasCoreCycle
            ? "How many times to run on each core?"
            : "How many times to run each benchmark?";
        Renderer::DrawText(2, row, prompt, Theme::Current().TextDim);
        row += 2;

        // Runs row
        bool runsHi = (pickerCursor == 0);
        {
            char line[64];
            int p = 0;
            line[p++] = runsHi ? '>' : ' '; line[p++] = ' ';
            for (const char* s = "Runs:  "; *s; ++s) line[p++] = *s;
            const char* ns = UintToStr(static_cast<UINT64>(runs));
            for (int j = 0; ns[j]; ++j) line[p++] = ns[j];
            line[p] = '\0';
            Renderer::DrawText(2, row, line,
                runsHi ? Theme::Current().Accent : Theme::Current().Text);
        }
        row++;

        // Core scope row (CoreCycle only)
        if (hasCoreCycle) {
            row++;
            bool scopeHi = (pickerCursor == 1);
            char line[80];
            int p = 0;
            line[p++] = scopeHi ? '>' : ' '; line[p++] = ' ';
            for (const char* s = "Core scope:  "; *s; ++s) line[p++] = *s;
            const char* sv = coreCycleAllCores ? "[All Cores]" : "[Selected]";
            for (int j = 0; sv[j]; ++j) line[p++] = sv[j];
            line[p] = '\0';
            Renderer::DrawText(2, row, line,
                scopeHi ? Theme::Current().Accent : Theme::Current().Text);
            row++;
        }

        row++;
        Renderer::DrawText(2, row,
            "[Up/Dn] Field  [Left/Right] Adjust  [Enter] Start  [Esc] Cancel",
            Theme::Current().TextDim);

        DrawFooter(Concat3("Running ", UintToStr(count), " benchmark(s)"));
        Renderer::Present();

        EFI_INPUT_KEY key = Renderer::WaitKey();
        if (key.ScanCode == SCAN_UP)
            pickerCursor = (pickerCursor - 1 + pickerRows) % pickerRows;
        else if (key.ScanCode == SCAN_DOWN)
            pickerCursor = (pickerCursor + 1) % pickerRows;
        else if (key.ScanCode == SCAN_LEFT || key.ScanCode == SCAN_RIGHT) {
            if (pickerCursor == 0) {
                if (key.ScanCode == SCAN_LEFT  && runs > 1)  --runs;
                if (key.ScanCode == SCAN_RIGHT && runs < 10) ++runs;
            } else {
                coreCycleAllCores = !coreCycleAllCores;
            }
        } else if (key.ScanCode == SCAN_ESC) {
            return;
        } else if (key.UnicodeChar == '\r' || key.UnicodeChar == '\n') {
            RunBenchmarks(indices, modes, count, static_cast<UINTN>(runs),
                          coreCycleAllCores);
            return;
        }
    }
}

// ── Run Benchmarks ───────────────────────────────────────────

void Tui::RunBenchmarks(const UINTN* indices, const RunMode* modes,
                        UINTN count, UINTN runs, bool coreCycleAllCores) {
    if (mLastCategory) {
        mLastResults = BenchmarkRunner::RunCategory(mLastCategory, runs);
        const char* cat = mLastCategory;
        mLastCategory = nullptr;
        ShowCategoryResults(cat);
    } else {
        mLastResults = BenchmarkRunner::RunSelected(indices, modes, count, runs,
                                                    coreCycleAllCores);
        ShowResults();
    }
}

// ── Category Results ─────────────────────────────────────────

void Tui::ShowCategoryResults(const char* category) {
    if (mLastResults.Empty()) { ShowResults(); return; }

    // Build header title: "<Category> BENCHMARK RESULTS"
    static char hdrTitle[80];
    {
        int p = 0;
        for (const char* s = category; *s && p < 50; ++s) hdrTitle[p++] = *s;
        for (const char* s = " BENCHMARK RESULTS"; *s && p < 78; ++s) hdrTitle[p++] = *s;
        hdrTitle[p] = '\0';
    }

    Renderer::FlushInput();
    while (true) {
        Renderer::Clear();
        int row = DrawHeader(hdrTitle);
        row++;

        // Column headers
        Renderer::DrawText(2,  row, Renderer::Pad("Benchmark",  36), Theme::Current().Accent);
        Renderer::DrawText(38, row, Renderer::Pad("Score",      14), Theme::Current().Accent);
        Renderer::DrawText(52, row, Renderer::Pad("Unit",       12), Theme::Current().Accent);
        Renderer::DrawText(64, row, "Avg time (ms)",              Theme::Current().Accent);
        row++;
        row = DrawSeparator(row);

        // Accumulate weighted composite while rendering rows
        UINT64 weightedSum   = 0;
        UINT64 totalWeight   = 0;
        bool   sameUnit      = true;
        const char* firstUnit = nullptr;

        for (UINTN i = 0; i < mLastResults.Size(); ++i) {
            auto& r = mLastResults[i];
            if (StrCmp(r.Category, category) != 0) continue;

            Renderer::DrawText(2, row, Renderer::Pad(r.Name, 36), Theme::Current().Text);

            if (r.ErrorCount > 0) {
                static char errStr[32];
                int p = 0;
                for (const char* s = "ERRORS: "; *s; ++s) errStr[p++] = *s;
                const char* ns = UintToStr(r.ErrorCount);
                for (int j = 0; ns[j]; ++j) errStr[p++] = ns[j];
                errStr[p] = '\0';
                Renderer::DrawText(38, row, Renderer::Pad(errStr, 14), Theme::Current().Error);
            } else if (r.Score > 0) {
                Renderer::DrawText(38, row, Renderer::Pad(UintToStr(r.Score), 14), Theme::Current().Success);
                Renderer::DrawText(52, row, Renderer::Pad(r.Unit, 12),             Theme::Current().TextDim);
                if (r.IncludeInScore) {
                    if (!firstUnit) firstUnit = r.Unit;
                    else if (StrCmp(firstUnit, r.Unit) != 0) sameUnit = false;
                    weightedSum  += r.Score * r.CategoryWeight;
                    totalWeight  += r.CategoryWeight;
                }
            } else if (!r.IncludeInScore) {
                Renderer::DrawText(38, row, Renderer::Pad("--", 14), Theme::Current().TextDim);
                Renderer::DrawText(52, row, "[pass/fail]",            Theme::Current().TextDim);
            } else {
                Renderer::DrawText(38, row, Renderer::Pad("--", 14), Theme::Current().TextDim);
            }

            UINT64 avgUs = Stats::GetAverage(r.RunTimesUs);
            Renderer::DrawText(64, row, Renderer::Pad(UintToStr(avgUs / 1000), 12), Theme::Current().TextDim);
            row++;
        }

        row++;
        row = DrawSeparator(row);

        // Weighted composite score
        UINT64 composite = (totalWeight > 0) ? weightedSum / totalWeight : 0;
        if (composite > 0) {
            const char* compLabel = (totalWeight != (UINT64)100 * (totalWeight / 100))
                                    ? "Weighted Score:"
                                    : "Composite Score:";
            Renderer::DrawText(2,  row, compLabel, Theme::Current().Accent);
            Renderer::DrawText(38, row, Renderer::Pad(UintToStr(composite), 14), Theme::Current().Accent);
            if (sameUnit && firstUnit)
                Renderer::DrawText(52, row, Renderer::Pad(firstUnit, 12), Theme::Current().TextDim);
            else
                Renderer::DrawText(52, row, "(mixed units)", Theme::Current().TextDim);
            row++;
        }

        // AI-specific LLM performance estimates
        if (composite > 0 && StrCmp(category, "AI") == 0) {
            row++;
            Renderer::DrawText(2, row, "LLM Performance Estimate (llama.cpp, approx.):",
                               Theme::Current().Accent);
            row++;
            Renderer::DrawText(4,  row, "Model:",   Theme::Current().TextDim);
            Renderer::DrawText(20, row, "7B Q4",    Theme::Current().TextDim);
            Renderer::DrawText(32, row, "14B Q4",   Theme::Current().TextDim);
            Renderer::DrawText(44, row, "32B Q4",   Theme::Current().TextDim);
            row++;

            // Format X.Y tok/s using ×10 fixed-point arithmetic
            auto fmtTok = [](char* buf, UINT64 score, UINT32 ref_x10) -> const char* {
                UINT64 t = score * ref_x10 / 1000;
                int p = 0;
                const char* n = UintToStr(t / 10);
                for (int i = 0; n[i]; ++i) buf[p++] = n[i];
                buf[p++] = '.';
                buf[p++] = '0' + (char)(t % 10);
                for (const char* s = " t/s"; *s; ++s) buf[p++] = *s;
                buf[p] = '\0';
                return buf;
            };

            static char t7[16], t14[16], t32[16];
            Renderer::DrawText(4,  row, "Est:",
                               Theme::Current().Text);
            Renderer::DrawText(20, row,
                fmtTok(t7,  composite, AI_LLM_7B_Q4_TOKS_X10),  Theme::Current().Success);
            Renderer::DrawText(32, row,
                fmtTok(t14, composite, AI_LLM_14B_Q4_TOKS_X10), Theme::Current().Success);
            Renderer::DrawText(44, row,
                fmtTok(t32, composite, AI_LLM_32B_Q4_TOKS_X10), Theme::Current().Success);
            row++;
        }

        row++;
        // Total time
        UINT64 totalUs = 0;
        for (UINTN i = 0; i < mLastResults.Size(); ++i) totalUs += mLastResults[i].TotalTimeUs;
        Renderer::DrawText(2, row,
            Concat3("Total suite time: ", UintToStr(totalUs / 1000), " ms"),
            Theme::Current().Text);

        DrawFooter("[Enter] Detailed View  [Esc] Back");
        Renderer::Present();

        EFI_INPUT_KEY key = Renderer::WaitKey();
        if (key.ScanCode == SCAN_ESC) return;
        if (key.UnicodeChar == '\r' || key.UnicodeChar == '\n') {
            ShowResults();
            return;
        }
    }
}

// ── Results ──────────────────────────────────────────────────

void Tui::ShowResults() {
    if (mLastResults.Empty()) {
        Renderer::Clear();
        DrawHeader("Benchmark Results");
        Renderer::DrawText(2, 5, "No results available. Run benchmarks first.", Theme::Current().Warning);
        DrawFooter("[Any key] Back");
        Renderer::Present();
        Renderer::WaitKey();
        return;
    }

    // Build flat display list: each entry is (resultIdx, coreIdx)
    // coreIdx == -1 → summary row; coreIdx >= 0 → per-core sub-row
    struct DisplayRow { int resIdx; int coreIdx; };
    static DisplayRow flat[32 + 32 * MAX_CYCLE_CORES];
    int flatCount = 0;
    for (int ri = 0; ri < static_cast<int>(mLastResults.Size()); ++ri) {
        flat[flatCount++] = { ri, -1 };
        auto& r = mLastResults[static_cast<UINTN>(ri)];
        if (r.RunModeUsed == RunMode::CoreCycle) {
            for (UINT32 c = 0; c < r.PerCoreSampleCount && flatCount < 32 + 32 * (int)MAX_CYCLE_CORES; ++c)
                flat[flatCount++] = { ri, static_cast<int>(c) };
        }
    }

    int scroll = 0;
    int resultCount = flatCount;

    Renderer::FlushInput();
    while (true) {
        Renderer::Clear();
        int row = DrawHeader("Benchmark Results");
        row++;

        // Column headers — grid is 100 cols
        Renderer::DrawText(2,  row, Renderer::Pad("Benchmark", 22),   Theme::Current().Accent);
        Renderer::DrawText(24, row, Renderer::Pad("Cat", 6),           Theme::Current().Accent);
        Renderer::DrawText(30, row, Renderer::Pad("Cores", 7),         Theme::Current().Accent);
        Renderer::DrawText(37, row, Renderer::Pad("Avg(us)", 12),      Theme::Current().Accent);
        Renderer::DrawText(49, row, Renderer::Pad("Min(us)", 12),      Theme::Current().Accent);
        Renderer::DrawText(61, row, Renderer::Pad("Max(us)", 12),      Theme::Current().Accent);
        Renderer::DrawText(73, row, Renderer::Pad("Score", 11),        Theme::Current().Accent);
        Renderer::DrawText(84, row, Renderer::Pad("Unit", 9),          Theme::Current().Accent);
        row++;
        row = DrawSeparator(row);

        int viewRows = static_cast<int>(Renderer::Rows()) - row - 8;
        for (int i = 0; i < viewRows && (scroll + i) < resultCount; ++i) {
            auto& dr = flat[scroll + i];
            auto& r  = mLastResults[static_cast<UINTN>(dr.resIdx)];

            if (dr.coreIdx < 0) {
                // ── Summary row ──
                Renderer::DrawText(2,  row, Renderer::Pad(r.Name, 22),     Theme::Current().Text);
                Renderer::DrawText(24, row, Renderer::Pad(r.Category, 6),  Theme::Current().Accent);

                char coreStr[16];
                if (r.RunModeUsed == RunMode::CoreCycle) {
                    // "CC8" style label
                    int p = 0;
                    coreStr[p++] = 'C'; coreStr[p++] = 'C';
                    const char* n = UintToStr(r.CoreCount);
                    for (int j = 0; n[j] && p < 14; ++j) coreStr[p++] = n[j];
                    coreStr[p] = '\0';
                } else if (r.MultiCore) {
                    const char* n = UintToStr(r.CoreCount);
                    int p = 0;
                    for (int j = 0; n[j] && p < 10; ++j) coreStr[p++] = n[j];
                    coreStr[p++] = 'x'; coreStr[p] = '\0';
                } else {
                    coreStr[0] = '1'; coreStr[1] = '\0';
                }
                Renderer::DrawText(30, row, Renderer::Pad(coreStr, 7),
                    (r.RunModeUsed == RunMode::CoreCycle || r.MultiCore)
                        ? Theme::Current().Warning : Theme::Current().TextDim);

                UINT64 avg = Stats::GetAverage(r.RunTimesUs);
                UINT64 mn  = Stats::GetMin(r.RunTimesUs);
                UINT64 mx  = Stats::GetMax(r.RunTimesUs);

                Renderer::DrawText(37, row, Renderer::Pad(UintToStr(avg), 12), Theme::Current().Success);
                Renderer::DrawText(49, row, Renderer::Pad(UintToStr(mn),  12), Theme::Current().TextDim);
                Renderer::DrawText(61, row, Renderer::Pad(UintToStr(mx),  12), Theme::Current().TextDim);

                if (r.Score > 0) {
                    Renderer::DrawText(73, row, Renderer::Pad(UintToStr(r.Score), 11), Theme::Current().Accent);
                    Renderer::DrawText(84, row, Renderer::Pad(r.Unit,             9),  Theme::Current().TextDim);
                } else {
                    Renderer::DrawText(73, row, Renderer::Pad("---", 11), Theme::Current().TextDim);
                }

                if (r.ErrorCount > 0)
                    Renderer::DrawText(93, row, "ERR", Theme::Current().Error);

            } else {
                // ── Per-core sub-row ──
                int c = dr.coreIdx;

                // Look up package/core/thread from CoreSelection roster
                char label[32];
                label[0] = '\0';
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

                // Detect outlier: >5% deviation from median (aggregate score)
                bool outlier = false;
                if (r.Score > 0) {
                    UINT64 sc = r.PerCoreScore[static_cast<UINT32>(c)];
                    UINT64 dev = (sc > r.Score) ? sc - r.Score : r.Score - sc;
                    outlier = (dev * 100 / r.Score) > 5;
                }

                Color nameCol = outlier ? Theme::Current().Warning : Theme::Current().TextDim;
                Renderer::DrawText(2, row, Renderer::Pad(label, 22), nameCol);

                // Score columns: min / avg / max score for this core (if available),
                // otherwise fall back to showing the per-core elapsed time.
                UINT64 sc  = r.PerCoreScore[static_cast<UINT32>(c)];
                UINT64 mn  = r.PerCoreMin[static_cast<UINT32>(c)];
                UINT64 mx  = r.PerCoreMax[static_cast<UINT32>(c)];
                bool hasScore = (sc > 0 || mn > 0 || mx > 0);
                if (hasScore) {
                    Renderer::DrawText(73, row, Renderer::Pad(UintToStr(sc), 11),
                                       outlier ? Theme::Current().Warning : Theme::Current().Accent);
                    Renderer::DrawText(49, row, Renderer::Pad(UintToStr(mn), 12), Theme::Current().TextDim);
                    Renderer::DrawText(61, row, Renderer::Pad(UintToStr(mx), 12), Theme::Current().TextDim);
                    Renderer::DrawText(84, row, Renderer::Pad(r.Unit, 9), Theme::Current().TextDim);
                } else {
                    UINT64 t = r.PerCoreTimeUs[static_cast<UINT32>(c)];
                    Renderer::DrawText(37, row, Renderer::Pad(UintToStr(t), 12), Theme::Current().Success);
                    Renderer::DrawText(84, row, Renderer::Pad("us", 9), Theme::Current().TextDim);
                }
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
            Theme::Current().Text);
        row++;

        // Integrity error summary
        UINT64 totalErrors = 0;
        for (UINTN i = 0; i < mLastResults.Size(); ++i)
            totalErrors += mLastResults[i].ErrorCount;
        if (totalErrors > 0) {
            Renderer::DrawText(2, row,
                Concat3("!! RAM ERRORS DETECTED: ", UintToStr(totalErrors), " mismatches !!"),
                Theme::Current().Error);
            row++;
        }

        if (Timer::IsCalibrated()) {
            Renderer::DrawText(2, row,
                Concat3("TSC: ", UintToStr(Timer::CyclesPerUs()), " cycles/us"),
                Theme::Current().TextDim);
            if (!Timer::HasInvariantTSC()) {
                row++;
                Renderer::DrawText(2, row,
                    "Warning: Invariant TSC not detected; timing may be imprecise",
                    Theme::Current().Warning);
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

// ── Theme Picker ─────────────────────────────────────────────

void Tui::ShowThemePicker() {
    struct ThemeEntry { ThemeId id; const char* name; };
    constexpr ThemeEntry kThemes[] = {
        { ThemeId::Dark,            "Dark (default)"     },
        { ThemeId::Light,           "Light"              },
        { ThemeId::HighContrastDark,"High-contrast dark" },
    };
    constexpr int kCount = 3;

    // Start cursor on the currently active theme
    int cursor = 0;
    for (int i = 0; i < kCount; ++i) {
        if (kThemes[i].id == Theme::CurrentId()) { cursor = i; break; }
    }

    Renderer::FlushInput();
    while (true) {
        Renderer::Clear();
        int row = DrawHeader("Change Theme");
        row++;
        Renderer::DrawText(2, row, "Select a colour theme:", Theme::Current().TextDim);
        row += 2;

        int menuStart = row;
        for (int i = 0; i < kCount; ++i) {
            char label[64];
            int p = 0;
            for (int j = 0; kThemes[i].name[j] && p < 60; ++j) label[p++] = kThemes[i].name[j];
            if (kThemes[i].id == Theme::CurrentId()) {
                for (const char* s = "  [current]"; *s && p < 62; ++s) label[p++] = *s;
            }
            label[p] = '\0';
            DrawMenuItem(menuStart + i, label, i == cursor);
        }

        row = menuStart + kCount + 1;
        DrawSeparator(row - 1);
        Renderer::DrawText(2, row,
            Concat2("Active: ", Theme::CurrentName()),
            Theme::Current().TextDim);

        DrawFooter("[Up/Down] Navigate  [Enter] Apply  [Esc] Cancel");
        Renderer::Present();

        EFI_INPUT_KEY key = Renderer::WaitKey();
        if (key.ScanCode == SCAN_UP)
            cursor = (cursor - 1 + kCount) % kCount;
        else if (key.ScanCode == SCAN_DOWN)
            cursor = (cursor + 1) % kCount;
        else if (key.ScanCode == SCAN_ESC)
            return;
        else if (key.UnicodeChar == '\r' || key.UnicodeChar == '\n') {
            Theme::Set(kThemes[cursor].id);
            return; // caller's next Clear()/Present() picks up new colours
        }
    }
}

// ── Resolution Picker ─────────────────────────────────────────

void Tui::ShowResolutionPicker() {
    Renderer::ModeDesc modes[64];
    UINT32 modeCount = Renderer::ListModes(modes, 64);

    if (modeCount == 0) {
        Renderer::Clear();
        DrawHeader("Change Resolution");
        Renderer::DrawText(2, 5, "No additional modes available.", Theme::Current().Warning);
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

    Renderer::FlushInput();
    while (true) {
        Renderer::Clear();
        int row = DrawHeader("Change Resolution");
        row++;
        Renderer::DrawText(2, row, "Select a display resolution:", Theme::Current().TextDim);
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
            Theme::Current().TextDim);

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
                Renderer::DrawText(2, 5, "Resolution applied.", Theme::Current().Success);
                Renderer::DrawText(2, 7,
                    Concat3(UintToStr(Renderer::ScreenWidth()), "x",
                            UintToStr(Renderer::ScreenHeight())),
                    Theme::Current().Text);
                DrawFooter("[Any key] Continue");
                Renderer::Present();
                Renderer::WaitKey();
            }
            return;
        }
    }
}

// ── Core Picker ───────────────────────────────────────────────

void Tui::ShowCorePicker() {
    if (!SystemInfo::HasMpServices() || CoreSelection::Count() == 0) {
        Renderer::Clear();
        DrawHeader("Select Cores");
        Renderer::DrawText(2, 5, "MP Services not available; single-core only.", Theme::Current().Warning);
        DrawFooter("[Any key] Back");
        Renderer::Present();
        Renderer::WaitKey();
        return;
    }

    UINT32 apCount = CoreSelection::Count();
    CoreSelection::ApInfo* roster = CoreSelection::GetAll();
    int cursor = 0;

    Renderer::FlushInput();
    while (true) {
        Renderer::Clear();
        int row = DrawHeader("Select Cores");
        row++;

        // Count selected for header info
        UINT32 selCount = CoreSelection::SelectedCount();
        {
            char info[80];
            int p = 0;
            const char* ns = UintToStr(selCount);
            for (int j = 0; ns[j]; ++j) info[p++] = ns[j];
            for (const char* s = " of "; *s; ++s) info[p++] = *s;
            ns = UintToStr(apCount);
            for (int j = 0; ns[j]; ++j) info[p++] = ns[j];
            for (const char* s = " APs selected (BSP always runs on core 0)"; *s; ++s) info[p++] = *s;
            info[p] = '\0';
            Renderer::DrawText(2, row, info, Theme::Current().TextDim);
        }
        row += 2;

        int menuStart = row;
        int viewRows = static_cast<int>(Renderer::Rows()) - row - 6;
        int scrollOff = 0;
        if (cursor >= viewRows) scrollOff = cursor - viewRows + 1;

        for (int i = 0; i < viewRows && (scrollOff + i) < static_cast<int>(apCount); ++i) {
            auto& ap = roster[scrollOff + i];
            char label[80];
            int p = 0;
            for (const char* s = "AP "; *s; ++s) label[p++] = *s;
            const char* ns = UintToStr(ap.ProcIndex);
            for (int j = 0; ns[j] && p < 10; ++j) label[p++] = ns[j];
            while (p < 6) label[p++] = ' ';
            for (const char* s = "  Pkg:"; *s; ++s) label[p++] = *s;
            ns = UintToStr(ap.Package);
            for (int j = 0; ns[j] && p < 70; ++j) label[p++] = ns[j];
            for (const char* s = "  Core:"; *s; ++s) label[p++] = *s;
            ns = UintToStr(ap.Core);
            for (int j = 0; ns[j] && p < 70; ++j) label[p++] = ns[j];
            for (const char* s = "  Thr:"; *s; ++s) label[p++] = *s;
            ns = UintToStr(ap.Thread);
            for (int j = 0; ns[j] && p < 70; ++j) label[p++] = ns[j];
            if (!ap.Available) {
                for (const char* s = "  [disabled by firmware]"; *s && p < 78; ++s) label[p++] = *s;
            }
            label[p] = '\0';
            DrawMenuItem(menuStart + i, label,
                         (scrollOff + i) == cursor,
                         true, ap.Selected);
        }

        DrawFooter("[Up/Down] Move  [Space] Toggle  [A]ll  [P]hysical  [1]PerPkg  [Esc] Done");
        Renderer::Present();

        EFI_INPUT_KEY key = Renderer::WaitKey();
        if (key.ScanCode == SCAN_UP && cursor > 0)
            --cursor;
        else if (key.ScanCode == SCAN_DOWN &&
                 cursor < static_cast<int>(apCount) - 1)
            ++cursor;
        else if (key.UnicodeChar == ' ' && roster[cursor].Available)
            roster[cursor].Selected = !roster[cursor].Selected;
        else if (key.UnicodeChar == 'a' || key.UnicodeChar == 'A')
            CoreSelection::SelectAll();
        else if (key.UnicodeChar == 'p' || key.UnicodeChar == 'P')
            CoreSelection::SelectPhysicalCoresOnly();
        else if (key.UnicodeChar == '1')
            CoreSelection::SelectOnePerPackage();
        else if (key.ScanCode == SCAN_ESC)
            return;
    }
}

// ── System Info ──────────────────────────────────────────────

void Tui::ShowSystemInfo() {
    Renderer::Clear();
    int row = DrawHeader("System Information");
    row++;

    auto DrawInfoLine = [&](const char* label, const char* value) {
        Renderer::DrawText(2, row, Renderer::Pad(label, 22), Theme::Current().Accent);
        Renderer::DrawText(24, row, value, Theme::Current().Text);
        row++;
    };

    auto FormatCacheKB = [](UINT32 kb) -> const char* {
        if (kb >= 1024) return Concat2(UintToStr(kb / 1024), " MB");
        return Concat2(UintToStr(kb), " KB");
    };

    DrawInfoLine("CPU Vendor:",        SystemInfo::GetCpuVendor());
    DrawInfoLine("CPU Brand:",         SystemInfo::GetCpuBrand());
    DrawInfoLine("CPU Stepping:",      UintToStr(SystemInfo::GetCpuStepping()));
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
    if (SystemInfo::GetL1DataCacheKB() > 0)
        DrawInfoLine("L1D Cache:",     FormatCacheKB(SystemInfo::GetL1DataCacheKB()));
    if (SystemInfo::GetL1InstCacheKB() > 0)
        DrawInfoLine("L1I Cache:",     FormatCacheKB(SystemInfo::GetL1InstCacheKB()));
    if (SystemInfo::GetL2CacheKB() > 0)
        DrawInfoLine("L2 Cache:",      FormatCacheKB(SystemInfo::GetL2CacheKB()));
    DrawInfoLine("L3 Cache:",          SystemInfo::GetL3CacheKB() > 0
                                           ? FormatCacheKB(SystemInfo::GetL3CacheKB())
                                           : "None");
    row++;
    DrawInfoLine("Available Memory:",  Concat2(UintToStr(SystemInfo::GetTotalMemoryMB()), " MB"));
    if (SystemInfo::GetMemorySpeedMHz() > 0)
        DrawInfoLine("Memory Speed:",  Concat2(UintToStr(SystemInfo::GetMemorySpeedMHz()), " MHz"));
    if (SystemInfo::GetMemoryConfiguredSpeedMHz() > 0 &&
        SystemInfo::GetMemoryConfiguredSpeedMHz() != SystemInfo::GetMemorySpeedMHz())
        DrawInfoLine("Configured:",    Concat2(UintToStr(SystemInfo::GetMemoryConfiguredSpeedMHz()), " MHz"));
    if (SystemInfo::GetMemoryChannelCount() > 0)
        DrawInfoLine("Mem Channels:",  UintToStr(SystemInfo::GetMemoryChannelCount()));
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
    Renderer::DrawText(2, row, "Benchmarks:", Theme::Current().TextDim);
    row++;

    DurationClass lastDc = DurationClass::Long;
    for (UINTN i = 0; i < count; ++i) {
        DurationClass dc = all[i]->GetDurationClass();
        if (dc != lastDc || i == 0) {
            const char* hdr = (dc == DurationClass::Short)
                ? "  [Short running]"
                : "  [Long running]";
            Renderer::DrawText(2, row, hdr, Theme::Current().TextDim);
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
        Renderer::DrawText(2, row, line, Theme::Current().Text);
        row++;
    }

    DrawFooter("[Any key] Back");
    Renderer::Present();
    Renderer::WaitKey();
}
