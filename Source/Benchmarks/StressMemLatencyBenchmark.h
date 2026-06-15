#pragma once
// Memory latency soak: large-stride write+verify across the whole BigBuffer,
// maximising DRAM row-activation pressure. Targets marginal command/timing
// margins that sequential streaming doesn't stress. Runs 30 min. 0 errors = stable.

#include "LongBenchmarkBase.h"
#include "BigBuffer.h"

class StressMemLatencyBenchmark : public LongBenchmarkBase {
public:
    const char* GetName()        const override { return "Stress: Mem Latency Soak"; }
    const char* GetDescription() const override {
        return "Large-stride write+verify across whole RAM; stresses DRAM row-activation margins";
    }
    const char* GetCategory()    const override { return "Stress"; }

    ThreadingMode GetThreadingMode()       const override { return ThreadingMode::SingleOnly; }
    bool          IncludeInCategoryScore() const override { return false; }

    UINT64 GetBudgetUs() const override { return mBudgetUs; }
    UINT64      GetScore() const override { return mErrorCount; }
    const char* GetUnit()  const override { return "Errors"; }
    UINT64      GetErrors() const { return mErrorCount; }

    void Setup()    override { BigBuffer::AddRef(); }
    void Teardown() override { BigBuffer::Release(); }
    void PreRun()   override { mErrorCount = 0; }
    void Run()      override { RunCore(0, 1); }
    void RunCore(UINT32 workerIndex, UINT32 totalWorkers) override;

private:
    static constexpr UINT64 mBudgetUs = 1800ULL * US_PER_SECOND; // 30 min
    static constexpr UINT64 MAGIC     = 0xFEEDFACEDEADC0DEULL;
    // 64 KB stride: skips many DRAM pages per access, maximising row activations
    static constexpr UINT64 STRIDE    = 65536;

    volatile UINT64 mErrorCount = 0;
};
