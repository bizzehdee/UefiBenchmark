#pragma once
// Runtime-configurable durations for time-boxed benchmarks, chosen by the user
// in the run-count picker. Two independent budgets because regular performance
// tests and stress soaks have very different natural time scales:
//   - Test budget   (regular time-boxed perf tests)   default 3 minutes
//   - Stress budget (CPU/memory soak tests)           default 30 minutes
// Both are settable from 1 minute to 24 hours. Every time-boxed benchmark reads
// its budget here via GetBudgetUs().

#include "UefiTypes.h"
#include "BenchmarkConstants.h"   // US_PER_SECOND

namespace RunConfig {

constexpr UINT32 kMinMinutes         = 1;
constexpr UINT32 kMaxMinutes         = 24 * 60;   // 24 hours
constexpr UINT32 kTestDefaultMinutes = 3;
constexpr UINT32 kStressDefaultMinutes = 30;

// Regular time-boxed performance tests.
UINT32 GetTestMinutes();
void   SetTestMinutes(UINT32 minutes);
UINT64 GetTestBudgetUs();

// Stress / soak tests.
UINT32 GetStressMinutes();
void   SetStressMinutes(UINT32 minutes);
UINT64 GetStressBudgetUs();

// Reset both budgets to their default "unset" state (returns defaults on next Get call).
// Primarily for unit-test isolation.
void Reset();

}  // namespace RunConfig
