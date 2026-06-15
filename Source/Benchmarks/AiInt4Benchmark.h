#pragma once
// AI INT4 packed GEMM throughput benchmark.
// Simulates INT4-quantized weight loading by packing 2 int4 values per byte in matrix A.
// Inner loop unpacks each row to INT8 before computing the dot product (AVX2 or scalar).
// Score: normalized AI pts (1000 = AMD Ryzen 7 5800X).

#include "LongBenchmarkBase.h"
#include "AiScore.h"

class AiInt4Benchmark : public LongBenchmarkBase {
public:
    const char* GetName()        const override { return "AI INT4 GEMM"; }
    const char* GetDescription() const override {
        return "INT4-packed GEMM (N=32): unpack 2×4-bit per byte + INT8 dot product (90s)";
    }
    const char* GetCategory()    const override { return "AI"; }

    ThreadingMode  GetThreadingMode()  const override { return ThreadingMode::Either; }
    UINT32         GetCategoryWeight() const override { return AI_WEIGHT_INT4; }

    UINT64 GetBudgetUs() const override { return mBudgetUs; }
    UINT64      GetScore() const override {
        return mTotalOps > 0 ? (mTotalOps / mBudgetUs) * 1000 / AI_REF_INT4_MOPS : 0;
    }
    const char* GetUnit()  const override { return "AI pts"; }

    void PreRun()  override { mTotalOps = 0; }
    void Run()     override { RunCore(0, 1); }
    void RunCore(UINT32 workerIndex, UINT32 totalWorkers) override;

    static constexpr int kN          = 32;
    static constexpr int kPackedN    = kN / 2;   // 16 packed bytes per row
    static constexpr int MAX_WORKERS = 32;

private:
    static constexpr UINT64 mBudgetUs  = 90ULL * US_PER_SECOND;
    static constexpr UINT64 CHUNK_SIZE = 50ULL;

    // A stored packed: 2 int4 values per byte, low nibble first
    static UINT8 sPackedA[MAX_WORKERS][kN * kPackedN];
    static INT8  sBT     [MAX_WORKERS][kN * kN];
    static INT32 sC      [MAX_WORKERS][kN * kN];
    static INT8  sUnpacked[MAX_WORKERS][kN];  // per-row unpack scratch

    volatile UINT64 mTotalOps = 0;
};
