// Each iteration runs a 1M-step LCG chain seeded from one of several distinct
// bit patterns and compares the result against the golden value computed at
// Setup() time. Integer arithmetic is exact, so any deviation is a genuine
// compute error. RunCore cycles through the patterns round-robin. Each core
// runs independently with its own pattern cursor; errors are counted atomically.

#include "StressCpuVerifyBenchmark.h"
#include "Timer.h"

static UINT64 ComputeChain(UINT64 state, UINT64 steps) {
    for (UINT64 i = 0; i < steps; ++i)
        state = state * LCG_KNUTH_A + LCG_KNUTH_C;
    return state;
}

void StressCpuVerifyBenchmark::Setup() {
    // Precompute the golden result for every seed pattern. Read-only during the
    // run, so all cores can safely compare against the shared goldens.
    for (UINT32 i = 0; i < PATTERN_COUNT; ++i)
        mGolden[i] = ComputeChain(PATTERNS[i], CHAIN_LENGTH);
}

void StressCpuVerifyBenchmark::RunCore(UINT32 /*workerIndex*/, UINT32 /*totalWorkers*/) {
    const UINT64 startTsc  = Timer::ReadTSC();
    const UINT64 cycPerUs  = Timer::IsCalibrated() ? Timer::CyclesPerUs() : 1;
    const UINT64 budgetTsc = mBudgetUs * cycPerUs;

    UINT64 localErrors = 0;
    UINT32 idx         = 0;  // per-core round-robin cursor over PATTERNS

    while ((Timer::ReadTSC() - startTsc) < budgetTsc) {
        mCurrentPattern = idx;  // publish for the live display
        if (ComputeChain(PATTERNS[idx], CHAIN_LENGTH) != mGolden[idx])
            ++localErrors;
        idx = (idx + 1) % PATTERN_COUNT;

        if (localErrors > 0) {
            __atomic_fetch_add(const_cast<UINT64*>(&mErrorCount), localErrors, __ATOMIC_RELAXED);
            localErrors = 0;
        }

        TryReportProgress((Timer::ReadTSC() - startTsc) / cycPerUs);
    }
}
