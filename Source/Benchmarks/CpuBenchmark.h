#pragma once
// CPU integer arithmetic benchmark.

#include "IBenchmark.h"

class CpuBenchmark : public IBenchmark {
public:
    const char* GetName() const override        { return "Integer Arithmetic"; }
    const char* GetDescription() const override { return "Add/Mul/Div/XOR throughput with dependency chain"; }
    const char* GetCategory() const override    { return "CPU"; }

    void Run() override;
};
