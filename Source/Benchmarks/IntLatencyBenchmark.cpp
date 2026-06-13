// Integer serial-latency benchmark.
// A single dependency chain (each op feeds the next) so only one in-flight
// operation exists — measures per-op latency, not throughput.

#include "IntLatencyBenchmark.h"
#include "TimeBox.h"

void IntLatencyBenchmark::Run() {
    mTotalIter = TimeBox::RunWithProgress(mBudgetUs, CHUNK_SIZE, [](UINT64 n) {
        UINT64 x = 0x123456789ABCDEF0ULL;
        for (UINT64 i = 0; i < n; ++i) {
            x = x + (x >> 3);    // add — uses result of previous iter
            x = x * 0x9E3779B97F4A7C15ULL;
            x = x ^ (x << 13);
            x = x - (x >> 7);
        }
        volatile UINT64 sink = x;
        (void)sink;
    }, [this](UINT64 e, UINT64) { TryReportProgress(e); });
}
