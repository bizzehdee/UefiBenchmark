#pragma once
// TUI manager: main menu, benchmark selection (with threading mode),
// run-count picker, progress display, and results/system-info screens.

#include "UefiTypes.h"
#include "Freestanding.h"
#include "BenchmarkResult.h"

class Tui {
public:
    void Run();   // enters the main menu loop (does not return)

private:
    void ShowMainMenu();
    void ShowBenchmarkSelection();
    void ShowRunCountPicker(const UINTN* indices, const bool* multiCore, UINTN count);
    void RunBenchmarks(const UINTN* indices, const bool* multiCore,
                       UINTN count, UINTN runs);
    void ShowResults();
    void ShowSystemInfo();

    // UI helpers
    int  DrawHeader(const char* title, int startRow = 0);
    int  DrawSeparator(int row);
    void DrawMenuItem(int row, const char* text, bool highlighted,
                      bool showCheckbox = false, bool isChecked = false);
    void DrawFooter(const char* text);
    void DrawProgressBar(int row, UINTN current, UINTN total);

    Vector<BenchmarkResult> mLastResults;
};
