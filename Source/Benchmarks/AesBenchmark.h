#pragma once
// AES-NI encryption throughput benchmark.
// Falls back gracefully if AES-NI is absent.
// Multi-core, time-boxed. Score: MB/s.

#include "LongBenchmarkBase.h"

class AesBenchmark : public LongBenchmarkBase {
public:
    const char* GetName()        const override { return "AES-NI Encryption"; }
    const char* GetDescription() const override {
        return "AES-128 round chaining via _mm_aesenc; measures AES unit throughput (180s)";
    }
    const char* GetCategory()    const override { return "CPU"; }

    ThreadingMode GetThreadingMode() const override { return ThreadingMode::MultiOnly; }

    UINT64 GetBudgetUs() const override { return mBudgetUs; }
    // Each iteration encrypts 16 bytes (one AES block)
    UINT64      GetScore() const override { return (mTotalIter * 16ULL) / mBudgetUs; }
    const char* GetUnit()  const override { return "MB/s"; }

    void Setup()    override;
    void PreRun()   override { mTotalIter = 0; }
    void Run()      override { RunCore(0, 1); }
    void RunCore(UINT32 workerIndex, UINT32 totalWorkers) override;

private:
    static constexpr UINT64 mBudgetUs  = 180ULL * 1000000;
    static constexpr UINT64 CHUNK_SIZE = 1000000ULL;

    // AES-128 key schedule (11 round keys × 16 bytes each)
    alignas(16) UINT8 mRoundKeys[11 * 16] = {};
    bool mHasAes = false;

    volatile UINT64 mTotalIter = 0;
};
