// AI cache hierarchy pointer-chase benchmark.
// Builds 4 pointer chains (L1/L2/L3/DRAM working sets) using an LCG permutation
// (full period for power-of-2 N). Chases all 4 chains in rotation each iteration.
// The 32 MB DRAM chain dominates elapsed time (pointer-chase cannot be prefetched).

#include "AiCacheBenchmark.h"
#include "Freestanding.h"  // operator new[]/delete[]

// LCG full-period parameters (Knuth): a≡1(mod4), c odd, modulus = power of 2
static constexpr UINT64 kLcgA = 6364136223846793005ULL;
static constexpr UINT64 kLcgC = 1442695040888963407ULL;

void AiCacheBenchmark::BuildChain(UINT64* buf, UINT64 n) {
    // slot[i] = address of slot[(i*a + c) & (n-1)].
    // Full-period LCG visits every slot exactly once, creating a single Hamiltonian cycle.
    const UINT64 mask = n - 1ULL;
    for (UINT64 i = 0; i < n; ++i) {
        UINT64 next = (i * kLcgA + kLcgC) & mask;
        buf[i] = reinterpret_cast<UINT64>(&buf[next]);
    }
}

void AiCacheBenchmark::Setup() {
    mL1   = new UINT64[kL1Slots];
    mL2   = new UINT64[kL2Slots];
    mL3   = new UINT64[kL3Slots];
    mDram = new UINT64[kDramSlots];

    if (mL1)   BuildChain(mL1,   kL1Slots);
    if (mL2)   BuildChain(mL2,   kL2Slots);
    if (mL3)   BuildChain(mL3,   kL3Slots);
    if (mDram) BuildChain(mDram, kDramSlots);
}

void AiCacheBenchmark::Teardown() {
    delete[] mL1;   mL1   = nullptr;
    delete[] mL2;   mL2   = nullptr;
    delete[] mL3;   mL3   = nullptr;
    delete[] mDram; mDram = nullptr;
}

void AiCacheBenchmark::Run() {
    if (!mL1 || !mL2 || !mL3 || !mDram) return;

    const UINT64 cyclesPerUs  = Timer::CyclesPerUs();
    const UINT64 budgetCycles = mBudgetUs * cyclesPerUs;
    const UINT64 t0           = Timer::ReadTSC();

    UINT64 pL1   = reinterpret_cast<UINT64>(mL1);
    UINT64 pL2   = reinterpret_cast<UINT64>(mL2);
    UINT64 pL3   = reinterpret_cast<UINT64>(mL3);
    UINT64 pDram = reinterpret_cast<UINT64>(mDram);

    UINT64 totalAccesses = 0;

    while ((Timer::ReadTSC() - t0) < budgetCycles) {
        // Chase each chain for kChunk steps — the DRAM chain dominates iteration time.
        for (UINT64 i = 0; i < kChunk; ++i) pL1   = *reinterpret_cast<UINT64*>(pL1);
        for (UINT64 i = 0; i < kChunk; ++i) pL2   = *reinterpret_cast<UINT64*>(pL2);
        for (UINT64 i = 0; i < kChunk; ++i) pL3   = *reinterpret_cast<UINT64*>(pL3);
        for (UINT64 i = 0; i < kChunk; ++i) pDram = *reinterpret_cast<UINT64*>(pDram);
        totalAccesses += kChunk * 4;

        TryReportProgress((Timer::ReadTSC() - t0) / cyclesPerUs);
    }

    // Prevent DCE of the pointer chases
    volatile UINT64 sink = pL1 ^ pL2 ^ pL3 ^ pDram;
    (void)sink;

    mTotalAccesses = totalAccesses;
}
