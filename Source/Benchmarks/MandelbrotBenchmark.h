#pragma once
// Mandelbrot escape-time benchmark: mixed FP + data-dependent early exit.
// Multi-core, time-boxed. Score: Miter/s.

#include "LongBenchmarkBase.h"

class MandelbrotBenchmark : public LongBenchmarkBase {
public:
    const char* GetName()        const override { return "Mandelbrot (FP+Branch)"; }
    const char* GetDescription() const override {
        return "Escape-time per-pixel FP loop; mixes FP units with branch predictor (180s)";
    }
    const char* GetCategory()    const override { return "CPU"; }

    ThreadingMode GetThreadingMode() const override { return ThreadingMode::MultiOnly; }

    UINT64 GetBudgetUs() const override { return mBudgetUs; }
    UINT64      GetScore() const override { return mTotalIter / mBudgetUs; }
    const char* GetUnit()  const override { return "Miter/s"; }

    void PreRun()  override { mTotalIter = 0; }
    void Run()     override { RunCore(0, 1); }
    void RunCore(UINT32 workerIndex, UINT32 totalWorkers) override;

private:
    static constexpr UINT64 mBudgetUs   = 180ULL * US_PER_SECOND;
    static constexpr UINT64 CHUNK_PIXELS = 4096;  // pixels per chunk call
    static constexpr int    GRID_W = 512, GRID_H = 512; // fixed grid
    static constexpr int    MAX_ITER = 256;

    volatile UINT64 mTotalIter = 0;
};
