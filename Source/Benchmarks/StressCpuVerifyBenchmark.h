#pragma once
// CPU compute verification: repeatedly runs deterministic integer chains seeded
// from several distinct bit patterns and compares each against a precomputed
// golden value. Any mismatch exposes marginal Vcore, marginal clock, or
// cache/register corruption. RunCore cycles through the patterns round-robin so
// a wide range of ALU/register bit states is exercised. Runs 30 min. 0 errors = stable.

#include "LongBenchmarkBase.h"
#include "BenchmarkConstants.h"

class StressCpuVerifyBenchmark : public LongBenchmarkBase {
public:
    const char* GetName()        const override { return "Stress: CPU Compute Verify"; }
    const char* GetDescription() const override {
        return "Deterministic LCG chains (cycled bit patterns) vs golden; mismatches = marginal Vcore or clock";
    }
    const char* GetCategory()    const override { return "Stress"; }

    ThreadingMode GetThreadingMode()       const override { return ThreadingMode::MultiOnly; }
    bool          IncludeInCategoryScore() const override { return false; }

    UINT64 GetBudgetUs() const override { return mBudgetUs; }
    UINT64      GetScore() const override { return mErrorCount; }
    const char* GetUnit()  const override { return "Errors"; }
    UINT64      GetErrors() const { return mErrorCount; }

    // Live label for the pattern most recently entered (display hint; with
    // multiple cores this shows whichever core updated it last).
    const char* GetStatus() const override { return PATTERN_NAMES[mCurrentPattern % PATTERN_COUNT]; }

    void Setup()   override;
    void PreRun()  override { mErrorCount = 0; }
    void Run()     override { RunCore(0, 1); }
    void RunCore(UINT32 workerIndex, UINT32 totalWorkers) override;

private:
    static constexpr UINT64 mBudgetUs    = 1800ULL * US_PER_SECOND; // 30 min
    static constexpr UINT64 CHAIN_LENGTH = 1000000ULL;             // 1M LCG steps per check

    // Distinct seed patterns (see BenchmarkConstants.h) exercise different
    // bit-transition failure modes. RunCore cycles through them round-robin.
    static constexpr UINT64 PATTERNS[] = {
        PATTERN_ZEROS,
        PATTERN_ONES,
        PATTERN_ALT_AA,
        PATTERN_ALT_55,
        PATTERN_WALKING,
        TEST_PATTERN,
    };
    static constexpr UINT32 PATTERN_COUNT = sizeof(PATTERNS) / sizeof(PATTERNS[0]);

    // Human-readable labels, parallel to PATTERNS, shown live via GetStatus().
    static constexpr const char* PATTERN_NAMES[] = {
        "all-zeros (0x0000...)",
        "all-ones (0xFFFF...)",
        "alternating (0xAAAA...)",
        "alternating (0x5555...)",
        "walking nibbles (0x0123...)",
        "mixed (0xDEADBEEF...)",
    };
    static_assert(sizeof(PATTERN_NAMES) / sizeof(PATTERN_NAMES[0]) == PATTERN_COUNT,
                  "PATTERN_NAMES must stay in sync with PATTERNS");

    UINT64          mGolden[PATTERN_COUNT] = {};
    volatile UINT32 mCurrentPattern = 0;   // index of pattern currently under test
    volatile UINT64 mErrorCount = 0;
};
