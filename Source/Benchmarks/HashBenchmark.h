#pragma once
// CRC32/SSE4.2 hashing throughput benchmark.
// Multi-core, time-boxed. Score: MB/s.

#include "LongBenchmarkBase.h"

class HashBenchmark : public LongBenchmarkBase {
public:
    const char* GetName()        const override { return "Hash / CRC32"; }
    const char* GetDescription() const override {
        return "_mm_crc32_u64 over cache-resident block; measures CRC fixed-function unit (150s)";
    }
    const char* GetCategory()    const override { return "CPU"; }

    ThreadingMode GetThreadingMode() const override { return ThreadingMode::MultiOnly; }

    UINT64 GetBudgetUs() const override { return mBudgetUs; }
    // Each iteration processes 8 bytes
    UINT64      GetScore() const override { return (mTotalIter * 8ULL) / mBudgetUs; }
    const char* GetUnit()  const override { return "MB/s"; }

    void Setup()    override;
    void Teardown() override;
    void PreRun()   override { mTotalIter = 0; }
    void Run()      override { RunCore(0, 1); }
    void RunCore(UINT32 workerIndex, UINT32 totalWorkers) override;

private:
    static constexpr UINT64 mBudgetUs  = 150ULL * 1000000;
    static constexpr UINTN  BUF_BYTES  = 4096;   // 4 KB — stays in L1
    static constexpr UINT64 CHUNK_SIZE = 1000000ULL;

    UINT8*  mData     = nullptr;
    volatile UINT64 mTotalIter = 0;
};
