#pragma once
// Memory clock soak: continuously write an address-encoded pattern to the whole
// BigBuffer and verify readback. Mismatches indicate marginal DRAM clocks,
// sub-timings, or voltage. Runs for 30 minutes. 0 errors = stable.

#include "LongBenchmarkBase.h"
#include "BigBuffer.h"

class StressMemClockBenchmark : public LongBenchmarkBase {
public:
    const char* GetName()        const override { return "Stress: Mem Clock Soak"; }
    const char* GetDescription() const override {
        return "Sustained stream write+verify over whole RAM; any error = memory instability";
    }
    const char* GetCategory()    const override { return "Stress"; }

    ThreadingMode GetThreadingMode()       const override { return ThreadingMode::MultiOnly; }
    bool          IncludeInCategoryScore() const override { return false; }

    UINT64 GetBudgetUs() const override { return mBudgetUs; }
    UINT64      GetScore() const override { return mErrorCount; }
    const char* GetUnit()  const override { return "Errors"; }
    UINT64      GetErrors() const { return mErrorCount; }

    void Setup()    override { BigBuffer::AddRef(); }
    void Teardown() override { BigBuffer::Release(); }
    void PreRun()   override { mTotalBytes = 0; mErrorCount = 0; }
    void Run()      override { RunCore(0, 1); }
    void RunCore(UINT32 workerIndex, UINT32 totalWorkers) override;

private:
    static constexpr UINT64 mBudgetUs = 1800ULL * 1000000; // 30 min
    static constexpr UINT64 MAGIC     = 0xDEADBEEFCAFEBABEULL;

    volatile UINT64 mTotalBytes = 0;
    volatile UINT64 mErrorCount = 0;
};
