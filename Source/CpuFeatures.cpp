// CPU feature detection (CPUID) and AVX state enablement (CR4 + XSETBV).
// Runs at CPL=0 (UEFI pre-ExitBootServices), so CR4 writes and XSETBV are legal.

#include "CpuFeatures.h"

namespace CpuFeatures {

static Features sFeatures = {};
static bool     sDetected = false;

// Raw CPUID wrapper (serialising fence via CPUID itself).
static void Cpuid(UINT32 leaf, UINT32 subleaf,
                  UINT32* eax, UINT32* ebx, UINT32* ecx, UINT32* edx) {
    asm volatile("cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf), "c"(subleaf));
}

void Detect() {
    UINT32 eax, ebx, ecx, edx;

    // Leaf 1: basic features
    Cpuid(1, 0, &eax, &ebx, &ecx, &edx);
    sFeatures.HasSSE2  = (edx >> 26) & 1;
    sFeatures.HasAESNI = (ecx >>  25) & 1;
    sFeatures.HasAVX   = (ecx >> 28) & 1;
    sFeatures.HasFMA   = (ecx >> 12) & 1;
    sFeatures.HasXSave = (ecx >> 26) & 1;

    // Leaf 7 subleaf 0: extended features
    UINT32 maxLeaf;
    Cpuid(0, 0, &maxLeaf, &ebx, &ecx, &edx);
    if (maxLeaf >= 7) {
        Cpuid(7, 0, &eax, &ebx, &ecx, &edx);
        sFeatures.HasAVX2 = (ebx >>  5) & 1;
        sFeatures.HasSHA  = (ebx >> 29) & 1;
    }

    // SSE4.2 is leaf 1 ECX bit 20
    Cpuid(1, 0, &eax, &ebx, &ecx, &edx);
    sFeatures.HasSSE42 = (ecx >> 20) & 1;

    sDetected = true;
}

const Features& Get() {
    return sFeatures;
}

bool EnableAvxState() {
    if (!sDetected) Detect();
    if (!sFeatures.HasXSave || !sFeatures.HasAVX) return false;

    // Set CR4.OSFXSR (9), OSXMMEXCPT (10), OSXSAVE (18)
    UINT64 cr4;
    asm volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1ULL << 9) | (1ULL << 10) | (1ULL << 18);
    asm volatile("mov %0, %%cr4" : : "r"(cr4) : "memory");

    // Enable x87 (bit 0) + SSE (bit 1) + AVX (bit 2) in XCR0
    asm volatile("xsetbv" : : "c"(0U), "a"(7U), "d"(0U) : "memory");

    return true;
}

} // namespace CpuFeatures
