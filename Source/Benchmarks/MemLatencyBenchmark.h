#pragma once
// Full-RAM random-access latency benchmark via pointer-chase.
// Builds an LCG-based cyclic permutation spanning the entire BigBuffer
// so every access causes a cache miss and reaches DRAM.
// Single-core. Score: ns/access.

#include "LongBenchmarkBase.h"
#include "BigBuffer.h"

class MemLatencyBenchmark : public LongBenchmarkBase {
public:
    const char* GetName()        const override { return "Mem Random Latency"; }
    const char* GetDescription() const override {
        return "Pointer-chase across all RAM; measures full-DRAM random-access latency";
    }
    const char* GetCategory()    const override { return "Memory"; }

    ThreadingMode GetThreadingMode() const override { return ThreadingMode::SingleOnly; }

    UINT64 GetBudgetUs() const override { return mBudgetUs; }
    // ns/access = (budgetUs * 1000) / totalAccesses
    UINT64      GetScore() const override {
        return mTotalAccesses > 0 ? (mBudgetUs * 1000ULL) / mTotalAccesses : 0;
    }
    const char* GetUnit()  const override { return "ns/access"; }

    void Setup()    override;
    void Teardown() override;
    void PreRun()   override { mTotalAccesses = 0; }
    void Run()      override;

private:
    static constexpr UINT64 mBudgetUs = 120ULL * 1000000;

    UINT64* mStartPtr    = nullptr;
    UINT64  mSlotCount   = 0;
    UINT64  mTotalAccesses = 0;
};
