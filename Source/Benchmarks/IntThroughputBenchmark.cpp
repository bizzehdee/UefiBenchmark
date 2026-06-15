// Integer ILP throughput benchmark.
// 8 independent 64-bit LCG chains keep all integer ALU ports busy.

#include "IntThroughputBenchmark.h"
#include "TimeBox.h"
#include "BenchmarkConstants.h"

void IntThroughputBenchmark::RunCore(UINT32 /*workerIndex*/, UINT32 /*totalWorkers*/) {
    // LCG multiplier (Knuth); each chain starts with a distinct seed so the
    // compiler cannot merge them into a single dependency chain.
    constexpr UINT64 M = LCG_KNUTH_A;

    UINT64 localIter = TimeBox::RunWithProgress(mBudgetUs, CHUNK_SIZE, [](UINT64 n) {
        UINT64 a0 = 1ULL, a1 = 2ULL, a2 = 3ULL, a3 = 4ULL;
        UINT64 a4 = 5ULL, a5 = 6ULL, a6 = 7ULL, a7 = 8ULL;

        for (UINT64 i = 0; i < n; ++i) {
            a0 = a0 * M + 1ULL;
            a1 = a1 * M + 2ULL;
            a2 = a2 * M + 3ULL;
            a3 = a3 * M + 4ULL;
            a4 = a4 * M + 5ULL;
            a5 = a5 * M + 6ULL;
            a6 = a6 * M + 7ULL;
            a7 = a7 * M + 8ULL;
        }

        // Volatile sink prevents dead-code elimination
        volatile UINT64 sink = a0 ^ a1 ^ a2 ^ a3 ^ a4 ^ a5 ^ a6 ^ a7;
        (void)sink;
    }, [this](UINT64 e, UINT64) { TryReportProgress(e); });

    __atomic_fetch_add(const_cast<UINT64*>(&mTotalIter), localIter, __ATOMIC_RELAXED);
}
