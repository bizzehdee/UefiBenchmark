// Benchmark registry: fixed-capacity array of benchmark pointers.

#include "BenchmarkRegistry.h"

IBenchmark* BenchmarkRegistry::sBenchmarks[MAX_BENCHMARKS] = {};
UINTN       BenchmarkRegistry::sCount = 0;

void BenchmarkRegistry::Register(IBenchmark* benchmark) {
    if (benchmark && sCount < MAX_BENCHMARKS) {
        sBenchmarks[sCount++] = benchmark;
    }
}

IBenchmark** BenchmarkRegistry::GetAll() {
    return sBenchmarks;
}

UINTN BenchmarkRegistry::Count() {
    return sCount;
}

void BenchmarkRegistry::Clear() {
    sCount = 0;
}
