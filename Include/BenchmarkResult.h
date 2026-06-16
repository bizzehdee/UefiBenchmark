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
    UINT64          BudgetUs;      // configured time-box duration for this run
    UINT64          RawMetric;     // raw pre-normalization throughput (calibration)
    const char*     RawUnit;       // unit for RawMetric (e.g. "MOPS", "MB/s")
    const char*     Note;          // recoverable-failure reason (OOM, unsupported); null = ran normally
    const char*     IsaPath;       // ISA path taken (e.g. "AVX2", "SSE2"); null = no ISA choice
    UINT32          McCorrected;   // corrected machine-check events seen during the run
    UINT32          McUncorrected; // uncorrected (but survived) machine-check events
    Vector<UINT64>  RunTimesUs;
    UINT64          TotalTimeUs;
    bool            MultiCore;
    bool            IncludeInScore;   // from IBenchmark::IncludeInCategoryScore()
    UINT32          CategoryWeight;   // from IBenchmark::GetCategoryWeight()
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
          Iterations(0), ErrorCount(0), BudgetUs(0), RawMetric(0), RawUnit(""),
          Note(nullptr), IsaPath(nullptr), McCorrected(0), McUncorrected(0),
          TotalTimeUs(0),
          MultiCore(false), IncludeInScore(true), CategoryWeight(100), CoreCount(1),
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
