#pragma once
// Per-boot AP roster and selection state for multi-core benchmark dispatch.
// Populated from MP Services at startup; resets to all available APs each boot.

#include "UefiTypes.h"

namespace CoreSelection {

constexpr UINT32 MAX_APS = 64;

struct ApInfo {
    UINTN  ProcIndex;  // processor number for StartupThisAP / EnableDisableAP
    UINT32 Package;
    UINT32 Core;
    UINT32 Thread;
    bool   Selected;
    bool   Available;  // false if firmware reports the AP as disabled
};

// Enumerate APs via GetProcessorInfo; call after SystemInfo::Detect().
void Init();

// Number of APs in the roster (BSP excluded).
UINT32 Count();

// Direct access to the roster array (Count() entries).
ApInfo* GetAll();

// Number of APs currently marked Selected and Available.
UINT32 SelectedCount();

// Fill `out` with the ProcIndex of each selected+available AP.
// Returns the number written (capped at `cap`).
UINT32 GetSelectedIndices(UINTN* out, UINT32 cap);

// Presets
void SelectAll();
void SelectPhysicalCoresOnly();  // Thread == 0 only (or sole thread on that core)
void SelectOnePerPackage();      // first AP per socket

// BSP participation: when true, BenchmarkRunner adds Core 0 as an
// additional worker (sequential phase after APs). Defaults to false.
void  SetIncludeBsp(bool include);
bool  GetIncludeBsp();

#ifdef UEFI_HOST_TEST
// Replace the roster with a synthetic list for unit-test use (bypasses Init()).
// Also resets IncludeBsp to false.
void InjectRoster(const ApInfo* aps, UINT32 count);
#endif

} // namespace CoreSelection
