#pragma once
// AI INT8 GEMM throughput benchmark.
// Measures sustained INT8 matrix multiply throughput across all cores.
// Uses N=32 matrices (L1-resident per core). AVX2 path + scalar fallback.
// Score: normalized AI pts (1000 = AMD Ryzen 7 5800X).

#include "LongBenchmarkBase.h"
#include "AiScore.h"

class AiInt8Benchmark : public LongBenchmarkBase {
public:
    const char* GetName()        const override { return "AI INT8 GEMM"; }
    const char* GetDescription() const override {
        return "INT8 matrix multiply (N=32) across all cores; AVX2 or scalar (90s)";
    }
    const char* GetCategory()    const override { return "AI"; }

    ThreadingMode  GetThreadingMode()  const override { return ThreadingMode::Either; }
    UINT32         GetCategoryWeight() const override { return AI_WEIGHT_INT8; }

    UINT64 GetBudgetUs() const override { return mBudgetUs; }
    UINT64      GetScore() const override {
        return mTotalOps > 0 ? (mTotalOps / mBudgetUs) * 1000 / AI_REF_INT8_MOPS : 0;
    }
    const char* GetUnit()  const override { return "AI pts"; }

    void PreRun()  override { mTotalOps = 0; }
    void Run()     override { RunCore(0, 1); }
    void RunCore(UINT32 workerIndex, UINT32 totalWorkers) override;

    static constexpr int kN          = 32;
    static constexpr int MAX_WORKERS = 32;

private:
    static constexpr UINT64 mBudgetUs  = 90ULL * US_PER_SECOND;
    static constexpr UINT64 CHUNK_SIZE = 50ULL;  // GEMMs per TimeBox chunk

    static INT8  sA[MAX_WORKERS][kN * kN];
    static INT8  sBT[MAX_WORKERS][kN * kN];
    static INT32 sC[MAX_WORKERS][kN * kN];

    volatile UINT64 mTotalOps = 0;
};
