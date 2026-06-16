// Tests for TimeBox::Run and TimeBox::RunWithProgress (TimeBox.h).
// Uses the FakeTimer from UefiShim so time is entirely deterministic.

#include "doctest.h"
#include "TimeBox.h"
#include "host/UefiShim.h"

// ── Helpers ───────────────────────────────────────────────────────────────────

// Kernel that counts invocations and verifies chunkSize is passed correctly.
struct CountingKernel {
    int   calls       = 0;
    UINT64 lastChunk  = 0;
    void operator()(UINT64 n) { ++calls; lastChunk = n; }
};

struct ProgressRecord {
    int    calls       = 0;
    UINT64 lastElapsed = 0;
    UINT64 lastBudget  = 0;
    UINT64 lastTotal   = 0;
    void operator()(UINT64 elapsed, UINT64 total) {
        ++calls;
        lastElapsed = elapsed;
        lastTotal   = total;
    }
};

// ── Run — uncalibrated / edge cases ──────────────────────────────────────────

TEST_CASE("TimeBox::Run: uncalibrated — runs kernel exactly once, returns chunkSize") {
    FakeTimer::Reset();
    // IsCalibrated() returns false → single-shot path
    CountingKernel k;
    UINT64 result = TimeBox::Run(1000, 100, [&k](UINT64 n){ k(n); });
    CHECK(k.calls      == 1);
    CHECK(k.lastChunk  == 100);
    CHECK(result       == 100);
}

TEST_CASE("TimeBox::Run: zero budget — single shot") {
    FakeTimer::Reset();
    FakeTimer::SetCalibrated(true);
    FakeTimer::SetCyclesPerUs(1);
    CountingKernel k;
    UINT64 result = TimeBox::Run(0, 50, [&k](UINT64 n){ k(n); });
    CHECK(k.calls == 1);
    CHECK(result  == 50);
}

TEST_CASE("TimeBox::Run: zero chunkSize — single shot") {
    FakeTimer::Reset();
    FakeTimer::SetCalibrated(true);
    FakeTimer::SetCyclesPerUs(1);
    CountingKernel k;
    UINT64 result = TimeBox::Run(1000, 0, [&k](UINT64 n){ k(n); });
    CHECK(k.calls == 1);
    CHECK(result  == 0);
}

// ── Run — normal time-boxed loop ─────────────────────────────────────────────

TEST_CASE("TimeBox::Run: loops until budget elapses, at least one chunk") {
    FakeTimer::Reset();
    FakeTimer::SetCalibrated(true);
    FakeTimer::SetCyclesPerUs(1);
    // Budget = 1000 µs → 1000 cycles.
    // Each ReadTSC() call advances by 100 cycles (step=100).
    // ReadTSC #1 (start) → 100
    // Per iteration: one ReadTSC → advances 100 cycles.
    // Loop breaks when TSC−start >= 1000:
    //   iter1: elapsed=100−100=0 ... wait, let me trace:
    //   start = ReadTSC() = 100  (first call, step=100: 0+100=100)
    //   iter1: kernel(100), ReadTSC()=200, elapsed=100  < 1000 → continue
    //   iter2: ReadTSC()=300, elapsed=200 < 1000 → continue
    //   ...
    //   iter10: ReadTSC()=1100, elapsed=1000 >= 1000 → break
    // Returns: 10 * 100 = 1000
    FakeTimer::SetStepPerRead(100);
    CountingKernel k;
    UINT64 result = TimeBox::Run(1000, 100, [&k](UINT64 n){ k(n); });
    CHECK(k.calls >= 1);             // at least one chunk always runs
    CHECK(result  == (UINT64)k.calls * 100);  // total = chunks × chunkSize
    CHECK(result  == 1000);          // exact: 10 chunks of 100
}

TEST_CASE("TimeBox::Run: return value equals chunkSize × kernel invocations") {
    FakeTimer::Reset();
    FakeTimer::SetCalibrated(true);
    FakeTimer::SetCyclesPerUs(1);
    FakeTimer::SetStepPerRead(250);  // 4 iterations until 1000 cycles elapsed
    // start=250, iter1:500 elapsed=250, iter2:750 elapsed=500,
    // iter3:1000 elapsed=750, iter4:1250 elapsed=1000 → break after 4 iters
    CountingKernel k;
    const UINT64 CHUNK = 77;
    UINT64 result = TimeBox::Run(1000, CHUNK, [&k](UINT64 n){ k(n); });
    CHECK(result == (UINT64)k.calls * CHUNK);
}

// ── RunWithProgress — uncalibrated ───────────────────────────────────────────

TEST_CASE("TimeBox::RunWithProgress: uncalibrated — single chunk, one progress call") {
    FakeTimer::Reset();
    ProgressRecord pr;
    UINT64 result = TimeBox::RunWithProgress(1000, 50,
        [](UINT64){},
        [&pr](UINT64 elapsed, UINT64 total){ pr.calls++; pr.lastElapsed=elapsed; pr.lastTotal=total; });
    CHECK(result   == 50);
    CHECK(pr.calls == 1);
}

// ── RunWithProgress — normal loop ─────────────────────────────────────────────

TEST_CASE("TimeBox::RunWithProgress: callback fires once per chunk") {
    FakeTimer::Reset();
    FakeTimer::SetCalibrated(true);
    FakeTimer::SetCyclesPerUs(1);
    FakeTimer::SetStepPerRead(100);  // 10 chunks to exhaust 1000µs budget

    ProgressRecord pr;
    UINT64 result = TimeBox::RunWithProgress(1000, 1,
        [](UINT64){},
        [&pr](UINT64 elapsed, UINT64 total){
            pr.calls++; pr.lastElapsed = elapsed; pr.lastTotal = total;
        });

    CHECK(pr.calls == 10);
    CHECK(result   == 10);
}

TEST_CASE("TimeBox::RunWithProgress: elapsedUs is non-decreasing") {
    FakeTimer::Reset();
    FakeTimer::SetCalibrated(true);
    FakeTimer::SetCyclesPerUs(1);
    FakeTimer::SetStepPerRead(50);

    UINT64 prevElapsed = 0;
    bool   monotonic   = true;
    TimeBox::RunWithProgress(500, 1,
        [](UINT64){},
        [&](UINT64 elapsed, UINT64){
            if (elapsed < prevElapsed) monotonic = false;
            prevElapsed = elapsed;
        });
    CHECK(monotonic);
}

TEST_CASE("TimeBox::RunWithProgress: final elapsedUs >= budget when budget expires") {
    FakeTimer::Reset();
    FakeTimer::SetCalibrated(true);
    FakeTimer::SetCyclesPerUs(1);
    FakeTimer::SetStepPerRead(100);

    UINT64 finalElapsed = 0;
    TimeBox::RunWithProgress(1000, 1,
        [](UINT64){},
        [&](UINT64 elapsed, UINT64){ finalElapsed = elapsed; });
    CHECK(finalElapsed >= 1000);
}
