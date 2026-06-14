// Sequential write+verify stress test. Each 8-byte slot is written with its
// own pointer address XOR'd with a magic constant; the verify pass reads back
// and compares. This detects bit flips, addressing errors, and marginal
// data-clock stability that a single-pass integrity test might miss.

#include "StressMemClockBenchmark.h"
#include "Timer.h"

void StressMemClockBenchmark::RunCore(UINT32 workerIndex, UINT32 totalWorkers) {
    auto* buf = BigBuffer::GetShared();
    if (!buf || buf->TotalSize() == 0) return;

    UINT64 start, end;
    buf->GetWorkerRange(workerIndex, totalWorkers, &start, &end);

    constexpr UINT32 MAX_SPANS = 32;
    BigSegment spans[MAX_SPANS];
    UINT32 nSpans = buf->GetSpans(start, end, spans, MAX_SPANS);

    const UINT64 startTsc  = Timer::ReadTSC();
    const UINT64 cycPerUs  = Timer::IsCalibrated() ? Timer::CyclesPerUs() : 1;
    const UINT64 budgetTsc = mBudgetUs * cycPerUs;

    UINT64 localErrors = 0;
    UINT64 localBytes  = 0;

    while ((Timer::ReadTSC() - startTsc) < budgetTsc) {
        // Write pass: store (ptr_addr ^ MAGIC) at each 8-byte slot
        for (UINT32 s = 0; s < nSpans; ++s) {
            UINT64* ptr = reinterpret_cast<UINT64*>(spans[s].Base);
            UINT64* lim = ptr + (spans[s].Size >> 3);
            while (ptr < lim) {
                *ptr = reinterpret_cast<UINT64>(ptr) ^ MAGIC;
                ++ptr;
            }
            localBytes += spans[s].Size & ~7ULL;
        }

        // Verify pass: read back and compare
        for (UINT32 s = 0; s < nSpans; ++s) {
            UINT64* ptr = reinterpret_cast<UINT64*>(spans[s].Base);
            UINT64* lim = ptr + (spans[s].Size >> 3);
            while (ptr < lim) {
                if (*ptr != (reinterpret_cast<UINT64>(ptr) ^ MAGIC))
                    ++localErrors;
                ++ptr;
            }
            localBytes += spans[s].Size & ~7ULL;
        }

        __atomic_fetch_add(const_cast<UINT64*>(&mTotalBytes), localBytes, __ATOMIC_RELAXED);
        localBytes = 0;

        if (localErrors > 0) {
            __atomic_fetch_add(const_cast<UINT64*>(&mErrorCount), localErrors, __ATOMIC_RELAXED);
            localErrors = 0;
        }

        TryReportProgress((Timer::ReadTSC() - startTsc) / cycPerUs);
    }
}
