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

    // Run benchmark in core-cycle mode: dispatches to each AP sequentially,
    // `runs` times per AP. Aggregate score is the median across all cycled cores.
    // If allCores is true, cycles every available AP; otherwise only selected APs.
    static BenchmarkResult RunCoreCycle(IBenchmark* benchmark, UINTN runs,
                                        bool allCores = true);

    // Run all registered benchmarks, each `runs` times, single-core.
    static Vector<BenchmarkResult> RunAll(UINTN runs);

    // Run a subset of benchmarks identified by indices into the registry.
    // modes[i] controls run mode for each selected benchmark.
    // coreCycleAllCores applies to any CoreCycle-mode benchmarks.
    static Vector<BenchmarkResult> RunSelected(
        const UINTN* indices, const RunMode* modes, UINTN count, UINTN runs,
        bool coreCycleAllCores = true);
};
