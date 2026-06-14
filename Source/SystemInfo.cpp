// System resource detection: CPUID for CPU info, GetMemoryMap for RAM.

#include "SystemInfo.h"
#include "Freestanding.h"

namespace SystemInfo {

static char sVendor[16]  = {};
static char sBrand[52]   = {};
static UINT32 sCoreCount = 1;
static UINT64 sTotalMem  = 0;
static EFI_MP_SERVICES_PROTOCOL* sMpServices = nullptr;
static bool sMpAvailable = false;
static UINT32 sEnabledProcessors = 1;

static UINT32 sCpuStepping     = 0;
static UINT32 sL1DataCacheKB   = 0;
static UINT32 sL1InstCacheKB   = 0;
static UINT32 sL2CacheKB       = 0;
static UINT32 sL3CacheKB       = 0;
static UINT32 sMemSpeedMHz     = 0;
static UINT32 sMemConfigSpeed  = 0;
static UINT32 sMemChannelCount = 0;
static UINT32 sMemVoltageMv    = 0;
static char   sMemType[8]      = "Unknown";

static UINT32 sSpdTCL   = 0;
static UINT32 sSpdTRCD  = 0;
static UINT32 sSpdTRP   = 0;
static UINT32 sSpdTRAS  = 0;
static bool   sSpdDdr5  = false;

// ── CPUID helpers ────────────────────────────────────────────
static void CpuidRaw(UINT32 leaf, UINT32 subleaf,
                     UINT32& eax, UINT32& ebx, UINT32& ecx, UINT32& edx) {
    __asm__ volatile(
        "cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(leaf), "c"(subleaf)
    );
}

static void DetectCpuVendor() {
    UINT32 eax, ebx, ecx, edx;
    CpuidRaw(0, 0, eax, ebx, ecx, edx);
    // Vendor string: EBX + EDX + ECX
    memcpy(sVendor + 0, &ebx, 4);
    memcpy(sVendor + 4, &edx, 4);
    memcpy(sVendor + 8, &ecx, 4);
    sVendor[12] = '\0';
}

static void DetectCpuBrand() {
    UINT32 eax, ebx, ecx, edx;
    CpuidRaw(0x80000000, 0, eax, ebx, ecx, edx);
    if (eax < 0x80000004) {
        StrCopy(sBrand, "Unknown CPU", sizeof(sBrand));
        return;
    }
    for (UINT32 i = 0; i < 3; ++i) {
        CpuidRaw(0x80000002 + i, 0, eax, ebx, ecx, edx);
        memcpy(sBrand + i * 16 +  0, &eax, 4);
        memcpy(sBrand + i * 16 +  4, &ebx, 4);
        memcpy(sBrand + i * 16 +  8, &ecx, 4);
        memcpy(sBrand + i * 16 + 12, &edx, 4);
    }
    sBrand[48] = '\0';
    // Trim leading spaces
    int start = 0;
    while (sBrand[start] == ' ') ++start;
    if (start > 0) {
        int len = static_cast<int>(StrLen(sBrand + start));
        memmove(sBrand, sBrand + start, len + 1);
    }
}

static void DetectCoreCount() {
    UINT32 eax, ebx, ecx, edx;

    // Try leaf 0xB (Extended Topology Enumeration)
    CpuidRaw(0, 0, eax, ebx, ecx, edx);
    UINT32 maxLeaf = eax;

    if (maxLeaf >= 0x1F) {
        // Try leaf 0x1F (V2 Extended Topology) first — newer Intel
        CpuidRaw(0x1F, 1, eax, ebx, ecx, edx);
        if ((ebx & 0xFFFF) > 0) {
            sCoreCount = ebx & 0xFFFF;
            return;
        }
    }

    if (maxLeaf >= 0x0B) {
        // Leaf 0xB subleaf 1 = core-level count
        CpuidRaw(0x0B, 1, eax, ebx, ecx, edx);
        if ((ebx & 0xFFFF) > 0) {
            sCoreCount = ebx & 0xFFFF;
            return;
        }
    }

    // Fallback: leaf 0x1 — logical processor count in EBX[23:16]
    if (maxLeaf >= 0x01) {
        CpuidRaw(0x01, 0, eax, ebx, ecx, edx);
        UINT32 logicalCount = (ebx >> 16) & 0xFF;
        if (logicalCount > 0) {
            sCoreCount = logicalCount;
            return;
        }
    }

    sCoreCount = 1;
}

// ── Memory map ───────────────────────────────────────────────
static void DetectMemory() {
    if (!gBS) return;

    UINTN mapSize = 0;
    UINTN mapKey, descSize;
    UINT32 descVersion;

    // First call to get required buffer size
    EFI_STATUS status = gBS->GetMemoryMap(
        &mapSize, nullptr, &mapKey, &descSize, &descVersion);

    if (status != EFI_BUFFER_TOO_SMALL) return;

    // Add headroom (allocating changes the map)
    mapSize += 2 * descSize;
    UINT8* buf = nullptr;
    status = gBS->AllocatePool(EfiLoaderData, mapSize, reinterpret_cast<VOID**>(&buf));
    if (EFI_ERROR(status) || !buf) return;

    status = gBS->GetMemoryMap(
        &mapSize, reinterpret_cast<EFI_MEMORY_DESCRIPTOR*>(buf),
        &mapKey, &descSize, &descVersion);

    if (!EFI_ERROR(status)) {
        UINT64 total = 0;
        for (UINTN off = 0; off < mapSize; off += descSize) {
            auto desc = reinterpret_cast<EFI_MEMORY_DESCRIPTOR*>(buf + off);
            if (desc->Type == EfiConventionalMemory) {
                total += desc->NumberOfPages * EFI_PAGE_SIZE;
            }
        }
        sTotalMem = total;
    }

    gBS->FreePool(buf);
}

// ── MP Services detection ────────────────────────────────────
static void DetectMpServices() {
    if (!gBS) return;

    EFI_GUID mpGuid = EFI_MP_SERVICES_PROTOCOL_GUID;
    EFI_STATUS status = gBS->LocateProtocol(
        &mpGuid, nullptr, reinterpret_cast<VOID**>(&sMpServices));

    if (!EFI_ERROR(status) && sMpServices) {
        sMpAvailable = true;
        UINTN total = 0, enabled = 0;
        status = sMpServices->GetNumberOfProcessors(
            sMpServices, &total, &enabled);
        if (!EFI_ERROR(status) && enabled > 0) {
            sEnabledProcessors = static_cast<UINT32>(enabled);
            // Override CPUID core count with MP Services (more accurate)
            sCoreCount = sEnabledProcessors;
        }
    }
}

static void DetectCpuStepping() {
    UINT32 eax, ebx, ecx, edx;
    CpuidRaw(0x1, 0, eax, ebx, ecx, edx);
    sCpuStepping = eax & 0xF;
}

static UINT32 CalcCacheKBFromLeaf4(UINT32 /*eax*/, UINT32 ebx, UINT32 ecx) {
    UINT32 lineSize   = (ebx & 0xFFF) + 1;
    UINT32 partitions = ((ebx >> 12) & 0x3FF) + 1;
    UINT32 ways       = ((ebx >> 22) & 0x3FF) + 1;
    UINT32 sets       = ecx + 1;
    return (lineSize * partitions * ways * sets) / 1024;
}

static void DetectCpuCacheIntel() {
    UINT32 eax, ebx, ecx, edx;
    for (UINT32 sub = 0; sub < 16; ++sub) {
        CpuidRaw(0x4, sub, eax, ebx, ecx, edx);
        UINT32 type  = eax & 0x1F;
        UINT32 level = (eax >> 5) & 0x7;
        if (type == 0) break;

        UINT32 kb = CalcCacheKBFromLeaf4(eax, ebx, ecx);
        if (level == 1 && type == 1 && sL1DataCacheKB == 0) sL1DataCacheKB = kb;
        if (level == 1 && type == 2 && sL1InstCacheKB == 0) sL1InstCacheKB = kb;
        if (level == 2 && sL2CacheKB == 0)                  sL2CacheKB = kb;
        if (level == 3 && sL3CacheKB == 0)                  sL3CacheKB = kb;
    }
}

static void DetectCpuCacheAmd() {
    UINT32 eax, ebx, ecx, edx;

    // Try CPUID 0x8000001D (Zen+ extended cache topology, same format as Intel leaf 4)
    CpuidRaw(0x80000000, 0, eax, ebx, ecx, edx);
    UINT32 maxExt = eax;

    if (maxExt >= 0x8000001D) {
        for (UINT32 sub = 0; sub < 16; ++sub) {
            CpuidRaw(0x8000001D, sub, eax, ebx, ecx, edx);
            UINT32 type  = eax & 0x1F;
            UINT32 level = (eax >> 5) & 0x7;
            if (type == 0) break;

            UINT32 kb = CalcCacheKBFromLeaf4(eax, ebx, ecx);
            if (level == 1 && type == 1 && sL1DataCacheKB == 0) sL1DataCacheKB = kb;
            if (level == 1 && type == 2 && sL1InstCacheKB == 0) sL1InstCacheKB = kb;
            if (level == 2 && sL2CacheKB == 0)                  sL2CacheKB = kb;
            if (level == 3 && sL3CacheKB == 0)                  sL3CacheKB = kb;
        }
        return;
    }

    // Fallback: legacy AMD leaves
    if (maxExt >= 0x80000005) {
        CpuidRaw(0x80000005, 0, eax, ebx, ecx, edx);
        sL1DataCacheKB = (ecx >> 24) & 0xFF;
        sL1InstCacheKB = (edx >> 24) & 0xFF;
    }
    if (maxExt >= 0x80000006) {
        CpuidRaw(0x80000006, 0, eax, ebx, ecx, edx);
        sL2CacheKB = (ecx >> 16) & 0xFFFF;
        sL3CacheKB = ((edx >> 18) & 0x3FFF) * 512; // units of 512 KB
    }
}

static void DetectCpuCache() {
    // Use Intel path for GenuineIntel, AMD path for AuthenticAMD
    if (sVendor[0] == 'G') { // GenuineIntel
        DetectCpuCacheIntel();
    } else if (sVendor[0] == 'A') { // AuthenticAMD
        DetectCpuCacheAmd();
    } else {
        // Unknown vendor: try Intel leaf 4 as a best-effort
        DetectCpuCacheIntel();
    }
}

// ── SMBIOS detection ─────────────────────────────────────────

static bool GuidEqual(const EFI_GUID& a, const EFI_GUID& b) {
    if (a.Data1 != b.Data1 || a.Data2 != b.Data2 || a.Data3 != b.Data3) return false;
    for (int i = 0; i < 8; ++i)
        if (a.Data4[i] != b.Data4[i]) return false;
    return true;
}

// Returns pointer to string at 1-based index, or "" if not found.
static const char* SmbiosGetString(const UINT8* structBase, UINT8 index) {
    if (index == 0) return "";
    const char* s = reinterpret_cast<const char*>(structBase + structBase[1]);
    for (UINT8 i = 1; i < index; ++i) {
        while (*s) ++s;
        ++s;
        if (*s == '\0') return ""; // ran off end of strings
    }
    return s;
}

// Returns pointer to byte after the structure's string section.
static const UINT8* SmbiosNextStruct(const UINT8* p) {
    const char* s = reinterpret_cast<const char*>(p + p[1]);
    while (s[0] || s[1]) ++s;
    return reinterpret_cast<const UINT8*>(s + 2);
}

static void ParseSmbiosTable(const UINT8* start, UINT32 len) {
    const UINT8* p   = start;
    const UINT8* end = start + len;

    char  banks[8][64];
    UINT32 bankCount = 0;

    while (p < end && p + 4 <= end) {
        UINT8 type   = p[0];
        UINT8 length = p[1];

        if (type == 127) break; // end-of-table marker
        if (length < 4)  break; // malformed

        if (type == 17 && length >= 0x18) { // Memory Device
            UINT16 size = *reinterpret_cast<const UINT16*>(p + 0x0C);
            bool populated = (size & 0x7FFF) != 0 && size != 0xFFFF;

            if (populated) {
                UINT16 speed = *reinterpret_cast<const UINT16*>(p + 0x15);
                if (speed > 0 && sMemSpeedMHz == 0) sMemSpeedMHz = speed;

                if (length >= 0x22) {
                    UINT16 cs = *reinterpret_cast<const UINT16*>(p + 0x20);
                    if (cs > 0 && sMemConfigSpeed == 0) sMemConfigSpeed = cs;
                }

                // Memory type (offset 0x12)
                if (sMemType[0] == 'U') { // still "Unknown"
                    UINT8 mt = p[0x12];
                    const char* typeName = nullptr;
                    if      (mt == 0x18) typeName = "DDR3";
                    else if (mt == 0x1A) typeName = "DDR4";
                    else if (mt == 0x1B) typeName = "LPDDR";
                    else if (mt == 0x1D) typeName = "LPDDR3";
                    else if (mt == 0x1E) typeName = "LPDDR4";
                    else if (mt == 0x1F) typeName = "LPDDR4X";
                    else if (mt == 0x22) typeName = "DDR5";
                    else if (mt == 0x23) typeName = "LPDDR5";
                    if (typeName) StrCopy(sMemType, typeName, 8);
                }

                // Configured voltage in mV (SMBIOS 2.8+, offset 0x26, length >= 0x28)
                if (sMemVoltageMv == 0 && length >= 0x28) {
                    UINT16 mv = *reinterpret_cast<const UINT16*>(p + 0x26);
                    if (mv > 0) sMemVoltageMv = mv;
                }

                // Collect unique bank locator strings for channel count
                const char* bankStr = SmbiosGetString(p, p[0x11]);
                if (bankStr[0] != '\0' && bankCount < 8) {
                    bool found = false;
                    for (UINT32 i = 0; i < bankCount; ++i)
                        if (StrCmp(banks[i], bankStr) == 0) { found = true; break; }
                    if (!found)
                        StrCopy(banks[bankCount++], bankStr, 64);
                }
            }
        }

        p = SmbiosNextStruct(p);
    }

    if (bankCount > 0) sMemChannelCount = bankCount;
}

static void DetectSmbios() {
    if (!gST) return;

    EFI_GUID guid3 = SMBIOS3_TABLE_GUID;
    EFI_GUID guid2 = SMBIOS_TABLE_GUID;

    auto confTable = reinterpret_cast<EFI_CONFIGURATION_TABLE*>(gST->ConfigurationTable);

    const UINT8* tableStart = nullptr;
    UINT32 tableLen = 0;

    // Prefer SMBIOS 3.x (64-bit address)
    for (UINTN i = 0; i < gST->NumberOfTableEntries && !tableStart; ++i) {
        if (!GuidEqual(confTable[i].VendorGuid, guid3)) continue;
        const UINT8* ep = reinterpret_cast<const UINT8*>(confTable[i].VendorTable);
        if (memcmp(ep, "_SM3_", 5) != 0) continue;
        UINT64 addr = *reinterpret_cast<const UINT64*>(ep + 16);
        UINT32 sz   = *reinterpret_cast<const UINT32*>(ep + 12);
        tableStart  = reinterpret_cast<const UINT8*>(static_cast<UINTN>(addr));
        tableLen    = sz;
    }

    // Fall back to SMBIOS 2.x (32-bit address)
    for (UINTN i = 0; i < gST->NumberOfTableEntries && !tableStart; ++i) {
        if (!GuidEqual(confTable[i].VendorGuid, guid2)) continue;
        const UINT8* ep = reinterpret_cast<const UINT8*>(confTable[i].VendorTable);
        if (memcmp(ep, "_SM_", 4) != 0) continue;
        UINT32 addr = *reinterpret_cast<const UINT32*>(ep + 24);
        UINT16 sz   = *reinterpret_cast<const UINT16*>(ep + 22);
        tableStart  = reinterpret_cast<const UINT8*>(static_cast<UINTN>(addr));
        tableLen    = sz;
    }

    if (tableStart && tableLen > 0)
        ParseSmbiosTable(tableStart, tableLen);
}

// ── SPD via SMBus I/O ─────────────────────────────────────────
// Reads DDR4 DRAM timings (tCL/tRCD/tRP/tRAS) from DIMM SPD EEPROMs.
// SMBus controller is located by scanning PCI bus 0 for class 0x0C/0x05.
// All I/O is via port IN/OUT (CPL=0 UEFI pre-boot environment).

static void IoOutB(UINT16 port, UINT8 val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}
static UINT8 IoInB(UINT16 port) {
    UINT8 v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static UINT32 PciCfgRead32Spd(UINT8 dev, UINT8 fn, UINT8 off) {
    UINT32 addr = 0x80000000U | ((UINT32)(dev & 0x1F) << 11) |
                  ((UINT32)(fn & 7) << 8) | (off & 0xFC);
    __asm__ volatile("outl %0, %1" : : "a"(addr), "Nd"((UINT16)0x0CF8));
    UINT32 data;
    __asm__ volatile("inl %1, %0" : "=a"(data) : "Nd"((UINT16)0x0CFC));
    return data;
}
static void PciCfgWrite32Spd(UINT8 dev, UINT8 fn, UINT8 off, UINT32 val) {
    UINT32 addr = 0x80000000U | ((UINT32)(dev & 0x1F) << 11) |
                  ((UINT32)(fn & 7) << 8) | (off & 0xFC);
    __asm__ volatile("outl %0, %1" : : "a"(addr), "Nd"((UINT16)0x0CF8));
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"((UINT16)0x0CFC));
}

static UINT16 FindSmbusIoBase() {
    for (UINT8 dev = 0; dev < 32; ++dev) {
        for (UINT8 fn = 0; fn < 8; ++fn) {
            UINT32 id = PciCfgRead32Spd(dev, fn, 0x00);
            if ((id & 0xFFFF) == 0xFFFF) { if (fn == 0) break; continue; }
            UINT32 cls = PciCfgRead32Spd(dev, fn, 0x08);
            if (((cls >> 24) & 0xFF) != 0x0C) continue;
            if (((cls >> 16) & 0xFF) != 0x05) continue;
            // Ensure I/O space is enabled in PCI command register
            UINT32 cmdReg = PciCfgRead32Spd(dev, fn, 0x04);
            if (!(cmdReg & 1)) {
                PciCfgWrite32Spd(dev, fn, 0x04, cmdReg | 1);
            }
            // Find I/O BAR (bit 0 = 1 indicates I/O space)
            for (UINT8 b = 0x10; b <= 0x24; b += 4) {
                UINT32 bar = PciCfgRead32Spd(dev, fn, b);
                if (!(bar & 1)) continue;
                UINT16 ioAddr = (UINT16)(bar & 0xFFFE);
                if (ioAddr > 0x0F) return ioAddr;
            }
        }
    }
    return 0;
}

static void IoDelay() {
    IoInB(0x80); // read from POST port as a short I/O delay
}

// Reset the SMBus controller — clears stuck HOST_BUSY, error bits, and orphans.
static void ResetSmbusController(UINT16 base) {
    IoOutB(base, 0xFF);                     // clear all writable status bits
    IoDelay();
    // Force an abort by writing START-only (no protocol) — controller will
    // time out and release the bus, clearing HOST_BUSY if it was stuck.
    IoOutB(base + 2, 0x40);                 // START | 0 (STOP generates automatically)
    IoDelay();
    for (UINT32 t = 50000; t; --t) {
        if (!(IoInB(base) & 1)) break;
        IoInB(0x80);                        // delay between polls
    }
    IoOutB(base, 0xFF);                     // clear any status from the abort
    IoDelay();
}

static UINT8 ReadSmbByte(UINT16 base, UINT8 addr, UINT8 reg) {
    // Poll HOST_BUSY (bit 0) until idle
    for (UINT32 t = 100000; t && (IoInB(base) & 1); --t) ;
    if (IoInB(base) & 1) {                  // bus stuck — try a reset
        ResetSmbusController(base);
        if (IoInB(base) & 1) return 0xFF;   // still stuck, give up
    }

    IoOutB(base,     0x1E);                  // clear INTR & error bits
    IoOutB(base + 4, (UINT8)((addr << 1) | 1)); // slave address + READ
    IoOutB(base + 3, reg);                   // SPD byte offset
    IoOutB(base + 2, 0x48);                  // START | BYTE_DATA command
    IoDelay();

    // Poll for INTR (bit 1) or any error (bits 2-4)
    UINT8 sts = 0;
    for (UINT32 t = 500000; t; --t) {
        sts = IoInB(base);
        if (sts & 0x1E) break;
    }
    return (sts & 0x02) ? IoInB(base + 5) : (UINT8)0xFF;
}

static void DetectSpd() {
    UINT16 base = FindSmbusIoBase();
    if (!base) return;

    // Reset the controller if it's busy (e.g., left over from firmware)
    if (IoInB(base) & 1) {
        ResetSmbusController(base);
        if (IoInB(base) & 1) return; // still stuck, can't use SMBus
    } else {
        // Even if idle, clear any stale error bits
        IoOutB(base, 0xFF);
        IoDelay();
    }

    // SPD EEPROMs are at SMBus addresses 0x50–0x57 (one per slot)
    for (UINT8 slot = 0x50; slot <= 0x57; ++slot) {
        UINT8 b0 = ReadSmbByte(base, slot, 0);
        if (b0 == 0x00 || b0 == 0xFF) continue;       // no DIMM here

        UINT8 devType = ReadSmbByte(base, slot, 2);

        if (devType == 0x12) {
            // DDR5 SPD (JEDEC SPD for DDR5, rev 1.0)
            // Timings use 4 ps MTB; tCKAVGmin at bytes 30-31 (16-bit LE, in ps)
            // tAAmin at bytes 40-41, tRCDmin at 42-43, tRPmin at 44-45, tRASmin at 46-47
            sSpdDdr5 = true;
            UINT8 tCKLo  = ReadSmbByte(base, slot, 30);
            UINT8 tCKHi  = ReadSmbByte(base, slot, 31);
            UINT8 tAALo  = ReadSmbByte(base, slot, 40);
            UINT8 tAAHi  = ReadSmbByte(base, slot, 41);
            UINT8 tRCDLo = ReadSmbByte(base, slot, 42);
            UINT8 tRCDHi = ReadSmbByte(base, slot, 43);
            UINT8 tRPLo  = ReadSmbByte(base, slot, 44);
            UINT8 tRPHi  = ReadSmbByte(base, slot, 45);
            UINT8 tRASLo5= ReadSmbByte(base, slot, 46);
            UINT8 tRASHi5= ReadSmbByte(base, slot, 47);

            UINT32 tCK  = ((UINT32)tCKHi  << 8) | tCKLo;
            UINT32 tAA  = ((UINT32)tAAHi  << 8) | tAALo;
            UINT32 tRCD = ((UINT32)tRCDHi << 8) | tRCDLo;
            UINT32 tRP  = ((UINT32)tRPHi  << 8) | tRPLo;
            UINT32 tRAS = ((UINT32)tRASHi5<< 8) | tRASLo5;

            if (tCK == 0 || tCK == 0xFFFF) continue;

            auto CeilDiv5 = [](UINT32 a, UINT32 b) -> UINT32 {
                return b ? (a + b - 1) / b : 0;
            };
            sSpdTCL  = CeilDiv5(tAA,  tCK);
            sSpdTRCD = CeilDiv5(tRCD, tCK);
            sSpdTRP  = CeilDiv5(tRP,  tCK);
            sSpdTRAS = CeilDiv5(tRAS, tCK);
            break;
        }
        else if (devType == 0x0C) {
            // DDR4 SPD (JEDEC SPD4) — all timings in MTB (Medium Timebase) units
            UINT8 tCKmin  = ReadSmbByte(base, slot, 18);  // min cycle time (MTB = 0.125 ns)
            UINT8 tAAmin  = ReadSmbByte(base, slot, 23);  // CAS Latency min time
            UINT8 tRCDmin = ReadSmbByte(base, slot, 24);  // RAS-to-CAS delay min
            UINT8 tRPmin  = ReadSmbByte(base, slot, 25);  // Row Precharge min
            UINT8 tRASLo  = ReadSmbByte(base, slot, 26);  // tRASmin lower 8 bits
            UINT8 b27     = ReadSmbByte(base, slot, 27);  // [3:0]=tRAS_ext [7:4]=tRC_ext

            if (tCKmin == 0 || tCKmin == 0xFF) continue;

            UINT32 tRAS = ((UINT32)(b27 & 0x0F) << 8) | tRASLo;

            // Cycles = ceil(timing_MTB / tCKmin_MTB)
            auto CeilDiv = [](UINT32 a, UINT32 b) -> UINT32 {
                return b ? (a + b - 1) / b : 0;
            };

            sSpdTCL  = CeilDiv((UINT32)tAAmin,  (UINT32)tCKmin);
            sSpdTRCD = CeilDiv((UINT32)tRCDmin, (UINT32)tCKmin);
            sSpdTRP  = CeilDiv((UINT32)tRPmin,  (UINT32)tCKmin);
            sSpdTRAS = CeilDiv(tRAS,             (UINT32)tCKmin);
            break; // first populated DDR4 DIMM wins
        }
    }
}

// ── Public API ───────────────────────────────────────────────
void Detect() {
    DetectCpuVendor();
    DetectCpuBrand();
    DetectCoreCount();
    DetectCpuStepping();
    DetectCpuCache();
    DetectMemory();
    DetectMpServices();
    DetectSmbios();
    DetectSpd();
}

const char* GetCpuVendor()     { return sVendor; }
const char* GetCpuBrand()      { return sBrand; }
UINT32      GetCpuCoreCount()  { return sCoreCount; }
UINT32      GetCpuStepping()   { return sCpuStepping; }
UINT32      GetL1DataCacheKB() { return sL1DataCacheKB; }
UINT32      GetL1InstCacheKB() { return sL1InstCacheKB; }
UINT32      GetL2CacheKB()     { return sL2CacheKB; }
UINT32      GetL3CacheKB()     { return sL3CacheKB; }
UINT64      GetTotalMemoryBytes() { return sTotalMem; }
UINT64      GetTotalMemoryMB()    { return sTotalMem / (1024 * 1024); }
UINT32      GetMemorySpeedMHz()            { return sMemSpeedMHz; }
UINT32      GetMemoryConfiguredSpeedMHz()  { return sMemConfigSpeed; }
UINT32      GetMemoryChannelCount()        { return sMemChannelCount; }
UINT32      GetMemoryVoltageMv()           { return sMemVoltageMv; }
const char* GetMemoryType()               { return sMemType; }
UINT32      GetSpdTCL()                   { return sSpdTCL; }
UINT32      GetSpdTRCD()                  { return sSpdTRCD; }
UINT32      GetSpdTRP()                   { return sSpdTRP; }
UINT32      GetSpdTRAS()                  { return sSpdTRAS; }
bool        IsSpdDdr5()                   { return sSpdDdr5; }

bool HasMpServices() { return sMpAvailable; }
EFI_MP_SERVICES_PROTOCOL* GetMpServices() { return sMpServices; }
UINT32 GetEnabledProcessorCount() { return sEnabledProcessors; }

} // namespace SystemInfo
