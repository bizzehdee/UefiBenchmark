// AI memory bandwidth benchmark.
// Sequential reads through the whole RAM partition assigned to this worker.
// Simulates loading LLM weights from DRAM sequentially.

#include "AiMemBenchmark.h"
#include "TimeBox.h"

void AiMemBenchmark::RunCore(UINT32 workerIndex, UINT32 totalWorkers) {
    ClearNote();
    auto* buf = BigBuffer::GetShared();
    if (!buf || buf->TotalSize() == 0) { SetNote("RAM buffer unavailable"); return; }

    // GetWorkerRange returns byte OFFSETS into the buffer; the buffer is a set of
    // discontiguous segments, so map the range to real (base,size) spans before
    // dereferencing. (Reading the raw offset as a pointer walks off into
    // unmapped memory / MMIO and faults mid-run.)
    UINT64 start, end;
    buf->GetWorkerRange(workerIndex, totalWorkers, &start, &end);
    if (end <= start) { SetNote("RAM buffer unavailable"); return; }

    constexpr UINT32 MAX_SPANS = 32;
    BigSegment spans[MAX_SPANS];
    UINT32 nSpans = buf->GetSpans(start, end, spans, MAX_SPANS);
    if (nSpans == 0) { SetNote("RAM buffer unavailable"); return; }

    // Bytes covered by one full pass (8-wide unroll floors each span to 8 slots).
    UINT64 passBytes = 0;
    for (UINT32 s = 0; s < nSpans; ++s) {
        UINT64 cnt = spans[s].Size / sizeof(UINT64);
        passBytes += (cnt & ~(UINT64)7) * sizeof(UINT64);
    }
    if (passBytes == 0) { SetNote("RAM buffer too small"); return; }

    // Read through every span repeatedly; 8 accumulators hide load latency.
    const UINT64 cyclesPerUs  = Timer::CyclesPerUs();
    const UINT64 budgetCycles = GetBudgetUs() * cyclesPerUs;
    const UINT64 t0           = Timer::ReadTSC();

    while ((Timer::ReadTSC() - t0) < budgetCycles) {
        UINT64 a0=0, a1=0, a2=0, a3=0, a4=0, a5=0, a6=0, a7=0;
        for (UINT32 s = 0; s < nSpans; ++s) {
            const UINT64* data = reinterpret_cast<const UINT64*>(spans[s].Base);
            UINT64 count = spans[s].Size / sizeof(UINT64);
            for (UINT64 i = 0; i + 8 <= count; i += 8) {
                a0 += data[i];     a1 += data[i+1];
                a2 += data[i+2];   a3 += data[i+3];
                a4 += data[i+4];   a5 += data[i+5];
                a6 += data[i+6];   a7 += data[i+7];
            }
        }
        volatile UINT64 sink = a0^a1^a2^a3^a4^a5^a6^a7;
        (void)sink;
        __atomic_fetch_add(const_cast<UINT64*>(&mTotalBytes), passBytes, __ATOMIC_RELAXED);
        TryReportProgress((Timer::ReadTSC() - t0) / cyclesPerUs);
    }
}
