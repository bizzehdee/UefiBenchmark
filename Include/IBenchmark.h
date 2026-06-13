#pragma once
// Abstract interface for all benchmarks.
// Each benchmark is self-contained and reports its results via Execute().

#include "UefiTypes.h"

enum class ThreadingMode : int {
    SingleOnly = 0,   // benchmark must run single-core
    MultiOnly  = 1,   // benchmark must run multi-core
    Either     = 2    // user chooses (default)
};

class IBenchmark {
public:
    virtual const char* GetName() const = 0;
    virtual const char* GetDescription() const = 0;
    virtual const char* GetCategory() const = 0;

    // Declares threading capability. Override to lock to a specific mode.
    virtual ThreadingMode GetThreadingMode() const { return ThreadingMode::Either; }

    virtual void Setup()    {}
    virtual void Run()      = 0;
    virtual void Teardown() {}

    // Per-worker entry point for multi-core mode.
    // workerIndex: 0-based compact index (0 .. totalWorkers-1)
    // totalWorkers: number of APs executing the benchmark
    // Default: each core independently runs the full Run().
    // Override to partition work (e.g. memory benchmarks split the buffer).
    virtual void RunCore(UINT32 workerIndex, UINT32 totalWorkers) {
        (void)workerIndex; (void)totalWorkers;
        Run();
    }

    virtual ~IBenchmark() {}
};
