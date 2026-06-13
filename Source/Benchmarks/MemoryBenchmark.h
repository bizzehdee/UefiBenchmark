#pragma once
// Memory throughput benchmark: sequential and random access patterns.

#include "IBenchmark.h"
#include "UefiTypes.h"

class MemoryBenchmarkSeq : public IBenchmark {
public:
    const char* GetName() const override        { return "Memory Sequential"; }
    const char* GetDescription() const override { return "Sequential R/W over 32 MB buffer"; }
    const char* GetCategory() const override    { return "Memory"; }

    void Setup() override;
    void Run() override;
    void RunCore(UINT32 workerIndex, UINT32 totalWorkers) override;
    void Teardown() override;

private:
    UINT8* mBuffer = nullptr;
    static constexpr UINTN BUFFER_SIZE = 32 * 1024 * 1024; // 32 MB
};

class MemoryBenchmarkRandom : public IBenchmark {
public:
    const char* GetName() const override        { return "Memory Random"; }
    const char* GetDescription() const override { return "Random 4-byte reads over 32 MB buffer"; }
    const char* GetCategory() const override    { return "Memory"; }

    void Setup() override;
    void Run() override;
    void RunCore(UINT32 workerIndex, UINT32 totalWorkers) override;
    void Teardown() override;

private:
    UINT8*  mBuffer  = nullptr;
    UINT32* mIndices = nullptr;
    static constexpr UINTN BUFFER_SIZE = 32 * 1024 * 1024; // 32 MB
    static constexpr UINTN INDEX_COUNT = 1024 * 1024;      // 1M random reads
};
