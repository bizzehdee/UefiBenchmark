#pragma once
// Base class for all time-boxed long benchmarks.
// Stores a progress callback and provides TryReportProgress() — safe to call
// from both the BSP and APs. At most one AP renders at a time via atomic trylock.
// Rendering is rate-limited to 2 Hz to keep GOP Blt overhead negligible.

#include "IBenchmark.h"
#include "Timer.h"
#include "BenchmarkConstants.h"
#include "MachineCheck.h"

class LongBenchmarkBase : public IBenchmark {
public:
    void SetProgressCallback(ProgressFn fn, void* ctx) override {
        mProgressFn    = fn;
        mProgressCtx   = ctx;
        mLastRenderTsc = 0;
        mLastElapsedUs = 0;
    }

    // Must be implemented by each subclass to expose its budget duration.
    virtual UINT64 GetBudgetUs() const override = 0;

    // Reason this benchmark produced no usable result (set via SetNote on a
    // recoverable bail-out). Read by the runner after the run completes.
    const char* GetStatusNote() const override { return mStatusNote; }

protected:
    // Record/clear the recoverable-failure reason. Call ClearNote() at the top
    // of RunCore/Run before any early-return guards, then SetNote("...") on the
    // bail path. Writing a pointer is atomic on x86, so it is safe for APs to
    // set the same string concurrently. The runner reads it on the BSP after
    // the dispatch completes (the completion event provides the barrier).
    void SetNote(const char* note) { mStatusNote = note; }
    void ClearNote()               { mStatusNote = nullptr; }
    // Divisor for live throughput scores: the elapsed time so far while running,
    // falling back to the full budget before the first progress tick (and after
    // the run, where it holds the final elapsed). Lets GetScore() report a true
    // running rate instead of ramping up against the full budget.
    UINT64 ScoreDurationUs() const {
        UINT64 e = mLastElapsedUs;
        return e ? e : GetBudgetUs();
    }

    // Call from RunCore after each TimeBox chunk.
    // Acquires trylock, enforces 500 ms minimum gap, then invokes the callback.
    void TryReportProgress(UINT64 elapsedUs) {
        // Publish latest elapsed for ScoreDurationUs() unconditionally — even
        // when no callback is set (BSP-driven rendering: APs only publish here,
        // they never render) or a render is skipped below (trylock/rate-limit).
        mLastElapsedUs = elapsedUs;

        // Per-CPU machine-check poll. Runs on whichever processor is executing
        // the kernel (every AP in a multi-core run, since they reach here each
        // chunk with mProgressFn == null). Self-throttled to ~100 ms per CPU, so
        // the MSR access does not perturb the measured throughput.
        MachineCheck::PollLocal();

        if (!mProgressFn) return;

        // Trylock: skip if another AP is rendering
        UINT32 expected = 0;
        if (!__atomic_compare_exchange_n(
                &mRenderLock, &expected, 1U,
                false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
            return;

        // Rate-limit: minimum 500 ms between renders
        if (Timer::IsCalibrated()) {
            UINT64 now = Timer::ReadTSC();
            if ((now - mLastRenderTsc) < Timer::CyclesPerUs() * 500000ULL) {
                __atomic_store_n(&mRenderLock, 0U, __ATOMIC_RELEASE);
                return;
            }
            mLastRenderTsc = now;
        }

        ProgressReport r;
        r.ElapsedUs = elapsedUs;
        r.BudgetUs  = GetBudgetUs();
        r.Score     = GetScore();
        r.Unit      = GetUnit();
        r.Status    = GetStatus();
        mProgressFn(r, mProgressCtx);

        __atomic_store_n(&mRenderLock, 0U, __ATOMIC_RELEASE);
    }

private:
    ProgressFn          mProgressFn    = nullptr;
    void*               mProgressCtx   = nullptr;
    volatile UINT32     mRenderLock    = 0;
    volatile UINT64     mLastRenderTsc = 0;
    volatile UINT64     mLastElapsedUs = 0;
    const char* volatile mStatusNote   = nullptr;
};
