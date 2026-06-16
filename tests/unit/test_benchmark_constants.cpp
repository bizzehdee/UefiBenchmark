// Tests for BenchmarkConstants.h — arithmetic properties of shared constants.
// We test MATHEMATICAL PROPERTIES rather than raw values so the tests are
// self-documenting and survive cosmetic renames.

#include "doctest.h"
#include "BenchmarkConstants.h"

TEST_CASE("BenchmarkConstants: US_PER_SECOND is 1 000 000") {
    CHECK(US_PER_SECOND == 1000000ULL);
}

TEST_CASE("BenchmarkConstants: stress bit patterns are as expected") {
    CHECK(PATTERN_ZEROS == 0ULL);
    CHECK(PATTERN_ONES  == 0xFFFFFFFFFFFFFFFFULL);
}

TEST_CASE("BenchmarkConstants: alternating patterns are bitwise complements") {
    // The stress test writes AA then 55 to flip all bits; they must be inverses.
    CHECK((PATTERN_ALT_AA ^ PATTERN_ALT_55) == PATTERN_ONES);
    CHECK((PATTERN_ALT_AA & PATTERN_ALT_55) == 0ULL);
}

TEST_CASE("BenchmarkConstants: alternating patterns have the right period") {
    // 0xAA = 10101010b, 0x55 = 01010101b in each byte.
    CHECK((PATTERN_ALT_AA & 0xFF) == 0xAA);
    CHECK((PATTERN_ALT_55 & 0xFF) == 0x55);
}

TEST_CASE("BenchmarkConstants: LCG_KNUTH_A satisfies A ≡ 1 (mod 4)") {
    // Required for a full-period LCG with power-of-2 modulus.
    CHECK((LCG_KNUTH_A % 4) == 1);
}

TEST_CASE("BenchmarkConstants: LCG_KNUTH_C is odd") {
    // Required for a full-period LCG.
    CHECK((LCG_KNUTH_C % 2) == 1);
}

TEST_CASE("BenchmarkConstants: LCG parameters are non-zero") {
    CHECK(LCG_KNUTH_A != 0);
    CHECK(LCG_KNUTH_C != 0);
}
