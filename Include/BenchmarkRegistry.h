#pragma once
// Static benchmark registry. Benchmarks register themselves here;
// the runner and TUI enumerate them without coupling.

#include "UefiTypes.h"
#include "IBenchmark.h"

class BenchmarkRegistry {
public:
    static void         Register(IBenchmark* benchmark);
    static IBenchmark** GetAll();
    static UINTN        Count();
    static void         Clear();

private:
    static constexpr UINTN MAX_BENCHMARKS = 32;
    static IBenchmark* sBenchmarks[MAX_BENCHMARKS];
    static UINTN       sCount;
};
