#pragma once
// Shared TUI widget helpers used by multiple screens. These are the reusable,
// non-chrome primitives (the header/footer chrome stays in Tui). Previously
// these were Tui members / file-statics inside the monolithic Tui.cpp.

#include "UefiTypes.h"

namespace Ui {

// Append-style string builders backed by one shared static buffer. The returned
// pointer is valid until the next Concat* call (matches the original behaviour).
const char* Concat2(const char* a, const char* b);
const char* Concat3(const char* a, const char* b, const char* c);

// A single selectable menu row, optionally with a checkbox. Highlighted rows
// draw with the theme highlight background.
void DrawMenuItem(int row, const char* text, bool highlighted,
                  bool showCheckbox = false, bool isChecked = false);

// Full-width horizontal separator. Returns row + 1 for chaining.
int  DrawSeparator(int row);

// A "[####....]" progress bar spanning the screen width.
void DrawProgressBar(int row, UINTN current, UINTN total);

}  // namespace Ui
