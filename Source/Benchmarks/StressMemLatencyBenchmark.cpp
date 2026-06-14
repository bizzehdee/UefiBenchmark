// Large-stride write+verify stress. Accessing one 8-byte slot per 64 KB of
// RAM ensures consecutive accesses always fall on different DRAM rows, forcing
// the memory controller to activate a new row every access. This is far more
// stressful to timing margins (tRCD, tRP, tRAS) than a sequential scan.

#include "StressMemLatencyBenchmark.h"
#include "Timer.h"

void StressMemLatencyBenchmark::RunCore(UINT32 /*workerIndex*/, UINT32 /*totalWorkers*/) {
    auto* buf = BigBuffer::GetShared();
    if (!buf || buf->TotalSize() == 0) return;

    // Single-core: use the entire buffer
    constexpr UINT32 MAX_SPANS = 64;
    BigSegment spans[MAX_SPANS];
    UINT32 nSpans = buf->GetSpans(0, buf->TotalSize(), spans, MAX_SPANS);

    const UINT64 startTsc  = Timer::ReadTSC();
    const UINT64 cycPerUs  = Timer::IsCalibrated() ? Timer::CyclesPerUs() : 1;
    const UINT64 budgetTsc = mBudgetUs * cycPerUs;

    UINT64 localErrors = 0;

    while ((Timer::ReadTSC() - startTsc) < budgetTsc) {
        // Write pass: one 8-byte slot per STRIDE bytes within each segment
        for (UINT32 s = 0; s < nSpans; ++s) {
            UINT8* base = spans[s].Base;
            UINT64 size = spans[s].Size;
            for (UINT64 off = 0; off + 8 <= size; off += STRIDE) {
                UINT64* qw = reinterpret_cast<UINT64*>(base + off);
                *qw = reinterpret_cast<UINT64>(qw) ^ MAGIC;
            }
        }

        // Verify pass: read and compare at the same stride
        for (UINT32 s = 0; s < nSpans; ++s) {
            UINT8* base = spans[s].Base;
            UINT64 size = spans[s].Size;
            for (UINT64 off = 0; off + 8 <= size; off += STRIDE) {
                UINT64* qw = reinterpret_cast<UINT64*>(base + off);
                if (*qw != (reinterpret_cast<UINT64>(qw) ^ MAGIC))
                    ++localErrors;
            }
        }

        if (localErrors > 0) {
            __atomic_fetch_add(const_cast<UINT64*>(&mErrorCount), localErrors, __ATOMIC_RELAXED);
            localErrors = 0;
        }

        TryReportProgress((Timer::ReadTSC() - startTsc) / cycPerUs);
    }
}
