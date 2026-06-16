// INT4 packed GEMM throughput benchmark.
// A rows are stored as 2 INT4 values per byte; each dot product unpacks one row
// before calling the AVX2 (or scalar) kernel. This exercises the INT4 dequant
// overhead typical in quantized LLM inference on CPUs without native INT4 support.

#include "AiInt4Benchmark.h"
#include "CpuFeatures.h"
#include "TimeBox.h"
#ifndef __SSE3__
#  define __SSE3__  1
#endif
#ifndef __SSSE3__
#  define __SSSE3__ 1
#endif
#ifndef __AVX__
#  define __AVX__  1
#endif
#ifndef __AVX2__
#  define __AVX2__ 1
#endif
#include <immintrin.h>

// ── Static slot storage ───────────────────────────────────────
UINT8 AiInt4Benchmark::sPackedA [AiInt4Benchmark::MAX_WORKERS][AiInt4Benchmark::kN * AiInt4Benchmark::kPackedN];
INT8  AiInt4Benchmark::sBT      [AiInt4Benchmark::MAX_WORKERS][AiInt4Benchmark::kN * AiInt4Benchmark::kN];
INT32 AiInt4Benchmark::sC       [AiInt4Benchmark::MAX_WORKERS][AiInt4Benchmark::kN * AiInt4Benchmark::kN];
INT8  AiInt4Benchmark::sUnpacked[AiInt4Benchmark::MAX_WORKERS][AiInt4Benchmark::kN];

// ── Unpack a row of kPackedN bytes → kN INT8 values ──────────

static inline void UnpackRow(const UINT8* packed, INT8* out, int nPacked) {
    for (int i = 0; i < nPacked; ++i) {
        out[2 * i]     = (INT8)(packed[i] & 0xFU);         // low nibble: 0-15
        out[2 * i + 1] = (INT8)((packed[i] >> 4) & 0xFU); // high nibble: 0-15
    }
}

// ── AVX2 dot product: 32-element uint8 × int8 → int32 ────────

__attribute__((target("avx2")))
static INT32 DotProduct32_Avx2(const INT8* a, const INT8* b) {
    const __m256i ones = _mm256_set1_epi16(1);
    __m256i va  = _mm256_loadu_si256((const __m256i*)a);
    __m256i vb  = _mm256_loadu_si256((const __m256i*)b);
    __m256i mul = _mm256_maddubs_epi16(va, vb);
    __m256i acc = _mm256_madd_epi16(mul, ones);
    __m128i lo  = _mm256_castsi256_si128(acc);
    __m128i hi  = _mm256_extracti128_si256(acc, 1);
    lo = _mm_add_epi32(lo, hi);
    lo = _mm_hadd_epi32(lo, lo);
    lo = _mm_hadd_epi32(lo, lo);
    return (INT32)_mm_cvtsi128_si32(lo);
}

__attribute__((target("avx2")))
static void GemmInt4_Avx2(const UINT8* packedA, const INT8* BT, INT32* C,
                           INT8* up, int N, int Np) {
    for (int i = 0; i < N; ++i) {
        UnpackRow(packedA + i * Np, up, Np);
        for (int j = 0; j < N; ++j)
            C[i * N + j] = DotProduct32_Avx2(up, BT + j * N);
    }
}

// ── Scalar fallback ───────────────────────────────────────────

static void GemmInt4_Scalar(const UINT8* packedA, const INT8* BT, INT32* C,
                             INT8* up, int N, int Np) {
    for (int i = 0; i < N; ++i) {
        UnpackRow(packedA + i * Np, up, Np);
        for (int j = 0; j < N; ++j) {
            INT32 sum = 0;
            for (int k = 0; k < N; ++k)
                sum += (INT32)up[k] * (INT32)BT[j * N + k];
            C[i * N + j] = sum;
        }
    }
}

// ── RunCore ───────────────────────────────────────────────────

void AiInt4Benchmark::RunCore(UINT32 workerIndex, UINT32 /*totalWorkers*/) {
    if (workerIndex >= (UINT32)MAX_WORKERS) return;

    // Init packed A and BT with small positive values
    UINT8* pA = sPackedA[workerIndex];
    INT8*  BT = sBT[workerIndex];
    INT8*  up = sUnpacked[workerIndex];
    INT32* C  = sC[workerIndex];
    for (int k = 0; k < kN * kPackedN; ++k)
        pA[k] = (UINT8)(((k & 7) + 1) | ((((k + 4) & 7) + 1) << 4));
    for (int k = 0; k < kN * kN; ++k)
        BT[k] = (INT8)(1 + (k & 3));

    const bool useAvx2 = CpuFeatures::Get().HasAVX2 && CpuFeatures::Get().HasXSave;
    SetIsa(useAvx2 ? "AVX2" : "scalar");
    if (useAvx2) CpuFeatures::EnableAvxState();

    constexpr UINT64 MACS_PER_GEMM = (UINT64)kN * kN * kN;

    if (useAvx2) {
        TimeBox::RunWithProgress(GetBudgetUs(), CHUNK_SIZE,
            [this, pA, BT, C, up](UINT64 n) {
                for (UINT64 k = 0; k < n; ++k) {
                    GemmInt4_Avx2(pA, BT, C, up, kN, kPackedN);
                    __asm__ volatile("" : : : "memory");  // each GEMM must run (defeat LICM/DSE)
                }
                __atomic_fetch_add(const_cast<UINT64*>(&mTotalOps), n * MACS_PER_GEMM, __ATOMIC_RELAXED);
            },
            [this](UINT64 e, UINT64) { TryReportProgress(e); });
    } else {
        TimeBox::RunWithProgress(GetBudgetUs(), CHUNK_SIZE,
            [this, pA, BT, C, up](UINT64 n) {
                for (UINT64 k = 0; k < n; ++k) {
                    GemmInt4_Scalar(pA, BT, C, up, kN, kPackedN);
                    __asm__ volatile("" : : : "memory");  // each GEMM must run (defeat LICM/DSE)
                }
                __atomic_fetch_add(const_cast<UINT64*>(&mTotalOps), n * MACS_PER_GEMM, __ATOMIC_RELAXED);
            },
            [this](UINT64 e, UINT64) { TryReportProgress(e); });
    }

    volatile INT32 sink = C[0];
    (void)sink;
}
