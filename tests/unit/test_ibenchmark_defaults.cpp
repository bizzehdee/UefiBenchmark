// Tests for IBenchmark default virtual return values (IBenchmark.h).
// A concrete subclass that only overrides the pure virtuals should inherit
// all documented defaults.

#include "doctest.h"
#include "Freestanding.h"
#include "IBenchmark.h"

// Minimal concrete subclass — overrides only the three pure virtuals.
struct MinimalBench : public IBenchmark {
    const char* GetName()        const override { return "minimal"; }
    const char* GetDescription() const override { return "minimal"; }
    const char* GetCategory()    const override { return "Test"; }
    void Run() override {}
};

// A benchmark that overrides the optional virtuals to their non-default values.
struct FullBench : public IBenchmark {
    const char* GetName()        const override { return "full"; }
    const char* GetDescription() const override { return "full"; }
    const char* GetCategory()    const override { return "Test"; }
    void Run() override {}

    UINT64      GetScore()              const override { return 42; }
    const char* GetUnit()               const override { return "MB/s"; }
    UINT64      GetBudgetUs()           const override { return 180000000ULL; }
    UINT64      GetRawMetric()          const override { return 99; }
    const char* GetRawUnit()            const override { return "MOPS"; }
    bool        IncludeInCategoryScore()const override { return false; }
    UINT32      GetCategoryWeight()     const override { return 50; }
    ThreadingMode GetThreadingMode()    const override { return ThreadingMode::SingleOnly; }
};

// ── Default return values ─────────────────────────────────────────────────────

TEST_CASE("IBenchmark: GetScore() default is 0") {
    MinimalBench b;
    CHECK(b.GetScore() == 0);
}

TEST_CASE("IBenchmark: GetUnit() default is empty string") {
    MinimalBench b;
    CHECK(StrLen(b.GetUnit()) == 0);
}

TEST_CASE("IBenchmark: GetBudgetUs() default is 0 (non-time-boxed)") {
    MinimalBench b;
    CHECK(b.GetBudgetUs() == 0);
}

TEST_CASE("IBenchmark: GetRawMetric() default is 0") {
    MinimalBench b;
    CHECK(b.GetRawMetric() == 0);
}

TEST_CASE("IBenchmark: GetRawUnit() default is empty string") {
    MinimalBench b;
    CHECK(StrLen(b.GetRawUnit()) == 0);
}

TEST_CASE("IBenchmark: GetStatus() default is nullptr") {
    MinimalBench b;
    CHECK(b.GetStatus() == nullptr);
}

TEST_CASE("IBenchmark: GetStatusNote() default is nullptr") {
    MinimalBench b;
    CHECK(b.GetStatusNote() == nullptr);
}

TEST_CASE("IBenchmark: GetIsaPath() default is nullptr") {
    MinimalBench b;
    CHECK(b.GetIsaPath() == nullptr);
}

TEST_CASE("IBenchmark: IncludeInCategoryScore() default is true") {
    MinimalBench b;
    CHECK(b.IncludeInCategoryScore() == true);
}

TEST_CASE("IBenchmark: GetCategoryWeight() default is 100") {
    MinimalBench b;
    CHECK(b.GetCategoryWeight() == 100);
}

TEST_CASE("IBenchmark: GetThreadingMode() default is Either") {
    MinimalBench b;
    CHECK(b.GetThreadingMode() == ThreadingMode::Either);
}

TEST_CASE("IBenchmark: GetSweep() default returns 0 and writes nothing") {
    MinimalBench b;
    UINT32 sizes[4] = {0xDEAD, 0xDEAD, 0xDEAD, 0xDEAD};
    UINT64 vals[4]  = {0xDEAD, 0xDEAD, 0xDEAD, 0xDEAD};
    UINT32 n = b.GetSweep(sizes, vals, 4);
    CHECK(n == 0);
    // Sentinel values untouched (default impl must not write past 0 entries)
    CHECK(sizes[0] == 0xDEAD);
    CHECK(vals[0]  == 0xDEAD);
}

// ── Non-default overrides round-trip ─────────────────────────────────────────

TEST_CASE("IBenchmark: overridden GetScore returns set value") {
    FullBench b;
    CHECK(b.GetScore() == 42);
}

TEST_CASE("IBenchmark: overridden GetUnit returns set value") {
    FullBench b;
    CHECK(StrCmp(b.GetUnit(), "MB/s") == 0);
}

TEST_CASE("IBenchmark: overridden IncludeInCategoryScore returns false") {
    FullBench b;
    CHECK(b.IncludeInCategoryScore() == false);
}

TEST_CASE("IBenchmark: overridden GetCategoryWeight returns 50") {
    FullBench b;
    CHECK(b.GetCategoryWeight() == 50);
}

// ── RunMode and ThreadingMode enum values are distinct ────────────────────────

TEST_CASE("IBenchmark: RunMode values are distinct") {
    CHECK(RunMode::SingleCore != RunMode::MultiCore);
    CHECK(RunMode::MultiCore  != RunMode::CoreCycle);
    CHECK(RunMode::SingleCore != RunMode::CoreCycle);
}

TEST_CASE("IBenchmark: ThreadingMode values are distinct") {
    CHECK(ThreadingMode::SingleOnly != ThreadingMode::MultiOnly);
    CHECK(ThreadingMode::MultiOnly  != ThreadingMode::Either);
    CHECK(ThreadingMode::SingleOnly != ThreadingMode::Either);
}
