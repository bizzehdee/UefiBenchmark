#pragma once
// Result of running a single benchmark (possibly multiple times).

#include "UefiTypes.h"
#include "Freestanding.h"
#include "IBenchmark.h"

constexpr UINT32 MAX_CYCLE_CORES = 64;

struct BenchmarkResult {
    const char*     Name;
    const char*     Category;
    const char*     Unit;
    UINT64          Score;
    UINT64          Iterations;
    UINT64          ErrorCount;    // non-zero for integrity test failures
    Vector<UINT64>  RunTimesUs;
    UINT64          TotalTimeUs;
    bool            MultiCore;
    UINT32          CoreCount;
    RunMode         RunModeUsed;

    // Populated only when RunModeUsed == RunMode::CoreCycle
    UINT32  PerCoreSampleCount;
    UINT64  PerCoreScore[MAX_CYCLE_CORES];   // avg score per core (0 if benchmark has no score)
    UINT64  PerCoreMin[MAX_CYCLE_CORES];
    UINT64  PerCoreMax[MAX_CYCLE_CORES];
    UINT64  PerCoreTimeUs[MAX_CYCLE_CORES];  // avg elapsed time per run on this AP
    UINT32  PerCoreApIndex[MAX_CYCLE_CORES]; // AP processor index for each slot

    BenchmarkResult()
        : Name(""), Category(""), Unit(""), Score(0),
          Iterations(0), ErrorCount(0), TotalTimeUs(0),
          MultiCore(false), CoreCount(1),
          RunModeUsed(RunMode::SingleCore),
          PerCoreSampleCount(0)
    {
        for (UINT32 i = 0; i < MAX_CYCLE_CORES; ++i) {
            PerCoreScore[i]   = 0;
            PerCoreMin[i]     = 0;
            PerCoreMax[i]     = 0;
            PerCoreTimeUs[i]  = 0;
            PerCoreApIndex[i] = 0;
        }
    }
};
