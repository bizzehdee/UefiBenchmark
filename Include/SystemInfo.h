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
UINT32      GetCpuStepping();   // CPUID leaf 1 EAX[3:0]

// CPU cache sizes (0 if not present or not detectable)
UINT32 GetL1DataCacheKB();
UINT32 GetL1InstCacheKB();
UINT32 GetL2CacheKB();
UINT32 GetL3CacheKB();

// Memory information
UINT64 GetTotalMemoryBytes();   // sum of EfiConventionalMemory regions
UINT64 GetTotalMemoryMB();

// Memory module info from SMBIOS (0 if SMBIOS not available)
UINT32      GetMemorySpeedMHz();           // DIMM rated speed
UINT32      GetMemoryConfiguredSpeedMHz(); // configured operating speed
UINT32      GetMemoryChannelCount();       // inferred from SMBIOS bank locator strings
UINT32      GetMemoryVoltageMv();          // configured voltage in mV (0 if unknown)
const char* GetMemoryType();               // e.g. "DDR4", "DDR5", "Unknown"

// DRAM timings read directly from SPD via SMBus (0 if not available).
// Populated for DDR4 and DDR5; DDR3 and older show 0.
UINT32 GetSpdTCL();
UINT32 GetSpdTRCD();
UINT32 GetSpdTRP();
UINT32 GetSpdTRAS();
bool   IsSpdDdr5();   // true when DDR5 DIMMs detected (DDR5 timing bytes read separately)

// SMBus detection diagnostics — populated by DetectSpd(), useful for debug.
struct SmbusDebugInfo {
    UINT8  Dev;         // PCI device# of SMBus controller (0xFF = none found)
    UINT8  Fn;          // PCI function#
    UINT16 Vid;         // vendor ID (0x8086=Intel, 0x1022=AMD, 0=not found)
    UINT16 Did;         // device ID
    UINT32 Reg20;       // PCI cfg offset 0x20 (Intel SMBA / BAR4)
    UINT32 Reg40;       // PCI cfg offset 0x40 (Intel HOSTC)
    UINT32 Reg90;       // PCI cfg offset 0x90 (AMD SMBHOSTADDR — older FCH)
    UINT16 PmioSmba;    // raw PMIO[0x2D:0x2C] AMD SMBus base (Promontory/AM4+)
    UINT16 IoBase;      // I/O base returned by FindSmbusIoBase (0 = not found)
    UINT8  InitHststs;  // HSTSTS value read at start of DetectSpd (0xFF = no base)
    UINT8  Slot50B0;    // ReadSmbByte(0x50, 0) — SPD byte 0 (0xFF = no response)
    UINT8  Slot50Dt;    // ReadSmbByte(0x50, 2) — device type byte
};
const SmbusDebugInfo& GetSmbusDebug();

// MP Services (multi-core) support
bool HasMpServices();
EFI_MP_SERVICES_PROTOCOL* GetMpServices();
UINT32 GetEnabledProcessorCount();  // includes BSP

} // namespace SystemInfo
