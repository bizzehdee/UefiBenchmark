#pragma once
// AI memory bandwidth benchmark.
// Measures sequential read throughput across all RAM to simulate LLM weight streaming.
// Multi-core, uses BigBuffer. Score: normalized AI pts (1000 = AMD Ryzen 7 5800X).

#include "LongBenchmarkBase.h"
#include "BigBuffer.h"
#include "AiScore.h"

class AiMemBenchmark : public LongBenchmarkBase {
public:
    const char* GetName()        const override { return "AI Memory Bandwidth"; }
    const char* GetDescription() const override {
        return "Sequential read across all RAM; simulates LLM weight streaming (90s)";
    }
    const char* GetCategory()    const override { return "AI"; }

    ThreadingMode  GetThreadingMode()  const override { return ThreadingMode::MultiOnly; }
    UINT32         GetCategoryWeight() const override { return AI_WEIGHT_MEM; }

    UINT64 GetBudgetUs() const override { return mBudgetUs; }
    UINT64      GetScore() const override {
        return mTotalBytes > 0 ? (mTotalBytes / mBudgetUs) * 1000 / AI_REF_MEM_MBS : 0;
    }
    const char* GetUnit()  const override { return "AI pts"; }

    void Setup()    override { BigBuffer::AddRef(); }
    void Teardown() override { BigBuffer::Release(); }
    void PreRun()   override { mTotalBytes = 0; }
    void Run()      override { RunCore(0, 1); }
    void RunCore(UINT32 workerIndex, UINT32 totalWorkers) override;

private:
    static constexpr UINT64 mBudgetUs = 90ULL * 1000000;
    volatile UINT64 mTotalBytes = 0;
};
