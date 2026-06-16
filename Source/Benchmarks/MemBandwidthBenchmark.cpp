// Sequential memory bandwidth benchmarks.
// Uses BigBuffer (whole RAM). Non-temporal stores bypass the cache for true
// DRAM write bandwidth; reads use a reduction to prevent hoisting.

#include "MemBandwidthBenchmark.h"
#include "TimeBox.h"
#include <emmintrin.h>   // _mm_stream_si128, _mm_load_si128 (SSE2 baseline)

// ── SeqWrite (non-temporal stores) ───────────────────────────

void MemSeqWriteBenchmark::RunCore(UINT32 workerIndex, UINT32 totalWorkers) {
    ClearNote();
    SetIsa("SSE2 NT");  // non-temporal streaming stores
    auto* buf = BigBuffer::GetShared();
    if (!buf || buf->TotalSize() == 0) { SetNote("RAM buffer unavailable"); return; }

    UINT64 start, end;
    buf->GetWorkerRange(workerIndex, totalWorkers, &start, &end);

    constexpr UINT32 MAX_SPANS = 32;
    BigSegment spans[MAX_SPANS];
    UINT32 nSpans = buf->GetSpans(start, end, spans, MAX_SPANS);

    const __m128i pattern  = _mm_set1_epi8(0x55);
    const UINT64  chunkBytes = end - start;

    // Warm-up pass (not counted in score)
    for (UINT32 s = 0; s < nSpans; ++s) {
        UINT8* base = spans[s].Base;
        UINT64 size = spans[s].Size & ~static_cast<UINT64>(15);
        for (UINT64 i = 0; i < size; i += 16)
            _mm_stream_si128(reinterpret_cast<__m128i*>(base + i), pattern);
    }
    _mm_sfence();

    // Timed passes — accumulate bytes inside lambda so GetScore() shows live data
    TimeBox::RunWithProgress(GetBudgetUs(), 1, [&](UINT64) {
        for (UINT32 s = 0; s < nSpans; ++s) {
            UINT8* base = spans[s].Base;
            UINT64 size = spans[s].Size & ~static_cast<UINT64>(15);
            for (UINT64 i = 0; i < size; i += 16)
                _mm_stream_si128(reinterpret_cast<__m128i*>(base + i), pattern);
        }
        _mm_sfence();
        __atomic_fetch_add(const_cast<UINT64*>(&mTotalBytes), chunkBytes, __ATOMIC_RELAXED);
    }, [this](UINT64 e, UINT64) { TryReportProgress(e); });
}

// ── SeqRead ───────────────────────────────────────────────────

void MemSeqReadBenchmark::RunCore(UINT32 workerIndex, UINT32 totalWorkers) {
    ClearNote();
    SetIsa("SSE2");
    auto* buf = BigBuffer::GetShared();
    if (!buf || buf->TotalSize() == 0) { SetNote("RAM buffer unavailable"); return; }

    UINT64 start, end;
    buf->GetWorkerRange(workerIndex, totalWorkers, &start, &end);

    constexpr UINT32 MAX_SPANS = 32;
    BigSegment spans[MAX_SPANS];
    UINT32 nSpans = buf->GetSpans(start, end, spans, MAX_SPANS);
    const UINT64 chunkBytes = end - start;

    TimeBox::RunWithProgress(GetBudgetUs(), 1, [&](UINT64) {
        __m128i acc = _mm_setzero_si128();
        for (UINT32 s = 0; s < nSpans; ++s) {
            UINT8* base = spans[s].Base;
            UINT64 size = spans[s].Size & ~static_cast<UINT64>(15);
            for (UINT64 i = 0; i < size; i += 16)
                acc = _mm_add_epi8(acc, _mm_load_si128(reinterpret_cast<const __m128i*>(base + i)));
        }
        volatile __m128i sink = acc;
        (void)sink;
        __atomic_fetch_add(const_cast<UINT64*>(&mTotalBytes), chunkBytes, __ATOMIC_RELAXED);
    }, [this](UINT64 e, UINT64) { TryReportProgress(e); });
}

// ── Copy ─────────────────────────────────────────────────────
// Uses the first half of the worker's range as source and second half as dest.

void MemCopyBenchmark::RunCore(UINT32 workerIndex, UINT32 totalWorkers) {
    ClearNote();
    SetIsa("SSE2 NT");  // non-temporal streaming copy
    auto* buf = BigBuffer::GetShared();
    if (!buf || buf->TotalSize() == 0) { SetNote("RAM buffer unavailable"); return; }

    UINT64 start, end;
    buf->GetWorkerRange(workerIndex, totalWorkers, &start, &end);

    UINT64 halfSize = ((end - start) / 2) & ~static_cast<UINT64>(15);
    if (halfSize < 16) { SetNote("RAM buffer too small"); return; }

    constexpr UINT32 MAX_SPANS = 32;
    BigSegment srcSpans[MAX_SPANS], dstSpans[MAX_SPANS];
    UINT32 nSrc = buf->GetSpans(start,            start + halfSize, srcSpans, MAX_SPANS);
    UINT32 nDst = buf->GetSpans(start + halfSize, start + halfSize * 2, dstSpans, MAX_SPANS);

    TimeBox::RunWithProgress(GetBudgetUs(), 1, [&](UINT64) {
        UINT32 si = 0, di = 0;
        UINT64 sOff = 0, dOff = 0;

        while (si < nSrc && di < nDst) {
            UINT64 sAvail = srcSpans[si].Size - sOff;
            UINT64 dAvail = dstSpans[di].Size - dOff;
            UINT64 chunk  = (sAvail < dAvail ? sAvail : dAvail) & ~static_cast<UINT64>(15);
            if (chunk == 0) { break; }

            const __m128i* s = reinterpret_cast<const __m128i*>(srcSpans[si].Base + sOff);
            __m128i*       d = reinterpret_cast<__m128i*>(dstSpans[di].Base + dOff);
            for (UINT64 i = 0; i < chunk; i += 16)
                _mm_stream_si128(d + i/16, _mm_load_si128(s + i/16));

            sOff += chunk; dOff += chunk;
            if (sOff >= srcSpans[si].Size) { ++si; sOff = 0; }
            if (dOff >= dstSpans[di].Size) { ++di; dOff = 0; }
        }
        _mm_sfence();
        __atomic_fetch_add(const_cast<UINT64*>(&mTotalBytes), halfSize, __ATOMIC_RELAXED);
    }, [this](UINT64 e, UINT64) { TryReportProgress(e); });
}
