// Full-RAM random-access latency benchmark.
// Builds a pointer-chase array spanning the BigBuffer using an LCG permutation.
// The LCG visits every 64-byte slot exactly once (full-period for power-of-2 N),
// written sequentially so setup is fast (~O(N) sequential writes).

#include "MemLatencyBenchmark.h"
#include "TimeBox.h"
#include "BenchmarkConstants.h"

void MemLatencyBenchmark::Setup() {
    BigBuffer::AddRef();
    auto* buf = BigBuffer::GetShared();
    if (!buf || buf->TotalSize() < 128) return;

    UINT64 total = buf->TotalSize();

    // Round slot count down to the largest power of 2 that fits
    UINT64 N = 1ULL;
    while ((N * 2ULL * 64ULL) <= total) N <<= 1;
    mSlotCount = N;

    // LCG parameters (Knuth). Full-period for any modulus = power of 2:
    //   a ≡ 1 (mod 4), c is odd
    const UINT64 a    = LCG_KNUTH_A;
    const UINT64 c    = LCG_KNUTH_C;
    const UINT64 mask = N - 1ULL;

    // Sequential write: slot[i] → address of slot[(i*a+c) & mask].
    // This builds a single Hamiltonian cycle through all N slots because
    // the LCG has full period N; overflow in i*a is harmless (unsigned wraps).
    for (UINT64 i = 0; i < N; ++i) {
        UINT64 next = (i * a + c) & mask;
        UINT64* slot = reinterpret_cast<UINT64*>(buf->SlotAddress(i));
        if (slot) *slot = reinterpret_cast<UINT64>(buf->SlotAddress(next));
    }

    mStartPtr = reinterpret_cast<UINT64*>(buf->SlotAddress(0));
}

void MemLatencyBenchmark::Teardown() {
    mStartPtr  = nullptr;
    mSlotCount = 0;
    BigBuffer::Release();
}

void MemLatencyBenchmark::Run() {
    ClearNote();
    if (!mStartPtr || mSlotCount == 0) { SetNote("RAM buffer unavailable"); return; }

    UINT64* ptr = mStartPtr;
    UINT64  N   = mSlotCount;

    // Use chunks smaller than a full lap so progress updates fire ~every second.
    // At ~90 ns/access, 1M accesses ≈ 90 ms — gives ~5-10 progress callbacks/s
    // before the rate limiter (500 ms) reduces it to ~2/s.
    UINT64 chunkSize = N > 1000000ULL ? N / 64ULL : N;
    if (chunkSize < 1024) chunkSize = 1024;

    TimeBox::RunWithProgress(GetBudgetUs(), chunkSize, [&](UINT64 n) {
        for (UINT64 i = 0; i < n; ++i)
            ptr = reinterpret_cast<UINT64*>(*ptr);
        __atomic_fetch_add(const_cast<UINT64*>(&mTotalAccesses), n, __ATOMIC_RELAXED);
    }, [this](UINT64 e, UINT64) { TryReportProgress(e); });

    // Force the final pointer to be materialized so the dependent chase above
    // is not dead-code-eliminated under -O2. Must be a volatile UINT64 *object*
    // (a real volatile store = a side effect); a volatile-pointer-typed local
    // would not keep the loop, leaving accesses huge and ns/access flooring to 0.
    volatile UINT64 sink = reinterpret_cast<UINT64>(ptr);
    (void)sink;
}
