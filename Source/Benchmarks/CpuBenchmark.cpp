// CPU integer arithmetic benchmark.
// Uses a dependency chain to prevent compiler reordering/elimination.

#include "CpuBenchmark.h"
#include "Freestanding.h"

// volatile sink prevents dead-code elimination
static volatile int sSink = 0;

void CpuBenchmark::Run() {
    constexpr long long ITERATIONS = 5000000LL;
    int a = 7, b = 13, c = 0;

    for (long long i = 0; i < ITERATIONS; ++i) {
        // Dependency chain: each op feeds the next
        c = a + b;
        c = c * a;
        c = (c != 0) ? (c / b) : 1;
        c = c ^ b;
        a = c & 0xFF;
        if (a == 0) a = 7;
    }

    sSink = c;
}
