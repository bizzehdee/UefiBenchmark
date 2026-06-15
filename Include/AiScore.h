#pragma once
// AI Readiness benchmark constants: reference values and weights.
// Reference system (baseline = 1000 AI pts per sub-test):
//   AMD Ryzen 9 5950X (16-core/32-thread, ~4.7 GHz all-core),
//   32 GB DDR4-3766 CL16 dual-channel.
//
// Calibrated from a measured RootBench AI run on that machine. To re-baseline to
// different hardware, run "Run All AI", reveal the calibration readout (press C
// on the AI results screen), set each AI_REF_* to the reported raw metric, bump
// AI_SCORE_VERSION, and update the LLM coefficients from measured tokens/sec.

#include "UefiTypes.h"

// Schema version — bump when reference values change (invalidates stored scores).
static constexpr UINT32 AI_SCORE_VERSION = 2;

// ── Reference values (raw metric / µs on the 5950X baseline) ──
// INT8 GEMM (N=32, multi-core):     raw = mTotalOps / mBudgetUs  (ops/µs = MOPS)
static constexpr UINT64 AI_REF_INT8_MOPS   = 22110;   // measured (Zen3 AVX2 maddubs path)
// INT4 GEMM (packed N=32, multi-core): raw = mTotalOps / mBudgetUs
static constexpr UINT64 AI_REF_INT4_MOPS   = 18305;   // measured
// Sequential read bandwidth (multi-core): raw = mTotalBytes / mBudgetUs (MB/s)
static constexpr UINT64 AI_REF_MEM_MBS     = 54241;   // measured (DDR4-3766 dual-channel)
// Pointer-chase aggregate (4 working sets, single-core): raw = mTotalAccesses / mBudgetUs
static constexpr UINT64 AI_REF_CACHE_MACCS = 97;      // measured (Macc/µs mixed L1/L2/L3/DRAM)

// ── Sub-test weights (must sum to 100) ───────────────────────
static constexpr UINT32 AI_WEIGHT_INT8  = 35;
static constexpr UINT32 AI_WEIGHT_INT4  = 25;
static constexpr UINT32 AI_WEIGHT_MEM   = 25;
static constexpr UINT32 AI_WEIGHT_CACHE = 15;

// ── LLM performance estimates at 1000 AI pts (5950X baseline) ─
// CPU-only single-instance throughput, measured with Ollama (deepseek-r1, Q4) on
// the baseline machine — which now scores 1000 AI pts, so each coefficient is
// just the measured tokens/sec x10: 7B 10.55, 14B 5.85, 32B 2.66 t/s.
static constexpr UINT32 AI_LLM_7B_Q4_TOKS_X10  = 106; // 10.6 tok/s at 1000 pts
static constexpr UINT32 AI_LLM_14B_Q4_TOKS_X10 = 59;  //  5.9 tok/s at 1000 pts
static constexpr UINT32 AI_LLM_32B_Q4_TOKS_X10 = 27;  //  2.7 tok/s at 1000 pts
