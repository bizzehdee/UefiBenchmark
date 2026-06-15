// INT8 GEMM throughput benchmark.
// N=32 matrices stay in L1 per core. AVX2 path uses _mm256_maddubs_epi16
// (uint8 × int8 → int16 pairs) + _mm256_madd_epi16 for int32 accumulation.
// Compiled with -mavx2 (per Makefile per-file override).

#include "AiInt8Benchmark.h"
#include "CpuFeatures.h"
#include "TimeBox.h"
#include <immintrin.h>

// ── Static slot storage ───────────────────────────────────────
INT8  AiInt8Benchmark::sA [AiInt8Benchmark::MAX_WORKERS][AiInt8Benchmark::kN * AiInt8Benchmark::kN];
INT8  AiInt8Benchmark::sBT[AiInt8Benchmark::MAX_WORKERS][AiInt8Benchmark::kN * AiInt8Benchmark::kN];
INT32 AiInt8Benchmark::sC [AiInt8Benchmark::MAX_WORKERS][AiInt8Benchmark::kN * AiInt8Benchmark::kN];

// ── AVX2 dot product: 32-element uint8 × int8 → int32 ────────

__attribute__((target("avx2")))
static INT32 DotProduct32_Avx2(const INT8* a, const INT8* b) {
    const __m256i ones = _mm256_set1_epi16(1);
    __m256i va  = _mm256_loadu_si256((const __m256i*)a);
    __m256i vb  = _mm256_loadu_si256((const __m256i*)b);
    // maddubs: treats va as uint8, vb as int8; multiplies and sums adjacent pairs → int16
    __m256i mul = _mm256_maddubs_epi16(va, vb);
    // madd with 1: sum adjacent int16 pairs → int32
    __m256i acc = _mm256_madd_epi16(mul, ones);
    // Horizontal reduce: 8 int32 → 1 int32
    __m128i lo = _mm256_castsi256_si128(acc);
    __m128i hi = _mm256_extracti128_si256(acc, 1);
    lo = _mm_add_epi32(lo, hi);
    lo = _mm_hadd_epi32(lo, lo);
    lo = _mm_hadd_epi32(lo, lo);
    return (INT32)_mm_cvtsi128_si32(lo);
}

__attribute__((target("avx2")))
static void GemmInt8_Avx2(const INT8* A, const INT8* BT, INT32* C, int N) {
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < N; ++j)
            C[i * N + j] = DotProduct32_Avx2(A + i * N, BT + j * N);
}

// ── Scalar fallback ───────────────────────────────────────────

static void GemmInt8_Scalar(const INT8* A, const INT8* BT, INT32* C, int N) {
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < N; ++j) {
            INT32 sum = 0;
            for (int k = 0; k < N; ++k)
                sum += (INT32)A[i * N + k] * (INT32)BT[j * N + k];
            C[i * N + j] = sum;
        }
}

// ── RunCore ───────────────────────────────────────────────────

void AiInt8Benchmark::RunCore(UINT32 workerIndex, UINT32 /*totalWorkers*/) {
    if (workerIndex >= (UINT32)MAX_WORKERS) return;

    // Initialize this slot's matrices (small positive values, safe for maddubs uint8 path)
    INT8*  A  = sA[workerIndex];
    INT8*  BT = sBT[workerIndex];
    INT32* C  = sC[workerIndex];
    for (int k = 0; k < kN * kN; ++k) {
        A[k]  = (INT8)(1 + (k & 3));  // 1..4
        BT[k] = (INT8)(1 + (k & 1)); // 1..2
    }

    const bool useAvx2 = CpuFeatures::Get().HasAVX2 && CpuFeatures::Get().HasXSave;
    if (useAvx2) CpuFeatures::EnableAvxState();

    constexpr UINT64 MACS_PER_GEMM = (UINT64)kN * kN * kN;

    if (useAvx2) {
        TimeBox::RunWithProgress(GetBudgetUs(), CHUNK_SIZE,
            [this, A, BT, C](UINT64 n) {
                for (UINT64 k = 0; k < n; ++k) {
                    GemmInt8_Avx2(A, BT, C, kN);
                    __asm__ volatile("" : : : "memory");  // each GEMM must run (defeat LICM/DSE)
                }
                __atomic_fetch_add(const_cast<UINT64*>(&mTotalOps), n * MACS_PER_GEMM, __ATOMIC_RELAXED);
            },
            [this](UINT64 e, UINT64) { TryReportProgress(e); });
    } else {
        TimeBox::RunWithProgress(GetBudgetUs(), CHUNK_SIZE,
            [this, A, BT, C](UINT64 n) {
                for (UINT64 k = 0; k < n; ++k) {
                    GemmInt8_Scalar(A, BT, C, kN);
                    __asm__ volatile("" : : : "memory");  // each GEMM must run (defeat LICM/DSE)
                }
                __atomic_fetch_add(const_cast<UINT64*>(&mTotalOps), n * MACS_PER_GEMM, __ATOMIC_RELAXED);
            },
            [this](UINT64 e, UINT64) { TryReportProgress(e); });
    }

    // Prevent dead-code elimination of C
    volatile INT32 sink = C[0];
    (void)sink;
}
