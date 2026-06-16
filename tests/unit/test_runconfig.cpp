// Tests for RunConfig:: budget functions (RunConfig.h / RunConfig.cpp).
// RunConfig::Reset() is called between test cases to isolate static state.

#include "doctest.h"
#include "RunConfig.h"
#include "BenchmarkConstants.h"

// ── Helpers ───────────────────────────────────────────────────────────────────

static void reset() { RunConfig::Reset(); }

// ── Default values ────────────────────────────────────────────────────────────

TEST_CASE("RunConfig: test default is 3 minutes") {
    reset();
    CHECK(RunConfig::GetTestMinutes() == 3);
}

TEST_CASE("RunConfig: stress default is 30 minutes") {
    reset();
    CHECK(RunConfig::GetStressMinutes() == 30);
}

// ── Budget arithmetic ─────────────────────────────────────────────────────────

TEST_CASE("RunConfig: GetTestBudgetUs == minutes * 60 * US_PER_SECOND") {
    reset();
    UINT64 expected = (UINT64)RunConfig::kTestDefaultMinutes * 60ULL * US_PER_SECOND;
    CHECK(RunConfig::GetTestBudgetUs() == expected);
}

TEST_CASE("RunConfig: GetStressBudgetUs == minutes * 60 * US_PER_SECOND") {
    reset();
    UINT64 expected = (UINT64)RunConfig::kStressDefaultMinutes * 60ULL * US_PER_SECOND;
    CHECK(RunConfig::GetStressBudgetUs() == expected);
}

TEST_CASE("RunConfig: budget updates when minutes change") {
    reset();
    RunConfig::SetTestMinutes(10);
    CHECK(RunConfig::GetTestBudgetUs() == 10ULL * 60ULL * US_PER_SECOND);
}

// ── Round-trips ───────────────────────────────────────────────────────────────

TEST_CASE("RunConfig: SetTestMinutes / GetTestMinutes round-trip") {
    reset();
    RunConfig::SetTestMinutes(7);
    CHECK(RunConfig::GetTestMinutes() == 7);
}

TEST_CASE("RunConfig: SetStressMinutes / GetStressMinutes round-trip") {
    reset();
    RunConfig::SetStressMinutes(45);
    CHECK(RunConfig::GetStressMinutes() == 45);
}

// ── Clamping ─────────────────────────────────────────────────────────────────

TEST_CASE("RunConfig: SetTestMinutes(0) clamps to kMinMinutes") {
    reset();
    RunConfig::SetTestMinutes(0);
    CHECK(RunConfig::GetTestMinutes() >= RunConfig::kMinMinutes);
    CHECK(RunConfig::GetTestMinutes() <= RunConfig::kMaxMinutes);
}

TEST_CASE("RunConfig: SetTestMinutes above kMaxMinutes clamps to kMaxMinutes") {
    reset();
    RunConfig::SetTestMinutes(RunConfig::kMaxMinutes + 1);
    CHECK(RunConfig::GetTestMinutes() == RunConfig::kMaxMinutes);
}

TEST_CASE("RunConfig: SetStressMinutes(0) clamps to kMinMinutes") {
    reset();
    RunConfig::SetStressMinutes(0);
    CHECK(RunConfig::GetStressMinutes() >= RunConfig::kMinMinutes);
}

TEST_CASE("RunConfig: SetStressMinutes above kMaxMinutes clamps to kMaxMinutes") {
    reset();
    RunConfig::SetStressMinutes(RunConfig::kMaxMinutes + 100);
    CHECK(RunConfig::GetStressMinutes() == RunConfig::kMaxMinutes);
}

TEST_CASE("RunConfig: kMinMinutes is 1, kMaxMinutes is 24*60") {
    CHECK(RunConfig::kMinMinutes  == 1);
    CHECK(RunConfig::kMaxMinutes  == 24 * 60);
}

// ── Reset ─────────────────────────────────────────────────────────────────────

TEST_CASE("RunConfig: Reset restores default test budget") {
    RunConfig::SetTestMinutes(99);
    RunConfig::Reset();
    CHECK(RunConfig::GetTestMinutes() == RunConfig::kTestDefaultMinutes);
}

TEST_CASE("RunConfig: Reset restores default stress budget") {
    RunConfig::SetStressMinutes(99);
    RunConfig::Reset();
    CHECK(RunConfig::GetStressMinutes() == RunConfig::kStressDefaultMinutes);
}
