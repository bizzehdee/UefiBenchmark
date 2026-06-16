#pragma once
// L3 Cache Cliff: random-access latency sweep across power-of-2 working-set
// sizes (1 MB .. 128 MB). Single-core dependent pointer-chase over a full-period
// LCG permutation (one access per 64-byte line, randomised so the prefetcher
// can't help). Latency stays flat while the working set fits in cache and jumps
// to DRAM once it spills — the size at which it jumps reveals the L3 capacity.
//
// This is the test that separates otherwise-identical parts that differ only in
// L3 size, e.g. Ryzen 7 5800X (32 MB L3) vs 5800X3D (96 MB V-Cache): at a 64 MB
// working set the 5800X is in DRAM (~80-95 ns) while the 5800X3D is still in L3
// (~14-20 ns). Headline score is ns/access at 64 MB.

#include "LongBenchmarkBase.h"
#include "BenchmarkConstants.h"

class L3CacheCliffBenchmark : public LongBenchmarkBase {
public:
    const char* GetName()        const override { return "L3 Cache Cliff"; }
    const char* GetDescription() const override {
        return "Random-access latency 1-128MB; reveals L3 size";
    }
    const char* GetCategory()    const override { return "Memory"; }

    ThreadingMode GetThreadingMode()       const override { return ThreadingMode::SingleOnly; }
    bool          IncludeInCategoryScore() const override { return false; }  // latency: lower is better

    UINT64 GetBudgetUs() const override;
    UINT64      GetScore() const override { return mHeadlineNs; }  // ns/access at the headline size
    const char* GetUnit()  const override { return "ns/access"; }
    // Cliff factor x10: latency(largest) / latency(smallest). ~70 (7.0x) on a
    // 5800X, ~15 (1.5x) on a 5800X3D — a single-number summary of the cliff.
    UINT64      GetRawMetric() const override { return mCliffX10; }
    const char* GetRawUnit()   const override { return "cliff/10"; }
    const char* GetStatus()    const override { return mStatus[0] ? mStatus : nullptr; }

    UINT32 GetSweep(UINT32* sizesMB, UINT64* values, UINT32 maxN) const override;

    void Setup()    override;
    void Teardown() override;
    void PreRun()   override;
    void Run()      override;

private:
    static constexpr UINT64 MAX_BYTES   = 128ULL * BYTES_PER_MB;
    static constexpr UINT64 MIN_BYTES   =   8ULL * BYTES_PER_MB;
    static constexpr UINT64 PER_SIZE_US = 250000ULL;   // timed window per size
    static constexpr UINT32 HEADLINE_MB = 64;
    static constexpr UINT32 MAX_POINTS  = 8;            // 1,2,4,8,16,32,64,128

    void BuildChain(UINT64 nSlots);
    void SetStatus(UINT64 sizeMB, UINT64 ns);

    UINT8*  mBuf      = nullptr;
    UINT64  mBufBytes = 0;

    volatile UINT32 mCount      = 0;
    UINT32          mSizeMB[MAX_POINTS] = {};
    volatile UINT64 mNs[MAX_POINTS]     = {};
    volatile UINT64 mHeadlineNs = 0;
    volatile UINT64 mCliffX10   = 0;
    char            mStatus[48] = {};
};
