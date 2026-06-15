#pragma once
// Integer ILP throughput: 8 independent 64-bit accumulator chains.
// Time-boxed, multi-core. Score: MOPS.

#include "LongBenchmarkBase.h"

class IntThroughputBenchmark : public LongBenchmarkBase {
public:
    const char* GetName()        const override { return "Int Throughput (ILP)"; }
    const char* GetDescription() const override {
        return "8 independent 64-bit add/shift/and/xor chains; fills all ALU ports (180s)";
    }
    const char* GetCategory()    const override { return "CPU"; }

    ThreadingMode GetThreadingMode() const override { return ThreadingMode::MultiOnly; }

    UINT64 GetBudgetUs() const override { return mBudgetUs; }
    UINT64      GetScore() const override { return mTotalIter / mBudgetUs; }
    const char* GetUnit()  const override { return "MOPS"; }

    void PreRun()  override { mTotalIter = 0; }
    void Run()     override { RunCore(0, 1); }
    void RunCore(UINT32 workerIndex, UINT32 totalWorkers) override;

private:
    static constexpr UINT64 mBudgetUs  = 180ULL * US_PER_SECOND;
    static constexpr UINT64 CHUNK_SIZE = 1000000ULL; // 1M ops per chunk
    volatile UINT64 mTotalIter = 0;
};
