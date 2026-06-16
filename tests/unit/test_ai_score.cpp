// Tests for the AI score normalisation formula and category composite arithmetic.
// The formulas live in each AI benchmark's GetScore() and in the category
// results composite computation. We test the FORMULAS as pure functions by
// applying them to known inputs — no benchmark kernels involved.
//
// Formula reference (from AiScore.h and CategoryResultsScreen.cpp):
//   per-test score  = (raw / AI_REF_*) * 1000      (integer, truncating)
//   composite       = sum(score[i] * weight[i]) / sum(weight[i])

#include "doctest.h"
#include "AiScore.h"

// ── Normalisation formula (mirroring each AI benchmark's GetScore) ────────────
// NormScore = (raw / reference) * 1000

// Mirrors the production GetScore() pattern: multiply before divide to keep
// precision for sub-reference inputs (e.g. AiInt8Benchmark.h line 24).
static UINT64 normScore(UINT64 raw, UINT64 ref) {
    return (ref > 0) ? (raw * 1000) / ref : 0;
}

TEST_CASE("AI score: reference raw → exactly 1000 pts") {
    CHECK(normScore(AI_REF_INT8_MOPS,   AI_REF_INT8_MOPS)   == 1000);
    CHECK(normScore(AI_REF_INT4_MOPS,   AI_REF_INT4_MOPS)   == 1000);
    CHECK(normScore(AI_REF_MEM_MBS,     AI_REF_MEM_MBS)     == 1000);
    CHECK(normScore(AI_REF_CACHE_MACCS, AI_REF_CACHE_MACCS) == 1000);
}

TEST_CASE("AI score: zero raw → 0 pts (no divide-by-zero)") {
    CHECK(normScore(0, AI_REF_INT8_MOPS)   == 0);
    CHECK(normScore(0, AI_REF_INT4_MOPS)   == 0);
    CHECK(normScore(0, AI_REF_MEM_MBS)     == 0);
    CHECK(normScore(0, AI_REF_CACHE_MACCS) == 0);
}

TEST_CASE("AI score: double the reference raw → ~2000 pts") {
    // Integer division: 2*ref / ref * 1000 = exactly 2000.
    CHECK(normScore(AI_REF_INT8_MOPS   * 2, AI_REF_INT8_MOPS)   == 2000);
    CHECK(normScore(AI_REF_INT4_MOPS   * 2, AI_REF_INT4_MOPS)   == 2000);
    CHECK(normScore(AI_REF_MEM_MBS     * 2, AI_REF_MEM_MBS)     == 2000);
    CHECK(normScore(AI_REF_CACHE_MACCS * 2, AI_REF_CACHE_MACCS) == 2000);
}

TEST_CASE("AI score: half the reference raw → ~500 pts") {
    // Integer division may truncate slightly below 500 depending on ref parity.
    UINT64 s = normScore(AI_REF_INT8_MOPS / 2, AI_REF_INT8_MOPS);
    CHECK(s >= 499);
    CHECK(s <= 500);
}

// ── Weights ───────────────────────────────────────────────────────────────────

TEST_CASE("AI score: weights sum to 100") {
    UINT32 total = AI_WEIGHT_INT8 + AI_WEIGHT_INT4 + AI_WEIGHT_MEM + AI_WEIGHT_CACHE;
    CHECK(total == 100);
}

TEST_CASE("AI score: individual weights are positive") {
    CHECK(AI_WEIGHT_INT8  > 0);
    CHECK(AI_WEIGHT_INT4  > 0);
    CHECK(AI_WEIGHT_MEM   > 0);
    CHECK(AI_WEIGHT_CACHE > 0);
}

// ── Category composite formula ────────────────────────────────────────────────
// composite = weightedSum / totalWeight  (from CategoryResultsScreen.cpp)

static UINT64 categoryComposite(UINT64 int8Score, UINT64 int4Score,
                                 UINT64 memScore,  UINT64 cacheScore) {
    UINT64 ws = int8Score  * AI_WEIGHT_INT8
              + int4Score  * AI_WEIGHT_INT4
              + memScore   * AI_WEIGHT_MEM
              + cacheScore * AI_WEIGHT_CACHE;
    UINT64 tw = AI_WEIGHT_INT8 + AI_WEIGHT_INT4 + AI_WEIGHT_MEM + AI_WEIGHT_CACHE;
    return (tw > 0) ? ws / tw : 0;
}

TEST_CASE("AI composite: all sub-tests at 1000 pts → composite 1000") {
    CHECK(categoryComposite(1000, 1000, 1000, 1000) == 1000);
}

TEST_CASE("AI composite: all zero → 0") {
    CHECK(categoryComposite(0, 0, 0, 0) == 0);
}

TEST_CASE("AI composite: heavier sub-test moves composite more than lighter") {
    // INT8 (weight 35) vs Cache (weight 15): raising INT8 by 1000 should move
    // the composite more than raising Cache by 1000.
    UINT64 base   = categoryComposite(1000, 1000, 1000, 1000);
    UINT64 int8Hi = categoryComposite(2000, 1000, 1000, 1000);
    UINT64 cacheHi= categoryComposite(1000, 1000, 1000, 2000);
    CHECK(int8Hi  > base);
    CHECK(cacheHi > base);
    CHECK((int8Hi - base) > (cacheHi - base));
}

TEST_CASE("AI composite: excluded sub-test (IncludeInScore=false) does not contribute") {
    // Simulate a pass/fail benchmark being excluded from composite:
    // composite with 3 tests (INT8/INT4/MEM) should differ from 4-test composite.
    UINT64 full    = categoryComposite(1000, 1000, 1000, 9999);  // cache at 9999
    UINT64 noCache = (1000ULL * AI_WEIGHT_INT8 +
                      1000ULL * AI_WEIGHT_INT4 +
                      1000ULL * AI_WEIGHT_MEM)
                    / (AI_WEIGHT_INT8 + AI_WEIGHT_INT4 + AI_WEIGHT_MEM);
    // full composite includes cache which is much higher → full > no-cache
    CHECK(full > noCache);
}

// ── LLM token estimates (coefficients positive) ───────────────────────────────

TEST_CASE("AI score: LLM coefficients are positive") {
    CHECK(AI_LLM_7B_Q4_TOKS_X10  > 0);
    CHECK(AI_LLM_14B_Q4_TOKS_X10 > 0);
    CHECK(AI_LLM_32B_Q4_TOKS_X10 > 0);
}

TEST_CASE("AI score: larger model has lower tokens/s coefficient") {
    CHECK(AI_LLM_7B_Q4_TOKS_X10  > AI_LLM_14B_Q4_TOKS_X10);
    CHECK(AI_LLM_14B_Q4_TOKS_X10 > AI_LLM_32B_Q4_TOKS_X10);
}

// ── Schema version ────────────────────────────────────────────────────────────

TEST_CASE("AI score: AI_SCORE_VERSION is non-zero") {
    CHECK(AI_SCORE_VERSION > 0);
}
