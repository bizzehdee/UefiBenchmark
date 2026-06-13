#pragma once
// Result of running a single benchmark (possibly multiple times).

#include "UefiTypes.h"
#include "Freestanding.h"

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

    BenchmarkResult()
        : Name(""), Category(""), Unit(""), Score(0),
          Iterations(0), ErrorCount(0), TotalTimeUs(0),
          MultiCore(false), CoreCount(1) {}
};
