#pragma once
// Sequential memory bandwidth benchmarks using the whole-RAM BigBuffer.
// Three variants: Write (non-temporal), Read, Copy.
// Multi-core, multi-pass. Score: MB/s.

#include "LongBenchmarkBase.h"
#include "BigBuffer.h"

// ── Write bandwidth (non-temporal stores) ─────────────────────

class MemSeqWriteBenchmark : public LongBenchmarkBase {
public:
    const char* GetName()        const override { return "Mem Sequential Write"; }
    const char* GetDescription() const override {
        return "Non-temporal stream stores across whole RAM; measures DRAM write BW";
    }
    const char* GetCategory()    const override { return "Memory"; }

    ThreadingMode GetThreadingMode() const override { return ThreadingMode::MultiOnly; }

    UINT64 GetBudgetUs() const override { return mBudgetUs; }
    UINT64      GetScore() const override { return mTotalBytes / mBudgetUs; }
    const char* GetUnit()  const override { return "MB/s"; }

    void Setup()    override { BigBuffer::AddRef(); }
    void Teardown() override { BigBuffer::Release(); }
    void PreRun()   override { mTotalBytes = 0; }
    void Run()      override { RunCore(0, 1); }
    void RunCore(UINT32 workerIndex, UINT32 totalWorkers) override;

private:
    static constexpr UINT64 mBudgetUs = 180ULL * US_PER_SECOND;
    volatile UINT64 mTotalBytes = 0;
};

// ── Read bandwidth ─────────────────────────────────────────────

class MemSeqReadBenchmark : public LongBenchmarkBase {
public:
    const char* GetName()        const override { return "Mem Sequential Read"; }
    const char* GetDescription() const override {
        return "Stream loads + reduce across whole RAM; measures DRAM read BW";
    }
    const char* GetCategory()    const override { return "Memory"; }

    ThreadingMode GetThreadingMode() const override { return ThreadingMode::MultiOnly; }

    UINT64 GetBudgetUs() const override { return mBudgetUs; }
    UINT64      GetScore() const override { return mTotalBytes / mBudgetUs; }
    const char* GetUnit()  const override { return "MB/s"; }

    void Setup()    override { BigBuffer::AddRef(); }
    void Teardown() override { BigBuffer::Release(); }
    void PreRun()   override { mTotalBytes = 0; }
    void Run()      override { RunCore(0, 1); }
    void RunCore(UINT32 workerIndex, UINT32 totalWorkers) override;

private:
    static constexpr UINT64 mBudgetUs = 180ULL * US_PER_SECOND;
    volatile UINT64 mTotalBytes = 0;
};

// ── Copy bandwidth (STREAM triad-like) ────────────────────────

class MemCopyBenchmark : public LongBenchmarkBase {
public:
    const char* GetName()        const override { return "Mem Copy Bandwidth"; }
    const char* GetDescription() const override {
        return "Read+write copy across whole RAM (STREAM triad); measures combined BW";
    }
    const char* GetCategory()    const override { return "Memory"; }

    ThreadingMode GetThreadingMode() const override { return ThreadingMode::MultiOnly; }

    UINT64 GetBudgetUs() const override { return mBudgetUs; }
    UINT64      GetScore() const override { return mTotalBytes / mBudgetUs; }
    const char* GetUnit()  const override { return "MB/s"; }

    void Setup()    override { BigBuffer::AddRef(); }
    void Teardown() override { BigBuffer::Release(); }
    void PreRun()   override { mTotalBytes = 0; }
    void Run()      override { RunCore(0, 1); }
    void RunCore(UINT32 workerIndex, UINT32 totalWorkers) override;

private:
    static constexpr UINT64 mBudgetUs = 180ULL * US_PER_SECOND;
    volatile UINT64 mTotalBytes = 0;
};
