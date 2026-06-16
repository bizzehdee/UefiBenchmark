// Tests for BenchmarkRegistry registration and category bookkeeping.
// BenchmarkRegistry::Clear() is called at the start of each test case to
// isolate the static global state.

#include "doctest.h"
#include "Freestanding.h"
#include "BenchmarkRegistry.h"

// ── Minimal fake IBenchmark ───────────────────────────────────────────────────

struct FakeBenchmark : public IBenchmark {
    const char* name;
    const char* cat;
    FakeBenchmark(const char* n, const char* c) : name(n), cat(c) {}
    const char* GetName()        const override { return name; }
    const char* GetDescription() const override { return ""; }
    const char* GetCategory()    const override { return cat; }
    void Run() override {}
};

// ── Registration ──────────────────────────────────────────────────────────────

TEST_CASE("Registry: Count() is 0 after Clear()") {
    BenchmarkRegistry::Clear();
    CHECK(BenchmarkRegistry::Count() == 0);
}

TEST_CASE("Registry: Register then Count reflects additions") {
    BenchmarkRegistry::Clear();
    FakeBenchmark a("A", "CPU"), b("B", "Memory");
    BenchmarkRegistry::Register(&a);
    CHECK(BenchmarkRegistry::Count() == 1);
    BenchmarkRegistry::Register(&b);
    CHECK(BenchmarkRegistry::Count() == 2);
}

TEST_CASE("Registry: GetAll returns benchmarks in registration order") {
    BenchmarkRegistry::Clear();
    FakeBenchmark a("First", "CPU"), b("Second", "Memory"), c("Third", "AI");
    BenchmarkRegistry::Register(&a);
    BenchmarkRegistry::Register(&b);
    BenchmarkRegistry::Register(&c);
    IBenchmark** all = BenchmarkRegistry::GetAll();
    CHECK(all[0] == &a);
    CHECK(all[1] == &b);
    CHECK(all[2] == &c);
}

TEST_CASE("Registry: MAX_BENCHMARKS cap — extras are silently dropped") {
    BenchmarkRegistry::Clear();
    // Register 32 benchmarks (the cap)
    static FakeBenchmark pool[34] = {
        {"b0","CPU"},{"b1","CPU"},{"b2","CPU"},{"b3","CPU"},
        {"b4","CPU"},{"b5","CPU"},{"b6","CPU"},{"b7","CPU"},
        {"b8","CPU"},{"b9","CPU"},{"b10","CPU"},{"b11","CPU"},
        {"b12","CPU"},{"b13","CPU"},{"b14","CPU"},{"b15","CPU"},
        {"b16","CPU"},{"b17","CPU"},{"b18","CPU"},{"b19","CPU"},
        {"b20","CPU"},{"b21","CPU"},{"b22","CPU"},{"b23","CPU"},
        {"b24","CPU"},{"b25","CPU"},{"b26","CPU"},{"b27","CPU"},
        {"b28","CPU"},{"b29","CPU"},{"b30","CPU"},{"b31","CPU"},
        {"b32","OVER"},{"b33","OVER"}
    };
    for (int i = 0; i < 34; ++i) BenchmarkRegistry::Register(&pool[i]);
    // Count must not exceed 32
    UINTN count = BenchmarkRegistry::Count();
    CHECK(count == 32);
    // The first 32 entries survived
    CHECK(BenchmarkRegistry::GetAll()[0] == &pool[0]);
    CHECK(BenchmarkRegistry::GetAll()[31] == &pool[31]);
}

// ── Category enumeration ──────────────────────────────────────────────────────

TEST_CASE("Registry: GetCategoryCount reflects unique categories") {
    BenchmarkRegistry::Clear();
    FakeBenchmark a("a","CPU"), b("b","Memory"), c("c","CPU"), d("d","AI");
    BenchmarkRegistry::Register(&a);
    BenchmarkRegistry::Register(&b);
    BenchmarkRegistry::Register(&c);
    BenchmarkRegistry::Register(&d);
    CHECK(BenchmarkRegistry::GetCategoryCount() == 3);  // CPU, Memory, AI
}

TEST_CASE("Registry: GetCategoryName returns names in order of first appearance") {
    BenchmarkRegistry::Clear();
    FakeBenchmark a("a","CPU"), b("b","Memory"), c("c","CPU"), d("d","Stress");
    BenchmarkRegistry::Register(&a);
    BenchmarkRegistry::Register(&b);
    BenchmarkRegistry::Register(&c);
    BenchmarkRegistry::Register(&d);
    CHECK(StrCmp(BenchmarkRegistry::GetCategoryName(0), "CPU")    == 0);
    CHECK(StrCmp(BenchmarkRegistry::GetCategoryName(1), "Memory") == 0);
    CHECK(StrCmp(BenchmarkRegistry::GetCategoryName(2), "Stress") == 0);
}

TEST_CASE("Registry: GetCategoryName out-of-range returns empty string") {
    BenchmarkRegistry::Clear();
    FakeBenchmark a("a","CPU");
    BenchmarkRegistry::Register(&a);
    const char* s = BenchmarkRegistry::GetCategoryName(99);
    // Must be non-null and either empty or the documented sentinel
    CHECK(s != nullptr);
    CHECK(StrLen(s) == 0);
}

TEST_CASE("Registry: GetCategoryCount is 0 when registry is empty") {
    BenchmarkRegistry::Clear();
    CHECK(BenchmarkRegistry::GetCategoryCount() == 0);
}

// ── GetBenchmarksInCategory ───────────────────────────────────────────────────

TEST_CASE("Registry: GetBenchmarksInCategory returns only matching benchmarks") {
    BenchmarkRegistry::Clear();
    FakeBenchmark a("a","CPU"), b("b","Memory"), c("c","CPU");
    BenchmarkRegistry::Register(&a);
    BenchmarkRegistry::Register(&b);
    BenchmarkRegistry::Register(&c);

    IBenchmark* out[8] = {};
    UINT32 found = BenchmarkRegistry::GetBenchmarksInCategory("CPU", out, 8);
    CHECK(found == 2);
    CHECK(out[0] == &a);
    CHECK(out[1] == &c);
}

TEST_CASE("Registry: GetBenchmarksInCategory respects maxCount cap") {
    BenchmarkRegistry::Clear();
    FakeBenchmark a("a","CPU"), b("b","CPU"), c("c","CPU");
    BenchmarkRegistry::Register(&a);
    BenchmarkRegistry::Register(&b);
    BenchmarkRegistry::Register(&c);

    IBenchmark* out[2] = {};
    UINT32 found = BenchmarkRegistry::GetBenchmarksInCategory("CPU", out, 2);
    CHECK(found == 2);  // capped, not 3
    CHECK(out[0] == &a);
    CHECK(out[1] == &b);
}

TEST_CASE("Registry: GetBenchmarksInCategory returns 0 for unknown category") {
    BenchmarkRegistry::Clear();
    FakeBenchmark a("a","CPU");
    BenchmarkRegistry::Register(&a);

    IBenchmark* out[4] = {};
    UINT32 found = BenchmarkRegistry::GetBenchmarksInCategory("Storage", out, 4);
    CHECK(found == 0);
}

TEST_CASE("Registry: full CPU/Memory/AI/Stress split") {
    BenchmarkRegistry::Clear();
    FakeBenchmark cpu1("c1","CPU"), cpu2("c2","CPU"),
                  mem1("m1","Memory"),
                  ai1("a1","AI"), ai2("a2","AI"), ai3("a3","AI"),
                  str1("s1","Stress");
    BenchmarkRegistry::Register(&cpu1);
    BenchmarkRegistry::Register(&cpu2);
    BenchmarkRegistry::Register(&mem1);
    BenchmarkRegistry::Register(&ai1);
    BenchmarkRegistry::Register(&ai2);
    BenchmarkRegistry::Register(&ai3);
    BenchmarkRegistry::Register(&str1);

    CHECK(BenchmarkRegistry::GetCategoryCount() == 4);

    IBenchmark* out[8] = {};
    CHECK(BenchmarkRegistry::GetBenchmarksInCategory("CPU",    out, 8) == 2);
    CHECK(BenchmarkRegistry::GetBenchmarksInCategory("Memory", out, 8) == 1);
    CHECK(BenchmarkRegistry::GetBenchmarksInCategory("AI",     out, 8) == 3);
    CHECK(BenchmarkRegistry::GetBenchmarksInCategory("Stress", out, 8) == 1);
}
