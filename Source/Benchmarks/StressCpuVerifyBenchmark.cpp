// Each iteration runs a 1M-step LCG chain starting from a fixed seed and
// compares the result against the golden value computed at Setup() time.
// Integer arithmetic is exact, so any deviation is a genuine compute error.
// Each core runs independently; errors are counted atomically.

#include "StressCpuVerifyBenchmark.h"
#include "Timer.h"

static UINT64 ComputeChain(UINT64 state, UINT64 steps) {
    for (UINT64 i = 0; i < steps; ++i)
        state = state * LCG_KNUTH_A + LCG_KNUTH_C;
    return state;
}

void StressCpuVerifyBenchmark::Setup() {
    mGolden = ComputeChain(SEED, CHAIN_LENGTH);
}

void StressCpuVerifyBenchmark::RunCore(UINT32 /*workerIndex*/, UINT32 /*totalWorkers*/) {
    const UINT64 startTsc  = Timer::ReadTSC();
    const UINT64 cycPerUs  = Timer::IsCalibrated() ? Timer::CyclesPerUs() : 1;
    const UINT64 budgetTsc = mBudgetUs * cycPerUs;

    UINT64 localErrors = 0;

    while ((Timer::ReadTSC() - startTsc) < budgetTsc) {
        if (ComputeChain(SEED, CHAIN_LENGTH) != mGolden)
            ++localErrors;

        if (localErrors > 0) {
            __atomic_fetch_add(const_cast<UINT64*>(&mErrorCount), localErrors, __ATOMIC_RELAXED);
            localErrors = 0;
        }

        TryReportProgress((Timer::ReadTSC() - startTsc) / cycPerUs);
    }
}
