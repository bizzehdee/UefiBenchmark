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

// ── Public API ───────────────────────────────────────────────
void Detect() {
    DetectCpuVendor();
    DetectCpuBrand();
    DetectCoreCount();
    DetectMemory();
    DetectMpServices();
}

const char* GetCpuVendor()     { return sVendor; }
const char* GetCpuBrand()      { return sBrand; }
UINT32      GetCpuCoreCount()  { return sCoreCount; }
UINT64      GetTotalMemoryBytes() { return sTotalMem; }
UINT64      GetTotalMemoryMB()    { return sTotalMem / (1024 * 1024); }

bool HasMpServices() { return sMpAvailable; }
EFI_MP_SERVICES_PROTOCOL* GetMpServices() { return sMpServices; }
UINT32 GetEnabledProcessorCount() { return sEnabledProcessors; }

} // namespace SystemInfo
