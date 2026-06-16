#pragma once
// Whole-RAM segmented buffer for memory benchmarks.
// Uses greedy UEFI AllocatePages to capture ~85-90% of free RAM across
// multiple discontiguous segments, avoiding fragmentation failures.

#include "UefiTypes.h"

struct BigSegment {
    UINT8*  Base;
    UINT64  Size;
};

class BigBuffer {
public:
    static constexpr UINT32 MAX_SEGMENTS = 64;

    // ── Allocation ───────────────────────────────────────────
    // Greedily allocate pctTarget% of free RAM.
    // Call on BSP inside Setup() only; never from APs.
    void Allocate(UINT32 pctTarget = 85);
    void Free();

    // ── Queries ──────────────────────────────────────────────
    UINT64 TotalSize()    const { return mTotalSize; }
    UINT32 SegmentCount() const { return mCount; }
    const BigSegment& GetSegment(UINT32 i) const { return mSegs[i]; }

    // Map a byte offset (0 .. TotalSize-1) to its physical address.
    UINT8* ByteAt(UINT64 byteOffset) const;

    // Map a 64-byte slot index to its physical address.
    UINT8* SlotAddress(UINT64 slotIndex) const { return ByteAt(slotIndex * 64ULL); }

    // Compute [startByte, endByte) for worker workerIndex of totalWorkers.
    // Aligned to 64-byte cache lines.
    void GetWorkerRange(UINT32 workerIndex, UINT32 totalWorkers,
                        UINT64* outStart, UINT64* outEnd) const;

    // Fill spans[] with (base, size) pairs covering [byteStart, byteEnd).
    // Returns the number of spans written (up to maxSpans).
    UINT32 GetSpans(UINT64 byteStart, UINT64 byteEnd,
                    BigSegment* spans, UINT32 maxSpans) const;

    // ── Shared singleton ─────────────────────────────────────
    // Memory benchmarks call AddRef in Setup and Release in Teardown.
    // Allocation happens on the first AddRef; free on the last Release.
    static BigBuffer* GetShared();
    static void AddRef();
    static void Release();

#ifdef UEFI_HOST_TEST
    // Inject a synthetic segment list for unit-test use (no firmware allocation).
    // Builds the prefix-sum array and sets TotalSize() from the injected segments.
    void InjectSegments(const BigSegment* segs, UINT32 count);
#endif

private:
    BigSegment mSegs[MAX_SEGMENTS];
    UINT64     mPfx[MAX_SEGMENTS + 1]; // segment prefix-sum byte offsets
    UINT32     mCount    = 0;
    UINT64     mTotalSize = 0;
};
