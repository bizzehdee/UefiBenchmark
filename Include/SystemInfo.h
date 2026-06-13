#pragma once
// System resource detection: CPU info (CPUID), available memory (GetMemoryMap),
// and MP Services Protocol for multi-core support.

#include "UefiTypes.h"

namespace SystemInfo {

// Call once during early init (after gBS is set).
void Detect();

// CPU information
const char* GetCpuVendor();     // e.g. "GenuineIntel"
const char* GetCpuBrand();      // e.g. "Intel(R) Core(TM) i7-..."
UINT32      GetCpuCoreCount();  // logical processors via CPUID topology

// Memory information
UINT64 GetTotalMemoryBytes();   // sum of EfiConventionalMemory regions
UINT64 GetTotalMemoryMB();

// MP Services (multi-core) support
bool HasMpServices();
EFI_MP_SERVICES_PROTOCOL* GetMpServices();
UINT32 GetEnabledProcessorCount();  // includes BSP

} // namespace SystemInfo
