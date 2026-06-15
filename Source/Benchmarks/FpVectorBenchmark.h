#pragma once
// AVX2/FMA 256-bit vector FP throughput benchmark.
// Requires AVX state enabled via CpuFeatures::EnableAvxState().
// Multi-core, time-boxed. Score: GFLOP/s.

#include "LongBenchmarkBase.h"

class FpVectorBenchmark : public LongBenchmarkBase {
public:
    const char* GetName()        const override { return "FP Vector (AVX2/FMA)"; }
    const char* GetDescription() const override {
        return "256-bit FMA throughput: 10 YMM accumulators, needs AVX2+FMA (180s)";
    }
    const char* GetCategory()    const override { return "CPU"; }

    ThreadingMode GetThreadingMode() const override { return ThreadingMode::MultiOnly; }

    UINT64 GetBudgetUs() const override { return mBudgetUs; }
    // 10 accumulators × 4 doubles × 2 flops/FMA = 80 flops per iteration
    // Score in GFLOP/s = flops / budgetUs / 1000
    UINT64      GetScore() const override { return (mTotalIter * 80ULL) / mBudgetUs / 1000ULL; }
    const char* GetUnit()  const override { return "GFLOP/s"; }

    void PreRun()  override { mTotalIter = 0; }
    void Run()     override { RunCore(0, 1); }
    void RunCore(UINT32 workerIndex, UINT32 totalWorkers) override;

private:
    static constexpr UINT64 mBudgetUs  = 180ULL * US_PER_SECOND;
    static constexpr UINT64 CHUNK_SIZE = 100000ULL;
    volatile UINT64 mTotalIter = 0;
};
