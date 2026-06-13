// AI memory bandwidth benchmark.
// Sequential reads through the whole RAM partition assigned to this worker.
// Simulates loading LLM weights from DRAM sequentially.

#include "AiMemBenchmark.h"
#include "TimeBox.h"

void AiMemBenchmark::RunCore(UINT32 workerIndex, UINT32 totalWorkers) {
    auto* buf = BigBuffer::GetShared();
    if (!buf) return;

    UINT64 start, end;
    buf->GetWorkerRange(workerIndex, totalWorkers, &start, &end);
    UINT64 size = end - start;
    if (size == 0) return;

    const UINT64* data  = reinterpret_cast<const UINT64*>(start);
    UINT64         count = size / sizeof(UINT64);

    // Read through the partition repeatedly; accumulate bytes read.
    // Use 8 accumulators to hide load latency across loop iterations.
    const UINT64 cyclesPerUs  = Timer::CyclesPerUs();
    const UINT64 budgetCycles = mBudgetUs * cyclesPerUs;
    const UINT64 t0           = Timer::ReadTSC();

    UINT64 totalBytes = 0;
    while ((Timer::ReadTSC() - t0) < budgetCycles) {
        UINT64 a0=0, a1=0, a2=0, a3=0, a4=0, a5=0, a6=0, a7=0;
        for (UINT64 i = 0; i + 8 <= count; i += 8) {
            a0 += data[i];     a1 += data[i+1];
            a2 += data[i+2];   a3 += data[i+3];
            a4 += data[i+4];   a5 += data[i+5];
            a6 += data[i+6];   a7 += data[i+7];
        }
        volatile UINT64 sink = a0^a1^a2^a3^a4^a5^a6^a7;
        (void)sink;
        totalBytes += size;
        TryReportProgress((Timer::ReadTSC() - t0) / cyclesPerUs);
    }

    __atomic_fetch_add(const_cast<UINT64*>(&mTotalBytes), totalBytes, __ATOMIC_RELAXED);
}
