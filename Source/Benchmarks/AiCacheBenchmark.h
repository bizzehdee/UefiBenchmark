#pragma once
// AI cache hierarchy benchmark.
// Pointer-chase through 4 working-set sizes (L1/L2/L3/DRAM) to measure
// cache-latency characteristics relevant to KV-cache and attention in LLMs.
// Single-core. Score: normalized AI pts (1000 = AMD Ryzen 7 5800X).

#include "LongBenchmarkBase.h"
#include "AiScore.h"

class AiCacheBenchmark : public LongBenchmarkBase {
public:
    const char* GetName()        const override { return "AI Cache Hierarchy"; }
    const char* GetDescription() const override {
        return "Pointer-chase at L1/L2/L3/DRAM sizes; simulates KV-cache access (90s)";
    }
    const char* GetCategory()    const override { return "AI"; }

    ThreadingMode  GetThreadingMode()  const override { return ThreadingMode::SingleOnly; }
    UINT32         GetCategoryWeight() const override { return AI_WEIGHT_CACHE; }

    UINT64 GetBudgetUs() const override { return mBudgetUs; }
    UINT64      GetScore() const override {
        return mTotalAccesses > 0
               ? mTotalAccesses * 1000 / mBudgetUs / AI_REF_CACHE_MACCS
               : 0;
    }
    const char* GetUnit()  const override { return "AI pts"; }

    void Setup()    override;
    void Teardown() override;
    void PreRun()   override { mTotalAccesses = 0; }
    void Run()      override;

private:
    static constexpr UINT64 mBudgetUs = 90ULL * 1000000;
    static constexpr UINT64 kChunk    = 4096ULL; // accesses per chain per iteration

    // Working-set sizes (slots of sizeof(UINT64) = 8 bytes)
    static constexpr UINT64 kL1Slots   =  16ULL * 1024 / 8;   //  16 KB
    static constexpr UINT64 kL2Slots   = 256ULL * 1024 / 8;   // 256 KB
    static constexpr UINT64 kL3Slots   =   4ULL * 1024 * 1024 / 8;  //  4 MB
    static constexpr UINT64 kDramSlots =  32ULL * 1024 * 1024 / 8;  // 32 MB

    // Each slot stores the address of the next slot (pointer-chase)
    UINT64* mL1   = nullptr;
    UINT64* mL2   = nullptr;
    UINT64* mL3   = nullptr;
    UINT64* mDram = nullptr;

    UINT64 mTotalAccesses = 0;

    static void BuildChain(UINT64* buf, UINT64 n);
};
