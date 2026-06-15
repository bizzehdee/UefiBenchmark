#pragma once
// Shared benchmark constants. Values here were previously duplicated (often
// under different names) across multiple benchmark translation units; keeping
// a single definition avoids drift between the copies.

#include "UefiTypes.h"

// Microsecond conversion factor. Benchmark time budgets are expressed as
// e.g. `180ULL * US_PER_SECOND`; elapsed-time formatting divides by it.
static constexpr UINT64 US_PER_SECOND = 1000000ULL;

// Knuth LCG full-period parameters: a ≡ 1 (mod 4), c odd, modulus = power of 2.
// Used to build Hamiltonian pointer-chase cycles and to keep integer ALU chains
// from being merged by the compiler.
static constexpr UINT64 LCG_KNUTH_A = 6364136223846793005ULL;
static constexpr UINT64 LCG_KNUTH_C = 1442695040888963407ULL;

// Address-encoded test pattern XORed into memory during stress soaks.
static constexpr UINT64 TEST_PATTERN = 0xDEADBEEFCAFEBABEULL;
