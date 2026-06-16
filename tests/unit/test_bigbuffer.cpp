// Tests for BigBuffer offset-mapping and partition maths.
// Uses InjectSegments() to supply a synthetic segment list — no firmware
// allocation. Tests focus on ByteAt(), GetWorkerRange(), and GetSpans()
// boundary conditions where prefix-sum logic is most likely to be wrong.

#include "doctest.h"
#include "BigBuffer.h"

// Fake base pointers — never dereferenced; only their numeric values matter.
// UINTN is unsigned long (== pointer-sized) in host builds.
static UINT8* const B0 = reinterpret_cast<UINT8*>(static_cast<UINTN>(0x10000));
static UINT8* const B1 = reinterpret_cast<UINT8*>(static_cast<UINTN>(0x20000));
static UINT8* const B2 = reinterpret_cast<UINT8*>(static_cast<UINTN>(0x30000));

// Three-segment buffer: 100 + 50 + 200 = 350 bytes total.
static BigBuffer make350() {
    BigBuffer buf;
    BigSegment segs[3] = { {B0, 100}, {B1, 50}, {B2, 200} };
    buf.InjectSegments(segs, 3);
    return buf;
}

// ── TotalSize / SegmentCount ─────────────────────────────────────────────────

TEST_CASE("BigBuffer: TotalSize equals sum of injected segment sizes") {
    auto buf = make350();
    CHECK(buf.TotalSize() == 350);
}

TEST_CASE("BigBuffer: SegmentCount matches injected count") {
    auto buf = make350();
    CHECK(buf.SegmentCount() == 3);
}

TEST_CASE("BigBuffer: single-segment buffer") {
    BigBuffer buf;
    BigSegment seg = {B0, 1024};
    buf.InjectSegments(&seg, 1);
    CHECK(buf.TotalSize()    == 1024);
    CHECK(buf.SegmentCount() == 1);
}

// ── ByteAt boundary conditions ───────────────────────────────────────────────

TEST_CASE("BigBuffer::ByteAt: offset 0 → first byte of seg0") {
    auto buf = make350();
    CHECK(buf.ByteAt(0) == B0);
}

TEST_CASE("BigBuffer::ByteAt: last byte of seg0") {
    auto buf = make350();
    CHECK(buf.ByteAt(99) == B0 + 99);
}

TEST_CASE("BigBuffer::ByteAt: first byte of seg1 (segment crossing)") {
    auto buf = make350();
    CHECK(buf.ByteAt(100) == B1);
}

TEST_CASE("BigBuffer::ByteAt: last byte of seg1") {
    auto buf = make350();
    CHECK(buf.ByteAt(149) == B1 + 49);
}

TEST_CASE("BigBuffer::ByteAt: first byte of seg2") {
    auto buf = make350();
    CHECK(buf.ByteAt(150) == B2);
}

TEST_CASE("BigBuffer::ByteAt: last valid offset (TotalSize-1)") {
    auto buf = make350();
    CHECK(buf.ByteAt(349) == B2 + 199);
}

TEST_CASE("BigBuffer::ByteAt: out-of-range offset returns nullptr") {
    auto buf = make350();
    CHECK(buf.ByteAt(350) == nullptr);
    CHECK(buf.ByteAt(1000) == nullptr);
}

// ── SlotAddress ──────────────────────────────────────────────────────────────

TEST_CASE("BigBuffer::SlotAddress(i) == ByteAt(i*64)") {
    auto buf = make350();
    CHECK(buf.SlotAddress(0) == buf.ByteAt(0));
    CHECK(buf.SlotAddress(1) == buf.ByteAt(64));
    // slot 2 → offset 128 → inside seg0 (ends at 100) is past seg0, into seg1
    CHECK(buf.SlotAddress(2) == buf.ByteAt(128));
}

// ── GetWorkerRange: no-overlap partition property ────────────────────────────

TEST_CASE("BigBuffer::GetWorkerRange: single worker covers whole buffer") {
    auto buf = make350();
    UINT64 s, e;
    buf.GetWorkerRange(0, 1, &s, &e);
    CHECK(s == 0);
    CHECK(e == 350);
}

TEST_CASE("BigBuffer::GetWorkerRange: two workers partition with no gap") {
    auto buf = make350();
    UINT64 s0, e0, s1, e1;
    buf.GetWorkerRange(0, 2, &s0, &e0);
    buf.GetWorkerRange(1, 2, &s1, &e1);

    // Ranges must be non-overlapping, contiguous, and cover [0, TotalSize).
    CHECK(s0 == 0);
    CHECK(e0 == s1);   // no gap between them
    CHECK(e1 == 350);
    // Each start must be 64-byte aligned (or 0).
    CHECK((s0 % 64) == 0);
    CHECK((s1 % 64) == 0);
}

TEST_CASE("BigBuffer::GetWorkerRange: property — contiguous, no-overlap, full coverage") {
    // Use a buffer and worker count where every worker gets a valid non-negative range.
    // We choose N <= floor(TotalSize/64) so perWorker >= 64.
    // 350 bytes → 5 complete cache lines → use N=5.
    auto buf = make350();
    const UINT32 N = 5;
    UINT64 prevEnd = 0;
    for (UINT32 w = 0; w < N; ++w) {
        UINT64 s, e;
        buf.GetWorkerRange(w, N, &s, &e);
        CHECK(s == prevEnd);   // contiguous (no gap from previous end)
        CHECK(e >= s);         // non-negative length
        CHECK(s % 64 == 0);    // 64-byte aligned start
        prevEnd = e;
    }
    CHECK(prevEnd == 350);     // last worker reaches TotalSize
}

// ── GetSpans ─────────────────────────────────────────────────────────────────

TEST_CASE("BigBuffer::GetSpans: range within one segment → 1 span") {
    auto buf = make350();
    BigSegment spans[4] = {};
    UINT32 n = buf.GetSpans(10, 90, spans, 4);
    CHECK(n == 1);
    CHECK(spans[0].Base == B0 + 10);
    CHECK(spans[0].Size == 80);
}

TEST_CASE("BigBuffer::GetSpans: range crossing seg0/seg1 boundary → 2 spans") {
    // [90, 120) straddles the seg0/seg1 boundary at offset 100.
    auto buf = make350();
    BigSegment spans[4] = {};
    UINT32 n = buf.GetSpans(90, 120, spans, 4);
    CHECK(n == 2);
    // First span: seg0 bytes 90..99
    CHECK(spans[0].Base == B0 + 90);
    CHECK(spans[0].Size == 10);
    // Second span: seg1 bytes 0..19
    CHECK(spans[1].Base == B1);
    CHECK(spans[1].Size == 20);
}

TEST_CASE("BigBuffer::GetSpans: span sizes sum to range length") {
    auto buf = make350();
    BigSegment spans[4] = {};
    // Range [90, 160) crosses seg0/seg1 and seg1/seg2 boundaries.
    UINT32 n = buf.GetSpans(90, 160, spans, 4);
    UINT64 total = 0;
    for (UINT32 i = 0; i < n; ++i) total += spans[i].Size;
    CHECK(total == 160 - 90);
}

TEST_CASE("BigBuffer::GetSpans: maxSpans cap is respected") {
    // [0, 350) spans all 3 segments, but cap is 2 → only 2 returned.
    auto buf = make350();
    BigSegment spans[4] = {};
    UINT32 n = buf.GetSpans(0, 350, spans, 2);
    CHECK(n == 2);
    CHECK(n <= 2);
}

TEST_CASE("BigBuffer::GetSpans: whole-buffer range → spans cover everything") {
    auto buf = make350();
    BigSegment spans[4] = {};
    UINT32 n = buf.GetSpans(0, 350, spans, 4);
    CHECK(n == 3);
    UINT64 total = 0;
    for (UINT32 i = 0; i < n; ++i) total += spans[i].Size;
    CHECK(total == 350);
}

TEST_CASE("BigBuffer::GetSpans: range entirely in last segment") {
    auto buf = make350();
    BigSegment spans[4] = {};
    UINT32 n = buf.GetSpans(200, 300, spans, 4);
    CHECK(n == 1);
    CHECK(spans[0].Base == B2 + 50);
    CHECK(spans[0].Size == 100);
}
