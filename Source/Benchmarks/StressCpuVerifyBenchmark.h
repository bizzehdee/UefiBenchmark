#pragma once
// CPU compute verification: repeatedly runs a deterministic integer chain and
// compares against a precomputed golden value. Any mismatch exposes marginal
// Vcore, marginal clock, or cache/register corruption. Runs 30 min. 0 errors = stable.

#include "LongBenchmarkBase.h"
#include "BenchmarkConstants.h"

class StressCpuVerifyBenchmark : public LongBenchmarkBase {
public:
    const char* GetName()        const override { return "Stress: CPU Compute Verify"; }
    const char* GetDescription() const override {
        return "Deterministic LCG chain vs golden value; mismatches = marginal Vcore or clock";
    }
    const char* GetCategory()    const override { return "Stress"; }

    ThreadingMode GetThreadingMode()       const override { return ThreadingMode::MultiOnly; }
    bool          IncludeInCategoryScore() const override { return false; }

    UINT64 GetBudgetUs() const override { return mBudgetUs; }
    UINT64      GetScore() const override { return mErrorCount; }
    const char* GetUnit()  const override { return "Errors"; }
    UINT64      GetErrors() const { return mErrorCount; }

    void Setup()   override;
    void PreRun()  override { mErrorCount = 0; }
    void Run()     override { RunCore(0, 1); }
    void RunCore(UINT32 workerIndex, UINT32 totalWorkers) override;

private:
    static constexpr UINT64 mBudgetUs    = 1800ULL * US_PER_SECOND; // 30 min
    static constexpr UINT64 CHAIN_LENGTH = 1000000ULL;             // 1M LCG steps per check
    static constexpr UINT64 SEED         = TEST_PATTERN;

    UINT64          mGolden    = 0;
    volatile UINT64 mErrorCount = 0;
};
