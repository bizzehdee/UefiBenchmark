// Tests for Stats::GetMin / GetMax / GetAverage / GetSum (Statistics.h).

#include "doctest.h"
#include "Statistics.h"

// ── Empty vector ──────────────────────────────────────────────────────────────

TEST_CASE("Stats: empty vector returns 0 for all four functions") {
    Vector<UINT64> v;
    CHECK(Stats::GetMin(v)     == 0);
    CHECK(Stats::GetMax(v)     == 0);
    CHECK(Stats::GetAverage(v) == 0);
    CHECK(Stats::GetSum(v)     == 0);
}

// ── Single element ────────────────────────────────────────────────────────────

TEST_CASE("Stats: single element — all four agree") {
    Vector<UINT64> v;
    v.PushBack(7ULL);
    CHECK(Stats::GetMin(v)     == 7);
    CHECK(Stats::GetMax(v)     == 7);
    CHECK(Stats::GetAverage(v) == 7);
    CHECK(Stats::GetSum(v)     == 7);
}

// ── Known multiset ────────────────────────────────────────────────────────────

TEST_CASE("Stats: GetMin picks the smallest regardless of position") {
    Vector<UINT64> v;
    v.PushBack(50); v.PushBack(1); v.PushBack(200);  // min in middle
    CHECK(Stats::GetMin(v) == 1);

    Vector<UINT64> v2;
    v2.PushBack(1); v2.PushBack(50); v2.PushBack(200);  // min first
    CHECK(Stats::GetMin(v2) == 1);

    Vector<UINT64> v3;
    v3.PushBack(200); v3.PushBack(50); v3.PushBack(1);  // min last
    CHECK(Stats::GetMin(v3) == 1);
}

TEST_CASE("Stats: GetMax picks the largest regardless of position") {
    Vector<UINT64> v;
    v.PushBack(100); v.PushBack(999); v.PushBack(50);  // max in middle
    CHECK(Stats::GetMax(v) == 999);
}

TEST_CASE("Stats: GetSum is the exact total") {
    Vector<UINT64> v;
    v.PushBack(10); v.PushBack(20); v.PushBack(30);
    CHECK(Stats::GetSum(v) == 60);
}

TEST_CASE("Stats: GetAverage is integer sum/size — truncates toward zero") {
    Vector<UINT64> v;
    v.PushBack(1); v.PushBack(2);   // sum=3, size=2 → avg=1 (truncated)
    CHECK(Stats::GetAverage(v) == 1);
}

TEST_CASE("Stats: GetAverage divides exactly when sum is divisible") {
    Vector<UINT64> v;
    v.PushBack(4); v.PushBack(8); v.PushBack(12);  // sum=24, size=3 → 8
    CHECK(Stats::GetAverage(v) == 8);
}

// ── UINT64 overflow (pinning current behaviour) ───────────────────────────────

TEST_CASE("Stats: GetSum wraps on UINT64 overflow (current contract — pin it)") {
    // Two values that together exceed 2^64-1.
    Vector<UINT64> v;
    v.PushBack(0xFFFFFFFFFFFFFFFFULL);
    v.PushBack(1ULL);
    // Wraps to 0 on UINT64 overflow — this pins the current behaviour so any
    // future fix is deliberate and visible in the test diff.
    CHECK(Stats::GetSum(v) == 0ULL);
}
