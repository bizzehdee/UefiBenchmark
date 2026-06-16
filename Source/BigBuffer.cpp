// Whole-RAM segmented allocator using UEFI AllocatePages.
// Strategy A: attempt 1 GB chunks, halve on failure, stop at 2 MB.

#include "BigBuffer.h"
#include "SystemInfo.h"

// ── Shared singleton state ────────────────────────────────────

static BigBuffer  gShared;
static UINT32     gRefCount = 0;

BigBuffer* BigBuffer::GetShared() { return &gShared; }

void BigBuffer::AddRef() {
    UINT32 prev = __atomic_fetch_add(&gRefCount, 1U, __ATOMIC_SEQ_CST);
    if (prev == 0) gShared.Allocate(85);
}

void BigBuffer::Release() {
    UINT32 prev = __atomic_fetch_sub(&gRefCount, 1U, __ATOMIC_SEQ_CST);
    if (prev == 1) gShared.Free();
}

// ── Allocation ────────────────────────────────────────────────

void BigBuffer::Allocate(UINT32 pctTarget) {
    if (mCount > 0) return; // already allocated

    UINT64 totalFree = SystemInfo::GetTotalMemoryBytes();
    UINT64 target    = (totalFree / 100ULL) * pctTarget;

    // Leave at least 512 MB headroom for firmware / stack / framebuffer
    constexpr UINT64 HEADROOM = 512ULL * BYTES_PER_MB;
    if (target + HEADROOM > totalFree)
        target = (totalFree > HEADROOM) ? totalFree - HEADROOM : 0;

    UINT64 allocated = 0;
    UINT64 chunkSize = 1ULL * BYTES_PER_GB; // start at 1 GB
    constexpr UINT64 MIN_CHUNK = 2ULL * BYTES_PER_MB; // stop at 2 MB

    while (allocated < target && chunkSize >= MIN_CHUNK
           && mCount < MAX_SEGMENTS) {
        UINTN pages = static_cast<UINTN>(chunkSize / EFI_PAGE_SIZE);
        EFI_PHYSICAL_ADDRESS addr = 0;
        EFI_STATUS st = gBS->AllocatePages(AllocateAnyPages, EfiLoaderData, pages, &addr);
        if (!EFI_ERROR(st)) {
            mSegs[mCount].Base = reinterpret_cast<UINT8*>(addr);
            mSegs[mCount].Size = chunkSize;
            ++mCount;
            allocated += chunkSize;
        } else {
            chunkSize /= 2;
        }
    }

    // Build prefix sum array for fast ByteAt lookup
    mPfx[0] = 0;
    for (UINT32 i = 0; i < mCount; ++i)
        mPfx[i + 1] = mPfx[i] + mSegs[i].Size;

    mTotalSize = mPfx[mCount];
}

void BigBuffer::Free() {
    for (UINT32 i = 0; i < mCount; ++i) {
        UINTN pages = static_cast<UINTN>(mSegs[i].Size / EFI_PAGE_SIZE);
        gBS->FreePages(reinterpret_cast<EFI_PHYSICAL_ADDRESS>(mSegs[i].Base), pages);
    }
    mCount     = 0;
    mTotalSize = 0;
}

// ── Lookup helpers ────────────────────────────────────────────

UINT8* BigBuffer::ByteAt(UINT64 off) const {
    // Binary search on prefix-sum array
    UINT32 lo = 0, hi = mCount;
    while (lo < hi) {
        UINT32 mid = (lo + hi) >> 1;
        if (mPfx[mid + 1] <= off) lo = mid + 1;
        else hi = mid;
    }
    if (lo >= mCount) return nullptr;
    return mSegs[lo].Base + (off - mPfx[lo]);
}

void BigBuffer::GetWorkerRange(UINT32 workerIndex, UINT32 totalWorkers,
                               UINT64* outStart, UINT64* outEnd) const {
    if (totalWorkers == 0) { *outStart = 0; *outEnd = mTotalSize; return; }
    UINT64 perWorker = (mTotalSize / totalWorkers) & ~static_cast<UINT64>(63);
    if (perWorker == 0) perWorker = 64;
    *outStart = static_cast<UINT64>(workerIndex) * perWorker;
    *outEnd   = (workerIndex == totalWorkers - 1)
                    ? mTotalSize
                    : *outStart + perWorker;
}

#ifdef UEFI_HOST_TEST
void BigBuffer::InjectSegments(const BigSegment* segs, UINT32 count) {
    mCount = count < MAX_SEGMENTS ? count : MAX_SEGMENTS;
    mPfx[0] = 0;
    for (UINT32 i = 0; i < mCount; ++i) {
        mSegs[i] = segs[i];
        mPfx[i + 1] = mPfx[i] + segs[i].Size;
    }
    mTotalSize = mPfx[mCount];
}
#endif

UINT32 BigBuffer::GetSpans(UINT64 byteStart, UINT64 byteEnd,
                            BigSegment* spans, UINT32 maxSpans) const {
    UINT32 n = 0;
    for (UINT32 i = 0; i < mCount && n < maxSpans; ++i) {
        UINT64 segBegin = mPfx[i];
        UINT64 segEnd   = mPfx[i + 1];
        if (segEnd   <= byteStart) continue;
        if (segBegin >= byteEnd)   break;

        UINT64 lo = byteStart > segBegin ? byteStart - segBegin : 0;
        UINT64 hi = byteEnd   < segEnd   ? byteEnd   - segBegin : mSegs[i].Size;

        spans[n].Base = mSegs[i].Base + lo;
        spans[n].Size = hi - lo;
        ++n;
    }
    return n;
}
