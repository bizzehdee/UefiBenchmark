// CRC32/SSE4.2 hashing throughput benchmark.
// Streams _mm_crc32_u64 over a 4 KB buffer in L1 cache to isolate the
// CRC fixed-function unit throughput.

#include "HashBenchmark.h"
#include "CpuFeatures.h"
#include "TimeBox.h"
#include "Freestanding.h"
#ifndef __SSE4_2__
#  define __SSE4_2__ 1
#endif
#include <nmmintrin.h>   // _mm_crc32_u64

static UINT32 Xorshift32(UINT32 s) {
    s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s;
}

void HashBenchmark::Setup() {
    UINTN pages = (BUF_BYTES + EFI_PAGE_SIZE - 1) / EFI_PAGE_SIZE;
    EFI_PHYSICAL_ADDRESS addr = 0;
    EFI_STATUS st = gBS->AllocatePages(AllocateAnyPages, EfiLoaderData, pages, &addr);
    mData = EFI_ERROR(st) ? nullptr : reinterpret_cast<UINT8*>(addr);

    if (mData) {
        UINT32 rng = 0x12345678;
        for (UINTN i = 0; i < BUF_BYTES; ++i) {
            rng = Xorshift32(rng);
            mData[i] = static_cast<UINT8>(rng);
        }
    }
}

void HashBenchmark::Teardown() {
    if (mData) {
        UINTN pages = (BUF_BYTES + EFI_PAGE_SIZE - 1) / EFI_PAGE_SIZE;
        gBS->FreePages(reinterpret_cast<EFI_PHYSICAL_ADDRESS>(mData), pages);
        mData = nullptr;
    }
}

static constexpr UINTN kBufBytes = 4096; // matches HashBenchmark::BUF_BYTES

// ── SSE4.2 CRC32 kernel ───────────────────────────────────────

__attribute__((target("sse4.2")))
static void RunCrc32Kernel(UINT64 n, const UINT8* data) {
    UINT64 crc = 0;
    constexpr UINTN WORDS = kBufBytes / sizeof(UINT64);
    const UINT64* words = reinterpret_cast<const UINT64*>(data);
    UINTN idx = 0;

    for (UINT64 i = 0; i < n; ++i) {
        crc = _mm_crc32_u64(crc, words[idx]);
        idx = (idx + 1) & (WORDS - 1);
    }

    volatile UINT64 sink = crc;
    (void)sink;
}

// ── Software FNV-1a fallback ──────────────────────────────────

static void RunFnvKernel(UINT64 n, const UINT8* data) {
    UINT64 h = 14695981039346656037ULL;
    constexpr UINT64 FNV_PRIME = 1099511628211ULL;
    UINTN idx = 0;

    for (UINT64 i = 0; i < n; ++i) {
        h ^= static_cast<UINT64>(data[idx]);
        h *= FNV_PRIME;
        idx = (idx + 1) & (kBufBytes - 1);
    }

    volatile UINT64 sink = h;
    (void)sink;
}

// ── RunCore ───────────────────────────────────────────────────

void HashBenchmark::RunCore(UINT32 /*workerIndex*/, UINT32 /*totalWorkers*/) {
    ClearNote();
    if (!mData) { SetNote("Out of memory (buffer alloc failed)"); return; }

    bool hasSse42 = CpuFeatures::Get().HasSSE42;
    const UINT8* data = mData;

    if (hasSse42) {
        TimeBox::RunWithProgress(GetBudgetUs(), CHUNK_SIZE,
            [this, data](UINT64 n) { RunCrc32Kernel(n, data); __atomic_fetch_add(const_cast<UINT64*>(&mTotalIter), n, __ATOMIC_RELAXED); },
            [this](UINT64 e, UINT64) { TryReportProgress(e); });
    } else {
        TimeBox::RunWithProgress(GetBudgetUs(), CHUNK_SIZE,
            [this, data](UINT64 n) { RunFnvKernel(n, data); __atomic_fetch_add(const_cast<UINT64*>(&mTotalIter), n, __ATOMIC_RELAXED); },
            [this](UINT64 e, UINT64) { TryReportProgress(e); });
    }
}
