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
    Vector<UINT64>  RunTimesUs;    // per-run time in microseconds
    UINT64          TotalTimeUs;   // sum of all runs
    bool            MultiCore;     // was this run multi-core?
    UINT32          CoreCount;     // number of cores that participated

    BenchmarkResult()
        : Name(""), Category(""), Unit(""), Score(0),
          Iterations(0), TotalTimeUs(0), MultiCore(false), CoreCount(1) {}
};
