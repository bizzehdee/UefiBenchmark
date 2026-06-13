// Pi calculation benchmarks.
//
// Both variants compute Pi via the Leibniz (Gregory-Leibniz) series:
//   Pi/4 = 1 - 1/3 + 1/5 - 1/7 + 1/9 - ...
//        = SUM_{k=0}^{N-1}  (-1)^k / (2k + 1)
//
// Scalar version: one term per iteration, auto-vectorisation disabled.
// SIMD version:   two terms per iteration using SSE2 packed doubles.
//
// Both use 100 million terms so the work is identical — the SIMD
// variant should run roughly 2× faster on modern x86-64 CPUs.

#include "PiBenchmark.h"
#include <emmintrin.h>  // SSE2 intrinsics (baseline for x86-64)

static volatile double sPiSink = 0.0;

// ── Scalar ───────────────────────────────────────────────────
// Disable auto-vectorisation so the compiler emits pure scalar
// (addsd / mulsd / divsd) instructions.

#ifdef __clang__
// Clang: handled with loop pragma below
#else
#pragma GCC push_options
#pragma GCC optimize("no-tree-vectorize")
#endif

void PiBenchmarkScalar::Run() {
    constexpr long long N = 100000000LL;  // 100M terms
    double sum  = 0.0;
    double sign = 1.0;

#ifdef __clang__
    #pragma clang loop vectorize(disable) interleave(disable)
#endif
    for (long long k = 0; k < N; ++k) {
        sum += sign / (2.0 * static_cast<double>(k) + 1.0);
        sign = -sign;
    }

    sPiSink = sum * 4.0;
}

#ifndef __clang__
#pragma GCC pop_options
#endif

// ── SIMD (SSE2) ──────────────────────────────────────────────
// Processes two consecutive Leibniz terms per iteration using
// 128-bit packed-double operations.
//
// Lane layout per iteration (k even, k+1 odd):
//   Lane 0: +1 / (2k + 1)        (positive term)
//   Lane 1: -1 / (2(k+1) + 1)    (negative term)
//
// The sign vector [+1, -1] never changes because k advances
// by 2 each step (even → odd pairing repeats).

void PiBenchmarkSimd::Run() {
    constexpr long long N = 100000000LL;  // 100M terms (must be even)

    __m128d vsum   = _mm_setzero_pd();
    __m128d vsigns = _mm_set_pd(-1.0, 1.0);   // lane0 = +1, lane1 = -1
    __m128d vtwo   = _mm_set1_pd(2.0);
    __m128d vone   = _mm_set1_pd(1.0);
    __m128d vstep  = _mm_set1_pd(2.0);

    // vk = [k, k+1] starting at [0, 1]
    __m128d vk = _mm_set_pd(1.0, 0.0);

    for (long long i = 0; i < N; i += 2) {
        // denom = 2*k + 1  for each lane
        __m128d denom = _mm_add_pd(_mm_mul_pd(vtwo, vk), vone);
        // term = sign / denom
        __m128d term = _mm_div_pd(vsigns, denom);
        vsum = _mm_add_pd(vsum, term);
        // advance k by 2 in both lanes
        vk = _mm_add_pd(vk, vstep);
    }

    // Horizontal sum of the two lanes
    // _mm_unpackhi_pd gives [lane1, lane1], add to [lane0, lane1]
    __m128d hi = _mm_unpackhi_pd(vsum, vsum);
    __m128d total = _mm_add_sd(vsum, hi);

    double result;
    _mm_store_sd(&result, total);
    sPiSink = result * 4.0;
}
