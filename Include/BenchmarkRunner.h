#pragma once
// Sequential benchmark runner with multi-run and multi-core support.

#include "UefiTypes.h"
#include "Freestanding.h"
#include "BenchmarkResult.h"
#include "IBenchmark.h"

class BenchmarkRunner {
public:
    // Run a single benchmark `runs` times. Returns aggregated result.
    // If multiCore is true, dispatches to all APs via MP Services.
    static BenchmarkResult RunSingle(IBenchmark* benchmark, UINTN runs,
                                     bool multiCore = false);

    // Run all registered benchmarks, each `runs` times, single-core.
    static Vector<BenchmarkResult> RunAll(UINTN runs);

    // Run a subset of benchmarks identified by indices into the registry.
    // multiCore[i] controls threading mode for each selected benchmark.
    static Vector<BenchmarkResult> RunSelected(
        const UINTN* indices, const bool* multiCore, UINTN count, UINTN runs);
};
