// Tests for LongBenchmarkBase: ScoreDurationUs() and TryReportProgress()
// rate-limiting. Uses FakeTimer from UefiShim for deterministic time control.

#include "doctest.h"
#include "Freestanding.h"
#include "LongBenchmarkBase.h"
#include "host/UefiShim.h"

// ── Concrete test subclass ────────────────────────────────────────────────────

struct TestableLBB : public LongBenchmarkBase {
    UINT64      budget   = 3000000ULL;  // 3 s default
    UINT64      scoreVal = 99;
    const char* unitVal  = "MOPS";

    const char* GetName()        const override { return "test"; }
    const char* GetDescription() const override { return "test"; }
    const char* GetCategory()    const override { return "Test"; }
    UINT64      GetBudgetUs()    const override { return budget; }
    UINT64      GetScore()       const override { return scoreVal; }
    const char* GetUnit()        const override { return unitVal; }
    void        Run()            override {}

    // Expose protected members for white-box contract tests.
    UINT64 CallScoreDurationUs() { return ScoreDurationUs(); }
    void   CallTryReport(UINT64 elapsed) { TryReportProgress(elapsed); }
    void   CallSetNote(const char* n)   { SetNote(n); }
    void   CallClearNote()              { ClearNote(); }
    void   CallSetIsa(const char* isa)  { SetIsa(isa); }
};

// ── ScoreDurationUs ───────────────────────────────────────────────────────────

TEST_CASE("LBB::ScoreDurationUs: returns GetBudgetUs() before first progress call") {
    TestableLBB b;
    b.budget = 5000000ULL;
    // No TryReportProgress called yet → mLastElapsedUs == 0 → returns budget.
    CHECK(b.CallScoreDurationUs() == 5000000ULL);
}

TEST_CASE("LBB::ScoreDurationUs: returns elapsed after TryReportProgress publishes it") {
    FakeTimer::Reset();
    // With uncalibrated timer the rate-limit check is skipped — callback always fires.
    TestableLBB b;
    bool fired = false;
    b.SetProgressCallback([](const ProgressReport&, void* ctx){
        *static_cast<bool*>(ctx) = true;
    }, &fired);

    b.CallTryReport(250000ULL);
    // Regardless of whether the callback fired, elapsed is always published.
    CHECK(b.CallScoreDurationUs() == 250000ULL);
}

TEST_CASE("LBB::ScoreDurationUs: elapsed published even when render is rate-limited") {
    FakeTimer::Reset();
    FakeTimer::SetCalibrated(true);
    FakeTimer::SetCyclesPerUs(1);
    // TSC starts well above 0 so the first read clears the 500 ms gate.
    FakeTimer::SetTSC(600000ULL);  // 600 000 µs above baseline
    FakeTimer::SetStepPerRead(0);  // TSC stays at 600000 for subsequent reads

    TestableLBB b;
    int cbCount = 0;
    b.SetProgressCallback([](const ProgressReport&, void* ctx){
        ++*static_cast<int*>(ctx);
    }, &cbCount);

    // First call fires (600000 µs > 500000 µs threshold from mLastRenderTsc=0).
    b.CallTryReport(1000ULL);
    CHECK(cbCount == 1);

    // Second call: TSC still 600000 (step=0), elapsed since last render = 0 → suppressed.
    b.CallTryReport(2000ULL);
    CHECK(cbCount == 1);  // callback NOT fired again

    // But elapsed must have been updated regardless of suppression.
    CHECK(b.CallScoreDurationUs() == 2000ULL);
}

// ── TryReportProgress: uncalibrated path ─────────────────────────────────────

TEST_CASE("LBB::TryReport: no callback set → no crash") {
    FakeTimer::Reset();
    TestableLBB b;
    // No SetProgressCallback call.
    b.CallTryReport(1000ULL);  // must not crash
    CHECK(b.CallScoreDurationUs() == 1000ULL);
}

TEST_CASE("LBB::TryReport: uncalibrated → callback fires every call") {
    FakeTimer::Reset();  // IsCalibrated() = false → rate-limit skipped
    TestableLBB b;
    int count = 0;
    b.SetProgressCallback([](const ProgressReport&, void* ctx){
        ++*static_cast<int*>(ctx);
    }, &count);
    b.CallTryReport(100ULL);
    b.CallTryReport(200ULL);
    b.CallTryReport(300ULL);
    CHECK(count == 3);
}

// ── TryReportProgress: calibrated rate-limiting ───────────────────────────────

TEST_CASE("LBB::TryReport: calibrated — first call fires when TSC > 500 ms threshold") {
    FakeTimer::Reset();
    FakeTimer::SetCalibrated(true);
    FakeTimer::SetCyclesPerUs(1);
    // mLastRenderTsc starts at 0 after SetProgressCallback.
    // ReadTSC() returns 600000 (> 500000 threshold) → fires.
    FakeTimer::SetTSC(600000ULL);
    FakeTimer::SetStepPerRead(0);

    TestableLBB b;
    int count = 0;
    b.SetProgressCallback([](const ProgressReport&, void* ctx){
        ++*static_cast<int*>(ctx);
    }, &count);
    b.CallTryReport(600ULL);
    CHECK(count == 1);
}

TEST_CASE("LBB::TryReport: calibrated — second call within 500 ms is suppressed") {
    FakeTimer::Reset();
    FakeTimer::SetCalibrated(true);
    FakeTimer::SetCyclesPerUs(1);
    FakeTimer::SetTSC(600000ULL);
    FakeTimer::SetStepPerRead(0);  // TSC frozen at 600000 after first read

    TestableLBB b;
    int count = 0;
    b.SetProgressCallback([](const ProgressReport&, void* ctx){
        ++*static_cast<int*>(ctx);
    }, &count);
    b.CallTryReport(100ULL);  // fires (600000 − 0 >= 500000)
    b.CallTryReport(200ULL);  // suppressed (600000 − 600000 = 0 < 500000)
    CHECK(count == 1);
}

TEST_CASE("LBB::TryReport: calibrated — call after 500 ms fires again") {
    FakeTimer::Reset();
    FakeTimer::SetCalibrated(true);
    FakeTimer::SetCyclesPerUs(1);

    // Call 1: TSC = 600000 → fires, mLastRenderTsc = 600000
    // Call 2: TSC = 600000 + 100 = 600100 → suppressed (100 < 500000)
    // Call 3: TSC = 600100 + 500000 = 1100100 → fires (>= 500000 from 600000)
    TestableLBB b;
    int count = 0;
    b.SetProgressCallback([](const ProgressReport&, void* ctx){
        ++*static_cast<int*>(ctx);
    }, &count);

    FakeTimer::SetTSC(600000ULL);  FakeTimer::SetStepPerRead(0);
    b.CallTryReport(100ULL);
    CHECK(count == 1);

    FakeTimer::SetTSC(600100ULL);
    b.CallTryReport(200ULL);
    CHECK(count == 1);  // suppressed

    FakeTimer::SetTSC(1100100ULL);
    b.CallTryReport(300ULL);
    CHECK(count == 2);  // fires again
}

// ── SetNote / ClearNote / GetStatusNote ───────────────────────────────────────

TEST_CASE("LBB::SetNote: GetStatusNote returns the set string") {
    TestableLBB b;
    b.CallSetNote("insufficient memory");
    CHECK(StrCmp(b.GetStatusNote(), "insufficient memory") == 0);
}

TEST_CASE("LBB::ClearNote: GetStatusNote returns nullptr after clear") {
    TestableLBB b;
    b.CallSetNote("something");
    b.CallClearNote();
    CHECK(b.GetStatusNote() == nullptr);
}

// ── SetIsa / GetIsaPath ───────────────────────────────────────────────────────

TEST_CASE("LBB::SetIsa: GetIsaPath returns the set string") {
    TestableLBB b;
    b.CallSetIsa("AVX2");
    CHECK(StrCmp(b.GetIsaPath(), "AVX2") == 0);
}

TEST_CASE("LBB::GetIsaPath: default is nullptr") {
    TestableLBB b;
    CHECK(b.GetIsaPath() == nullptr);
}
