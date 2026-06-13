#pragma once
// Pi calculation benchmarks — scalar and SIMD variants.
// Both compute Pi using the Leibniz series (100M terms).

#include "IBenchmark.h"

class PiBenchmarkScalar : public IBenchmark {
public:
    const char* GetName() const override        { return "Pi (Scalar)"; }
    const char* GetDescription() const override { return "Leibniz series, 100M terms, scalar double"; }
    const char* GetCategory() const override    { return "CPU"; }
    void Run() override;
};

class PiBenchmarkSimd : public IBenchmark {
public:
    const char* GetName() const override        { return "Pi (SIMD/SSE2)"; }
    const char* GetDescription() const override { return "Leibniz series, 100M terms, SSE2 packed double"; }
    const char* GetCategory() const override    { return "CPU"; }
    void Run() override;
};
