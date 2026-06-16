// Tests for AiSuitability::Evaluate and TierName (AiSuitability.h).
// All functions are inline in the header — no production .cpp to compile.

#include "doctest.h"
#include "Freestanding.h"
#include "AiSuitability.h"

using namespace AiSuitability;

// ── Helper: build a zeroed Features struct ────────────────────────────────────
static CpuFeatures::Features noFeatures() {
    CpuFeatures::Features f = {};  // all false
    return f;
}

// ── Tier::Excellent ───────────────────────────────────────────────────────────

TEST_CASE("AiSuitability: AVX512F + AVX512VNNI → Excellent") {
    auto f = noFeatures();
    f.HasAVX512F    = true;
    f.HasAVX512VNNI = true;
    CHECK(Evaluate(f) == Tier::Excellent);
}

TEST_CASE("AiSuitability: AVX512F alone does NOT reach Excellent") {
    auto f = noFeatures();
    f.HasAVX512F = true;
    CHECK(Evaluate(f) != Tier::Excellent);
}

TEST_CASE("AiSuitability: AVX512VNNI alone does NOT reach Excellent") {
    auto f = noFeatures();
    f.HasAVX512VNNI = true;
    CHECK(Evaluate(f) != Tier::Excellent);
}

// ── Tier::VeryGood ───────────────────────────────────────────────────────────

TEST_CASE("AiSuitability: AVX2 + FMA + AVX-VNNI → VeryGood") {
    auto f = noFeatures();
    f.HasAVX2    = true;
    f.HasFMA     = true;
    f.HasAVXVNNI = true;
    CHECK(Evaluate(f) == Tier::VeryGood);
}

TEST_CASE("AiSuitability: AVX2 + FMA without AVX-VNNI does NOT reach VeryGood") {
    auto f = noFeatures();
    f.HasAVX2 = true;
    f.HasFMA  = true;
    // HasAVXVNNI intentionally false
    CHECK(Evaluate(f) != Tier::VeryGood);
}

// ── Tier::Good ────────────────────────────────────────────────────────────────

TEST_CASE("AiSuitability: AVX2 + FMA (no AVX-VNNI) → Good") {
    auto f = noFeatures();
    f.HasAVX2 = true;
    f.HasFMA  = true;
    CHECK(Evaluate(f) == Tier::Good);
}

TEST_CASE("AiSuitability: AVX2 without FMA → Limited (not Good)") {
    auto f = noFeatures();
    f.HasAVX2 = true;
    CHECK(Evaluate(f) == Tier::Limited);
}

TEST_CASE("AiSuitability: FMA without AVX2 → Limited") {
    auto f = noFeatures();
    f.HasFMA = true;
    CHECK(Evaluate(f) == Tier::Limited);
}

// ── Tier::Limited ─────────────────────────────────────────────────────────────

TEST_CASE("AiSuitability: no features → Limited") {
    auto f = noFeatures();
    CHECK(Evaluate(f) == Tier::Limited);
}

TEST_CASE("AiSuitability: SSE4.2 only → Limited") {
    auto f = noFeatures();
    f.HasSSE42 = true;
    CHECK(Evaluate(f) == Tier::Limited);
}

// ── TierName mapping ─────────────────────────────────────────────────────────

TEST_CASE("AiSuitability: TierName returns distinct non-empty strings") {
    const char* names[4] = {
        TierName(Tier::Limited),
        TierName(Tier::Good),
        TierName(Tier::VeryGood),
        TierName(Tier::Excellent),
    };
    for (int i = 0; i < 4; ++i) {
        CHECK(names[i] != nullptr);
        CHECK(StrLen(names[i]) > 0);
    }
    // All four must be distinct
    for (int i = 0; i < 4; ++i)
        for (int j = i+1; j < 4; ++j)
            CHECK(StrCmp(names[i], names[j]) != 0);
}

// ── TierSummary — non-empty and tier-specific ─────────────────────────────────

TEST_CASE("AiSuitability: TierSummary returns a different non-empty string per tier") {
    const char* s[4] = {
        TierSummary(Tier::Limited),
        TierSummary(Tier::Good),
        TierSummary(Tier::VeryGood),
        TierSummary(Tier::Excellent),
    };
    for (int i = 0; i < 4; ++i) {
        CHECK(s[i] != nullptr);
        CHECK(StrLen(s[i]) > 0);
    }
    for (int i = 0; i < 4; ++i)
        for (int j = i+1; j < 4; ++j)
            CHECK(StrCmp(s[i], s[j]) != 0);
}
