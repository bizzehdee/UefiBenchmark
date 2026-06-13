// Scalar FP + divide benchmark.
// Mix of add, mul, div on doubles with dependency to prevent elimination.
// Division is kept in the chain to stress the slow divider unit.

#include "FpScalarBenchmark.h"
#include "TimeBox.h"
#include <emmintrin.h>   // _mm_sqrt_sd (SSE2 baseline)

void FpScalarBenchmark::RunCore(UINT32 /*workerIndex*/, UINT32 /*totalWorkers*/) {
    UINT64 localIter = TimeBox::RunWithProgress(mBudgetUs, CHUNK_SIZE, [](UINT64 n) {
        double x = 1.234567890123456;
        double y = 0.987654321098765;
        const double c1 = 1.000001;
        const double c2 = 0.999998;
        const double c3 = 1.111111;

        for (UINT64 i = 0; i < n; ++i) {
            x = x + c1;
            x = x * c2;
            // Division every 4 iters — stresses the FP divider
            if ((i & 3) == 0) x = x / c3;
            y = y + x * c2;
        }

        volatile double sink = x + y;
        (void)sink;
    }, [this](UINT64 e, UINT64) { TryReportProgress(e); });

    __atomic_fetch_add(const_cast<UINT64*>(&mTotalIter), localIter, __ATOMIC_RELAXED);
}
