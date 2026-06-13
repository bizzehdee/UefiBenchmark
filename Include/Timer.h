#pragma once
// TSC-based high-resolution timer for UEFI environment.
// Calibrated against BootServices->Stall() at startup.
// Checks for invariant TSC and uses CPUID serialisation fences.

#include "UefiTypes.h"

namespace Timer {

// Call once during early init (after gBS is set).
// Takes ~300 ms (multiple calibration samples).
void Calibrate();

// Returns true if calibration succeeded.
bool IsCalibrated();

// Cycles per microsecond (set after Calibrate).
UINT64 CyclesPerUs();

// Read the current TSC with serialisation fence.
UINT64 ReadTSC();

// Start / ElapsedUs pair for stopwatch usage.
void   Start();
UINT64 ElapsedUs();   // microseconds since last Start()
UINT64 ElapsedMs();   // milliseconds since last Start()

// True if the CPU advertises invariant TSC.
bool HasInvariantTSC();

} // namespace Timer
