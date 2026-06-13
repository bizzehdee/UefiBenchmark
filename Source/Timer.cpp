// TSC-based timer with UEFI Stall() calibration.
// Uses CPUID serialisation fences and multiple samples for accuracy.

#include "Timer.h"

namespace Timer {

static UINT64 sCyclesPerUs = 0;
static bool   sCalibrated  = false;
static bool   sInvariantTSC = false;
static UINT64 sStartTick   = 0;

// Serialised RDTSC: fence with CPUID before reading TSC.
static inline UINT64 SerializedRDTSC() {
    UINT32 lo, hi;
    // CPUID serialises the instruction stream
    __asm__ volatile (
        "cpuid\n\t"
        "rdtsc"
        : "=a"(lo), "=d"(hi)
        : "a"(0)
        : "rbx", "rcx"
    );
    return (static_cast<UINT64>(hi) << 32) | lo;
}

bool HasInvariantTSC() {
    UINT32 eax, ebx, ecx, edx;
    // Check max extended CPUID leaf
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x80000000));
    if (eax < 0x80000007) return false;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x80000007));
    return (edx & (1 << 8)) != 0;  // Invariant TSC bit
}

void Calibrate() {
    if (!gBS) return;

    sInvariantTSC = HasInvariantTSC();

    // Take 3 samples of 100ms each, use the median
    constexpr int SAMPLES = 3;
    constexpr UINTN STALL_US = 100000; // 100 ms
    UINT64 measurements[SAMPLES];

    for (int s = 0; s < SAMPLES; ++s) {
        UINT64 start = SerializedRDTSC();
        gBS->Stall(STALL_US);
        UINT64 end = SerializedRDTSC();
        measurements[s] = (end - start) / STALL_US; // cycles per us
    }

    // Simple sort for median
    for (int i = 0; i < SAMPLES - 1; ++i)
        for (int j = i + 1; j < SAMPLES; ++j)
            if (measurements[j] < measurements[i]) {
                UINT64 tmp = measurements[i];
                measurements[i] = measurements[j];
                measurements[j] = tmp;
            }

    sCyclesPerUs = measurements[SAMPLES / 2]; // median
    sCalibrated = sCyclesPerUs > 0;
}

bool   IsCalibrated()    { return sCalibrated; }
UINT64 CyclesPerUs()     { return sCyclesPerUs; }
UINT64 ReadTSC()         { return SerializedRDTSC(); }

void Start() {
    sStartTick = SerializedRDTSC();
}

UINT64 ElapsedUs() {
    if (!sCalibrated || sCyclesPerUs == 0) return 0;
    UINT64 now = SerializedRDTSC();
    return (now - sStartTick) / sCyclesPerUs;
}

UINT64 ElapsedMs() {
    return ElapsedUs() / 1000;
}

} // namespace Timer
