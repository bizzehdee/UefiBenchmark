#pragma once
// AI Readiness benchmark constants: reference values and weights.
// Reference system: AMD Ryzen 7 5800X (8-core, DDR4-3200) = 1000 AI pts per sub-test.
//
// CALIBRATE: values below are estimates from published specs / microbenchmark analysis.
// Run the AI suite on a real 5800X and adjust each AI_REF_* so that system scores ~1000.

#include "UefiTypes.h"

// Schema version — bump when reference values change (invalidates stored scores).
static constexpr UINT32 AI_SCORE_VERSION = 1;

// ── Reference values (raw metric / µs on 5800X) ──────────────
// INT8 GEMM (N=32, multi-core):     raw = mTotalOps / mBudgetUs  (ops/µs = MOPS)
static constexpr UINT64 AI_REF_INT8_MOPS   = 240000;  // ~240 GOPS estimated
// INT4 GEMM (packed N=32, multi-core): raw = mTotalOps / mBudgetUs
static constexpr UINT64 AI_REF_INT4_MOPS   = 22000;   // ~22 GOPS (unpack overhead)
// Sequential read bandwidth (multi-core): raw = mTotalBytes / mBudgetUs (MB/s)
static constexpr UINT64 AI_REF_MEM_MBS     = 45000;   // ~45 GB/s DDR4-3200 dual-channel
// Pointer-chase aggregate (4 working sets, single-core): raw = mTotalAccesses / mBudgetUs
static constexpr UINT64 AI_REF_CACHE_MACCS = 44;      // ~44 Macc/µs mixed L1/L2/L3/DRAM

// ── Sub-test weights (must sum to 100) ───────────────────────
static constexpr UINT32 AI_WEIGHT_INT8  = 35;
static constexpr UINT32 AI_WEIGHT_INT4  = 25;
static constexpr UINT32 AI_WEIGHT_MEM   = 25;
static constexpr UINT32 AI_WEIGHT_CACHE = 15;

// ── LLM performance estimates at 1000 AI pts (5800X baseline) ─
// Approximate llama.cpp throughput for CPU-only inference (single instance).
static constexpr UINT32 AI_LLM_7B_Q4_TOKS_X10  = 60;  // 6.0 tok/s at 1000 pts
static constexpr UINT32 AI_LLM_14B_Q4_TOKS_X10 = 28;  // 2.8 tok/s at 1000 pts
static constexpr UINT32 AI_LLM_32B_Q4_TOKS_X10 = 12;  // 1.2 tok/s at 1000 pts
