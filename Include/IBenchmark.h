#pragma once
// Abstract interface for all benchmarks.

#include "UefiTypes.h"

enum class ThreadingMode : int {
    SingleOnly = 0,
    MultiOnly  = 1,
    Either     = 2
};

enum class RunMode : int {
    SingleCore = 0,
    MultiCore  = 1,
    CoreCycle  = 2  // run on each core sequentially; produces per-core score table
};

// Progress snapshot emitted by long benchmarks while running.
struct ProgressReport {
    UINT64      ElapsedUs;
    UINT64      BudgetUs;
    UINT64      Score;
    const char* Unit;
};

using ProgressFn = void (*)(const ProgressReport& report, void* ctx);

class IBenchmark {
public:
    virtual const char* GetName()        const = 0;
    virtual const char* GetDescription() const = 0;
    virtual const char* GetCategory()    const = 0;

    virtual ThreadingMode GetThreadingMode()  const { return ThreadingMode::Either; }

    // Optional throughput score for time-boxed benchmarks.
    // GetScore() returns the result of the most recent run.
    virtual UINT64      GetScore() const { return 0; }
    virtual const char* GetUnit()  const { return ""; }

    // Return false for pass/fail tests (e.g. integrity) so they don't skew
    // the category composite score.
    virtual bool IncludeInCategoryScore() const { return true; }

    // Relative weight (0-100) used for weighted composite in category results.
    virtual UINT32 GetCategoryWeight() const { return 100; }

    virtual void SetProgressCallback(ProgressFn /*fn*/, void* /*ctx*/) {}

    virtual void Setup()    {}
    // Called by BenchmarkRunner before each individual run (single- or multi-core).
    // Use to reset per-run accumulators.
    virtual void PreRun()   {}
    virtual void Run()      = 0;
    virtual void Teardown() {}

    virtual void RunCore(UINT32 workerIndex, UINT32 totalWorkers) {
        (void)workerIndex; (void)totalWorkers;
        Run();
    }

    virtual ~IBenchmark() {}
};
