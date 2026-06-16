// AVX2/FMA vector FP throughput benchmark.
// 10 independent YMM accumulators to hide FMA latency (~4 cycles on Zen3)
// and saturate both FMA pipes. Requires AVX state enabled on the core.
// Falls back to SSE2 scalar if AVX2/FMA are absent.

#include "FpVectorBenchmark.h"
#include "CpuFeatures.h"
#include "TimeBox.h"
#ifndef __SSE3__
#  define __SSE3__ 1
#endif
#ifndef __AVX__
#  define __AVX__  1
#endif
#ifndef __AVX2__
#  define __AVX2__ 1
#endif
#ifndef __FMA__
#  define __FMA__  1
#endif
#include <immintrin.h>   // _mm256_fmadd_pd, _mm256_set1_pd, _mm_hadd_pd

// ── AVX2/FMA kernel ───────────────────────────────────────────

__attribute__((target("avx2,fma")))
static void RunAvx2Kernel(UINT64 n) {
    // 10 accumulators to exceed FMA pipeline depth and keep both FMA units full
    __m256d a0 = _mm256_set1_pd(1.0000001);
    __m256d a1 = _mm256_set1_pd(1.0000002);
    __m256d a2 = _mm256_set1_pd(1.0000003);
    __m256d a3 = _mm256_set1_pd(1.0000004);
    __m256d a4 = _mm256_set1_pd(1.0000005);
    __m256d a5 = _mm256_set1_pd(0.9999991);
    __m256d a6 = _mm256_set1_pd(0.9999992);
    __m256d a7 = _mm256_set1_pd(0.9999993);
    __m256d a8 = _mm256_set1_pd(0.9999994);
    __m256d a9 = _mm256_set1_pd(0.9999995);

    const __m256d mul = _mm256_set1_pd(1.0000001234567);
    const __m256d add = _mm256_set1_pd(0.0000000001111);

    for (UINT64 i = 0; i < n; ++i) {
        a0 = _mm256_fmadd_pd(a0, mul, add);
        a1 = _mm256_fmadd_pd(a1, mul, add);
        a2 = _mm256_fmadd_pd(a2, mul, add);
        a3 = _mm256_fmadd_pd(a3, mul, add);
        a4 = _mm256_fmadd_pd(a4, mul, add);
        a5 = _mm256_fmadd_pd(a5, mul, add);
        a6 = _mm256_fmadd_pd(a6, mul, add);
        a7 = _mm256_fmadd_pd(a7, mul, add);
        a8 = _mm256_fmadd_pd(a8, mul, add);
        a9 = _mm256_fmadd_pd(a9, mul, add);
    }

    // Reduce to prevent DCE
    __m256d sum = _mm256_add_pd(_mm256_add_pd(a0, a1), _mm256_add_pd(a2, a3));
    sum = _mm256_add_pd(sum, _mm256_add_pd(_mm256_add_pd(a4, a5), _mm256_add_pd(a6, a7)));
    sum = _mm256_add_pd(sum, _mm256_add_pd(a8, a9));
    __m128d lo   = _mm256_castpd256_pd128(sum);
    __m128d hi   = _mm256_extractf128_pd(sum, 1);
    __m128d pair = _mm_add_pd(lo, hi);
    double  result;
    _mm_store_sd(&result, _mm_hadd_pd(pair, pair));
    volatile double sink = result;
    (void)sink;
}

// ── SSE2 fallback ─────────────────────────────────────────────

static void RunSse2Fallback(UINT64 n) {
    double x0 = 1.0000001, x1 = 1.0000002, x2 = 1.0000003, x3 = 1.0000004;
    const double m = 1.0000001234567, a = 0.0000000001111;
    for (UINT64 i = 0; i < n; ++i) {
        x0 = x0 * m + a;
        x1 = x1 * m + a;
        x2 = x2 * m + a;
        x3 = x3 * m + a;
    }
    volatile double sink = x0 + x1 + x2 + x3;
    (void)sink;
}

// ── RunCore ───────────────────────────────────────────────────

void FpVectorBenchmark::RunCore(UINT32 /*workerIndex*/, UINT32 /*totalWorkers*/) {
    const auto& feat = CpuFeatures::Get();
    bool useAvx = feat.HasAVX2 && feat.HasFMA && feat.HasXSave;
    SetIsa(useAvx ? "AVX2+FMA" : "SSE2");

    if (useAvx) CpuFeatures::EnableAvxState();

    if (useAvx) {
        TimeBox::RunWithProgress(GetBudgetUs(), CHUNK_SIZE,
            [this](UINT64 n) { RunAvx2Kernel(n); __atomic_fetch_add(const_cast<UINT64*>(&mTotalIter), n, __ATOMIC_RELAXED); },
            [this](UINT64 e, UINT64) { TryReportProgress(e); });
    } else {
        TimeBox::RunWithProgress(GetBudgetUs(), CHUNK_SIZE,
            [this](UINT64 n) { RunSse2Fallback(n); __atomic_fetch_add(const_cast<UINT64*>(&mTotalIter), n, __ATOMIC_RELAXED); },
            [this](UINT64 e, UINT64) { TryReportProgress(e); });
    }
}
