#pragma once
// Host-build UEFI shim: control APIs for FakeTimer and FakeRenderer.
// FakeTimer: include from test files that drive time-dependent units (TimeBox,
//   LongBenchmarkBase). Reset() in each test for isolation.
// FakeRenderer: include from test files that call ScrollViewport::Render().
//   Records every DrawText / DrawTextBg call for assertion.

#include "UefiTypes.h"

namespace FakeTimer {

// Zero everything — uncalibrated, TSC=0, step=0, cyclesPerUs=1.
void Reset();

// Control calibration state.
void SetCalibrated(bool on);

// Cycles per microsecond reported by Timer::CyclesPerUs().
void SetCyclesPerUs(UINT64 cpus);

// Set the current TSC value returned by the next Timer::ReadTSC() call
// (before the auto-step is applied).
void SetTSC(UINT64 tsc);

// Amount added to the TSC on every Timer::ReadTSC() call.
// Set to 0 to stop auto-advancing (TSC stays at whatever SetTSC left it).
void SetStepPerRead(UINT64 step);

// Read the current internal TSC (after any in-progress step).
UINT64 GetCurrentTSC();

} // namespace FakeTimer

// ── FakeRenderer ─────────────────────────────────────────────────────────────
// Recording stub for Renderer::DrawText / DrawTextBg used by ScrollViewport.
// The real Renderer:: functions in UefiShim.cpp delegate here.

namespace FakeRenderer {

struct DrawCall {
    int  col, row;
    char text[128];  // copy of the text argument (truncated at 127 chars)
    bool hasBg;      // true = DrawTextBg, false = DrawText
};

static constexpr UINT32 MAX_CALLS = 512;

// Clear the call log and reset dimensions to 80×25.
void Reset();

// Set what Renderer::Columns() and Renderer::Rows() return.
void SetSize(UINT32 cols, UINT32 rows);

// Number of recorded draw calls since the last Reset().
UINT32 CallCount();

// Direct read-only access to the recorded call array.
const DrawCall* Calls();

} // namespace FakeRenderer
