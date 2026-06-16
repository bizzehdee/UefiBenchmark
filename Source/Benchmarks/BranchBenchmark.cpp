// Branch prediction benchmark.
// Sums over a random-byte array gated by data-dependent branches so the
// predictor cannot learn the pattern.

#include "BranchBenchmark.h"
#include "TimeBox.h"
#include "Freestanding.h"

static UINT32 Xorshift32(UINT32 s) {
    s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s;
}

void BranchBenchmark::Setup() {
    UINTN pages = (BUF_BYTES + EFI_PAGE_SIZE - 1) / EFI_PAGE_SIZE;
    EFI_PHYSICAL_ADDRESS addr = 0;
    EFI_STATUS st = gBS->AllocatePages(AllocateAnyPages, EfiLoaderData, pages, &addr);
    mData = EFI_ERROR(st) ? nullptr : reinterpret_cast<UINT8*>(addr);

    if (mData) {
        UINT32 rng = 0xDEADBEEF;
        for (UINTN i = 0; i < BUF_BYTES; ++i) {
            rng = Xorshift32(rng);
            mData[i] = static_cast<UINT8>(rng);
        }
    }
}

void BranchBenchmark::Teardown() {
    if (mData) {
        UINTN pages = (BUF_BYTES + EFI_PAGE_SIZE - 1) / EFI_PAGE_SIZE;
        gBS->FreePages(reinterpret_cast<EFI_PHYSICAL_ADDRESS>(mData), pages);
        mData = nullptr;
    }
}

void BranchBenchmark::RunCore(UINT32 /*workerIndex*/, UINT32 /*totalWorkers*/) {
    ClearNote();
    if (!mData) { SetNote("Out of memory"); return; }

    UINT8* data = mData;

    TimeBox::RunWithProgress(GetBudgetUs(), CHUNK_SIZE, [this, data](UINT64 n) {
        UINT64 sum = 0;
        UINTN mask = BUF_BYTES - 1;  // BUF_BYTES is power of 2
        for (UINT64 i = 0; i < n; ++i) {
            // Unpredictable branch: data byte is essentially random
            if (data[i & mask] & 1) sum += i;
            else                     sum -= i;
        }
        volatile UINT64 sink = sum;
        (void)sink;
        __atomic_fetch_add(const_cast<UINT64*>(&mTotalIter), n, __ATOMIC_RELAXED);
    }, [this](UINT64 e, UINT64) { TryReportProgress(e); });
}
