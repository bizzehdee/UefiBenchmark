#pragma once
// Base class for all time-boxed long benchmarks.
// Stores a progress callback and provides TryReportProgress() — safe to call
// from both the BSP and APs. At most one AP renders at a time via atomic trylock.
// Rendering is rate-limited to 2 Hz to keep GOP Blt overhead negligible.

#include "IBenchmark.h"
#include "Timer.h"

class LongBenchmarkBase : public IBenchmark {
public:
    DurationClass GetDurationClass() const override { return DurationClass::Long; }

    void SetProgressCallback(ProgressFn fn, void* ctx) override {
        mProgressFn    = fn;
        mProgressCtx   = ctx;
        mLastRenderTsc = 0;
    }

    // Must be implemented by each subclass to expose its budget duration.
    virtual UINT64 GetBudgetUs() const = 0;

protected:
    // Call from RunCore after each TimeBox chunk.
    // Acquires trylock, enforces 500 ms minimum gap, then invokes the callback.
    void TryReportProgress(UINT64 elapsedUs) {
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
        mProgressFn(r, mProgressCtx);

        __atomic_store_n(&mRenderLock, 0U, __ATOMIC_RELEASE);
    }

private:
    ProgressFn       mProgressFn    = nullptr;
    void*            mProgressCtx   = nullptr;
    volatile UINT32  mRenderLock    = 0;
    volatile UINT64  mLastRenderTsc = 0;
};
