// RAM integrity march test.
// Four patterns (0x00, 0xFF, 0xAA, 0x55): write all, then read back and verify.
// Mismatches are counted (not address-recorded, to stay stack/memory-safe).
// Progress is reported after each span in the verify pass; byte counts are
// flushed atomically per-span so GetScore() reflects live throughput.

#include "MemIntegrityBenchmark.h"
#include "Timer.h"

static const UINT8 kPatterns[] = { 0x00, 0xFF, 0xAA, 0x55 };
static constexpr int N_PATTERNS = 4;

void MemIntegrityBenchmark::RunCore(UINT32 workerIndex, UINT32 totalWorkers) {
    ClearNote();
    auto* buf = BigBuffer::GetShared();
    if (!buf || buf->TotalSize() == 0) { SetNote("RAM buffer unavailable"); return; }

    UINT64 start, end;
    buf->GetWorkerRange(workerIndex, totalWorkers, &start, &end);

    constexpr UINT32 MAX_SPANS = 32;
    BigSegment spans[MAX_SPANS];
    UINT32 nSpans = buf->GetSpans(start, end, spans, MAX_SPANS);

    const UINT64 startTsc = Timer::ReadTSC();
    const UINT64 cycPerUs = Timer::IsCalibrated() ? Timer::CyclesPerUs() : 1;

    UINT64 localErrors = 0;

    for (int p = 0; p < N_PATTERNS; ++p) {
        UINT8 pat = kPatterns[p];

        // Write pass — count bytes immediately so live score is accurate
        for (UINT32 s = 0; s < nSpans; ++s) {
            UINT8*  base = spans[s].Base;
            UINT64  size = spans[s].Size;
            for (UINT64 i = 0; i < size; ++i)
                base[i] = pat;
            __atomic_fetch_add(const_cast<UINT64*>(&mTotalBytes), size, __ATOMIC_RELAXED);
        }

        // Read + verify pass — report progress after each span
        for (UINT32 s = 0; s < nSpans; ++s) {
            UINT8*  base = spans[s].Base;
            UINT64  size = spans[s].Size;
            for (UINT64 i = 0; i < size; ++i) {
                if (base[i] != pat) ++localErrors;
            }
            __atomic_fetch_add(const_cast<UINT64*>(&mTotalBytes), size, __ATOMIC_RELAXED);

            // Flush errors early so display can show non-zero counts promptly
            if (localErrors > 0) {
                __atomic_fetch_add(const_cast<UINT64*>(&mErrorCount), localErrors, __ATOMIC_RELAXED);
                localErrors = 0;
            }

            UINT64 elapsedUs = (Timer::ReadTSC() - startTsc) / cycPerUs;
            TryReportProgress(elapsedUs);
        }
    }

    // Flush any remaining errors
    if (localErrors > 0) {
        __atomic_fetch_add(const_cast<UINT64*>(&mErrorCount), localErrors, __ATOMIC_RELAXED);
    }
}
