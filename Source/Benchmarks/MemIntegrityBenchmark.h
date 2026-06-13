#pragma once
// RAM integrity / march test: writes known patterns to every byte of RAM
// and reads back to verify. Reports MB/s and any mismatch count.
// Multi-core. Score: MB/s verified; ErrorCount contains mismatches.

#include "LongBenchmarkBase.h"
#include "BigBuffer.h"

class MemIntegrityBenchmark : public LongBenchmarkBase {
public:
    const char* GetName()        const override { return "Mem Integrity (March)"; }
    const char* GetDescription() const override {
        return "Write+verify 0x00/0xFF/0xAA/0x55 patterns over all RAM; reports errors";
    }
    const char* GetCategory()    const override { return "Memory"; }

    ThreadingMode GetThreadingMode() const override { return ThreadingMode::MultiOnly; }

    UINT64 GetBudgetUs() const override { return mBudgetUs; }
    // MB/s verified (writes + reads = 2× bytes per pattern × 4 patterns)
    UINT64      GetScore() const override { return mTotalBytes / mBudgetUs; }
    const char* GetUnit()  const override { return "MB/s"; }
    UINT64      GetErrors() const { return mErrorCount; }

    void Setup()    override { BigBuffer::AddRef(); }
    void Teardown() override { BigBuffer::Release(); }
    void PreRun()   override { mTotalBytes = 0; mErrorCount = 0; }
    void Run()      override { RunCore(0, 1); }
    void RunCore(UINT32 workerIndex, UINT32 totalWorkers) override;

private:
    static constexpr UINT64 mBudgetUs = 300ULL * 1000000; // 5 min — thorough

    volatile UINT64 mTotalBytes = 0;
    volatile UINT64 mErrorCount = 0;
};
