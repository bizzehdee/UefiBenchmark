// Tests for freestanding string and memory primitives.
// The shim (host/UefiShim.cpp) provides implementations that are verbatim
// copies of Source/Freestanding.cpp's pure-C routines. These tests pin the
// CONTRACT of those routines — the same assertions apply to the production code.

#include "doctest.h"
#include "Freestanding.h"

// ── StrLen ──────────────────────────────────────────────────────────────────

TEST_CASE("StrLen: empty string returns 0") {
    CHECK(StrLen("") == 0);
}

TEST_CASE("StrLen: returns byte count before NUL") {
    CHECK(StrLen("a")     == 1);
    CHECK(StrLen("hello") == 5);
    CHECK(StrLen("ab\0x") == 2);  // stops at embedded NUL
}

TEST_CASE("StrLen: null pointer returns 0") {
    CHECK(StrLen(nullptr) == 0);
}

// ── StrCmp ──────────────────────────────────────────────────────────────────

TEST_CASE("StrCmp: equal strings return 0") {
    CHECK(StrCmp("",    "")    == 0);
    CHECK(StrCmp("abc", "abc") == 0);
}

TEST_CASE("StrCmp: lexicographic ordering") {
    CHECK(StrCmp("a", "b") < 0);
    CHECK(StrCmp("b", "a") > 0);
}

TEST_CASE("StrCmp: sign matches first differing byte") {
    // 'A'=65, 'a'=97
    CHECK(StrCmp("A", "a") < 0);
    CHECK(StrCmp("a", "A") > 0);
}

TEST_CASE("StrCmp: shorter prefix sorts before longer") {
    CHECK(StrCmp("abc", "abcd") < 0);
    CHECK(StrCmp("abcd", "abc") > 0);
}

TEST_CASE("StrCmp: null handling") {
    CHECK(StrCmp(nullptr, nullptr) == 0);
    CHECK(StrCmp("x",    nullptr)  >  0);
    CHECK(StrCmp(nullptr, "x")     <  0);
}

// ── StrCopy ─────────────────────────────────────────────────────────────────

TEST_CASE("StrCopy: full copy when source fits") {
    char buf[16] = {};
    StrCopy(buf, "hello", 16);
    CHECK(StrCmp(buf, "hello") == 0);
}

TEST_CASE("StrCopy: always NUL-terminates within bounds") {
    char buf[4] = {'X','X','X','X'};
    StrCopy(buf, "hello", 4);  // fits 3 chars + NUL
    CHECK(buf[3] == '\0');
    CHECK(buf[0] == 'h');
    CHECK(buf[1] == 'e');
    CHECK(buf[2] == 'l');
}

TEST_CASE("StrCopy: truncation at exactly maxLen") {
    char buf[3] = {'X','X','X'};
    StrCopy(buf, "ab", 3);   // fits exactly: 'a','b','\0'
    CHECK(buf[0] == 'a');
    CHECK(buf[1] == 'b');
    CHECK(buf[2] == '\0');
}

TEST_CASE("StrCopy: zero maxLen is a no-op") {
    char buf[4] = {'X','X','X','X'};
    StrCopy(buf, "hello", 0);
    CHECK(buf[0] == 'X');  // untouched
}

TEST_CASE("StrCopy: null src copies empty string") {
    char buf[4] = {'X','X','X','X'};
    StrCopy(buf, nullptr, 4);
    CHECK(buf[0] == '\0');
}

// ── IntToStr ─────────────────────────────────────────────────────────────────

TEST_CASE("IntToStr: zero") {
    CHECK(StrCmp(IntToStr(0), "0") == 0);
}

TEST_CASE("IntToStr: positive values") {
    CHECK(StrCmp(IntToStr(1),       "1")       == 0);
    CHECK(StrCmp(IntToStr(42),      "42")      == 0);
    CHECK(StrCmp(IntToStr(1000000), "1000000") == 0);
}

TEST_CASE("IntToStr: negative values get single leading minus") {
    CHECK(StrCmp(IntToStr(-1),    "-1")    == 0);
    CHECK(StrCmp(IntToStr(-42),   "-42")   == 0);
    CHECK(StrCmp(IntToStr(-1000), "-1000") == 0);
}

TEST_CASE("IntToStr: INT64_MIN is handled") {
    // -9223372036854775808 — the pathological case for signed negation.
    const char* s = IntToStr(static_cast<INT64>(-9223372036854775807LL - 1));
    CHECK(StrCmp(s, "-9223372036854775808") == 0);
}

TEST_CASE("IntToStr: no leading zeros") {
    CHECK(StrLen(IntToStr(100)) == 3);   // "100", not "0100"
}

// ── UintToStr ────────────────────────────────────────────────────────────────

TEST_CASE("UintToStr: zero") {
    CHECK(StrCmp(UintToStr(0), "0") == 0);
}

TEST_CASE("UintToStr: representative values") {
    CHECK(StrCmp(UintToStr(1),          "1")          == 0);
    CHECK(StrCmp(UintToStr(999),        "999")        == 0);
    CHECK(StrCmp(UintToStr(1000000000), "1000000000") == 0);
}

TEST_CASE("UintToStr: UINT64 max round-trips") {
    // 18446744073709551615
    CHECK(StrCmp(UintToStr(18446744073709551615ULL),
                 "18446744073709551615") == 0);
}

TEST_CASE("UintToStr: no leading zeros") {
    CHECK(StrLen(UintToStr(7)) == 1);
}

// ── HexToStr ─────────────────────────────────────────────────────────────────

TEST_CASE("HexToStr: zero-padded to requested digits") {
    CHECK(StrCmp(HexToStr(0,    4), "0000") == 0);
    CHECK(StrCmp(HexToStr(0xFF, 4), "00FF") == 0);
}

TEST_CASE("HexToStr: correct nibbles") {
    CHECK(StrCmp(HexToStr(0xDEAD, 4), "DEAD") == 0);
    CHECK(StrCmp(HexToStr(0x1,    1), "1")    == 0);
}

TEST_CASE("HexToStr: digits smaller than value width truncates high nibbles") {
    // Value 0xABCD, only 2 digits → low 2 nibbles "CD"
    CHECK(StrCmp(HexToStr(0xABCD, 2), "CD") == 0);
}

TEST_CASE("HexToStr: digits larger than needed pads with zeros") {
    CHECK(StrCmp(HexToStr(0xF, 4), "000F") == 0);
}

TEST_CASE("HexToStr: digits clamped to 16") {
    // Should not write past the internal buffer regardless of argument.
    const char* s = HexToStr(0, 100);
    // Length is clamped to 16 zeros
    CHECK(StrLen(s) == 16);
}

// ── memset ───────────────────────────────────────────────────────────────────

TEST_CASE("memset: fills every byte with the given value") {
    UINT8 buf[8] = {};
    memset(buf, 0xAB, 8);
    for (int i = 0; i < 8; ++i) CHECK(buf[i] == 0xAB);
}

TEST_CASE("memset: zero-byte fill is a no-op") {
    UINT8 buf[4] = {1,2,3,4};
    memset(buf, 0, 0);
    CHECK(buf[0] == 1);  // untouched
}

TEST_CASE("memset: returns dest pointer") {
    UINT8 buf[4] = {};
    CHECK(memset(buf, 0, 4) == buf);
}

// ── memcpy ───────────────────────────────────────────────────────────────────

TEST_CASE("memcpy: copies bytes from src to dest") {
    const UINT8 src[5] = {1,2,3,4,5};
    UINT8 dst[5] = {};
    memcpy(dst, src, 5);
    for (int i = 0; i < 5; ++i) CHECK(dst[i] == src[i]);
}

TEST_CASE("memcpy: returns dest") {
    UINT8 src[2] = {7,8}, dst[2] = {};
    CHECK(memcpy(dst, src, 2) == dst);
}

// ── memmove ──────────────────────────────────────────────────────────────────

TEST_CASE("memmove: non-overlapping behaves like memcpy") {
    UINT8 src[4] = {10,20,30,40}, dst[4] = {};
    memmove(dst, src, 4);
    for (int i = 0; i < 4; ++i) CHECK(dst[i] == src[i]);
}

TEST_CASE("memmove: forward overlap (dest > src, regions overlap)") {
    // Copy [0..3] into [1..4] — classic forward overlap.
    UINT8 buf[6] = {1,2,3,4,0,0};
    memmove(buf+1, buf, 4);
    // Expected: buf = {1,1,2,3,4,0}
    CHECK(buf[0] == 1);
    CHECK(buf[1] == 1);
    CHECK(buf[2] == 2);
    CHECK(buf[3] == 3);
    CHECK(buf[4] == 4);
}

TEST_CASE("memmove: backward overlap (src > dest, regions overlap)") {
    UINT8 buf[6] = {0,1,2,3,4,5};
    memmove(buf, buf+1, 4);
    // Expected: {1,2,3,4,4,5}
    CHECK(buf[0] == 1);
    CHECK(buf[1] == 2);
    CHECK(buf[2] == 3);
    CHECK(buf[3] == 4);
}

TEST_CASE("memmove: returns dest") {
    UINT8 buf[4] = {1,2,3,4};
    CHECK(memmove(buf, buf, 4) == buf);
}

// ── memcmp ───────────────────────────────────────────────────────────────────

TEST_CASE("memcmp: equal regions return 0") {
    UINT8 a[4] = {1,2,3,4}, b[4] = {1,2,3,4};
    CHECK(memcmp(a, b, 4) == 0);
}

TEST_CASE("memcmp: sign matches first differing byte") {
    UINT8 a[4] = {1,2,10,4}, b[4] = {1,2,3,4};
    CHECK(memcmp(a, b, 4) > 0);   // a[2]=10 > b[2]=3
    CHECK(memcmp(b, a, 4) < 0);
}

TEST_CASE("memcmp: zero-length comparison returns 0") {
    UINT8 a[1] = {1}, b[1] = {2};
    CHECK(memcmp(a, b, 0) == 0);
}
