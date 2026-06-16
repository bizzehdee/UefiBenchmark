// L3 Cache Cliff — see L3CacheCliffBenchmark.h.

#include "L3CacheCliffBenchmark.h"
#include "Freestanding.h"   // gBS, EFI_PAGE_SIZE, AllocateAnyPages, EfiLoaderData
#include "Timer.h"

// Number of power-of-2 sizes (1 MB .. 128 MB) that fit in a buffer of `bytes`.
static UINT32 FittingPoints(UINT64 bytes) {
    UINT32 n = 0;
    for (UINT64 mb = 1; (mb * BYTES_PER_MB) <= bytes && n < 8; mb <<= 1) ++n;
    return n;
}

UINT64 L3CacheCliffBenchmark::GetBudgetUs() const {
    // Estimate (timed + warm-up per size, with margin) so the runner's progress
    // bar and safety cap are sane. Uses the real buffer size once allocated.
    UINT64 bytes = mBufBytes ? mBufBytes : MAX_BYTES;
    return (UINT64)FittingPoints(bytes) * PER_SIZE_US * 3ULL;
}

// slot[i] (a UINT64 at base + i*64) holds the address of slot[(i*a+c) & (n-1)].
// Full-period LCG over a power-of-2 slot count => one Hamiltonian cycle, one
// access per 64-byte line, randomised so the prefetcher cannot run ahead.
void L3CacheCliffBenchmark::BuildChain(UINT64 nSlots) {
    const UINT64 mask = nSlots - 1ULL;
    UINT8* base = mBuf;
    for (UINT64 i = 0; i < nSlots; ++i) {
        UINT64 next = (i * LCG_KNUTH_A + LCG_KNUTH_C) & mask;
        *reinterpret_cast<UINT64*>(base + i * 64ULL) =
            reinterpret_cast<UINT64>(base + next * 64ULL);
    }
}

void L3CacheCliffBenchmark::SetStatus(UINT64 sizeMB, UINT64 ns) {
    // "64 MB = 85 ns/access" — no sprintf in freestanding, build by hand.
    int p = 0;
    const char* a = UintToStr(sizeMB);
    for (int j = 0; a[j] && p < 8; ++j) mStatus[p++] = a[j];
    mStatus[p++] = ' '; mStatus[p++] = 'M'; mStatus[p++] = 'B';
    mStatus[p++] = ' '; mStatus[p++] = '='; mStatus[p++] = ' ';
    const char* b = UintToStr(ns);
    for (int j = 0; b[j] && p < 40; ++j) mStatus[p++] = b[j];
    mStatus[p++] = ' '; mStatus[p++] = 'n'; mStatus[p++] = 's';
    mStatus[p] = '\0';
}

void L3CacheCliffBenchmark::Setup() {
    UINT64 want = MAX_BYTES;
    while (want >= MIN_BYTES) {
        UINTN pages = static_cast<UINTN>(want / EFI_PAGE_SIZE);
        EFI_PHYSICAL_ADDRESS addr = 0;
        EFI_STATUS st = gBS->AllocatePages(AllocateAnyPages, EfiLoaderData, pages, &addr);
        if (!EFI_ERROR(st)) { mBuf = reinterpret_cast<UINT8*>(addr); mBufBytes = want; return; }
        want >>= 1;
    }
    mBuf = nullptr; mBufBytes = 0;
}

void L3CacheCliffBenchmark::Teardown() {
    if (mBuf) {
        gBS->FreePages(reinterpret_cast<EFI_PHYSICAL_ADDRESS>(mBuf),
                       static_cast<UINTN>(mBufBytes / EFI_PAGE_SIZE));
        mBuf = nullptr; mBufBytes = 0;
    }
}

void L3CacheCliffBenchmark::PreRun() {
    mCount = 0;
    mHeadlineNs = 0;
    mCliffX10 = 0;
    mStatus[0] = '\0';
    for (UINT32 i = 0; i < MAX_POINTS; ++i) { mSizeMB[i] = 0; mNs[i] = 0; }
}

void L3CacheCliffBenchmark::Run() {
    ClearNote();
    if (!mBuf || mBufBytes < MIN_BYTES) { SetNote("Out of memory"); return; }
    if (!Timer::IsCalibrated())        { SetNote("Timer not calibrated"); return; }

    const UINT64 cyc      = Timer::CyclesPerUs();
    const UINT64 timedCyc = PER_SIZE_US * cyc;
    const UINT64 runStart = Timer::ReadTSC();

    UINT64 smallNs = 0, largeNs = 0;

    for (UINT64 sizeMB = 1;
         (sizeMB * BYTES_PER_MB) <= mBufBytes && mCount < MAX_POINTS;
         sizeMB <<= 1) {

        const UINT64 nSlots = (sizeMB * BYTES_PER_MB) / 64ULL;   // power of 2
        BuildChain(nSlots);

        // Warm-up one lap (capped) so cache-resident sizes reach steady state.
        UINT64 cur  = reinterpret_cast<UINT64>(mBuf);
        UINT64 warm = nSlots < (1ULL << 20) ? nSlots : (1ULL << 20);
        for (UINT64 i = 0; i < warm; ++i)
            cur = *reinterpret_cast<UINT64*>(cur);

        // Timed: dependent chase for a fixed wall-clock window; count accesses.
        UINT64 acc = 0;
        UINT64 t0  = Timer::ReadTSC();
        do {
            for (int k = 0; k < 1024; ++k)
                cur = *reinterpret_cast<UINT64*>(cur);
            acc += 1024;
        } while ((Timer::ReadTSC() - t0) < timedCyc);
        UINT64 elapsedCyc = Timer::ReadTSC() - t0;

        // Value-volatile sink: keep the dependent chase from being elided at -O2.
        volatile UINT64 sink = cur;
        (void)sink;

        UINT64 ns = (elapsedCyc * 1000ULL) / (cyc * acc);   // ns per access
        mSizeMB[mCount] = static_cast<UINT32>(sizeMB);
        mNs[mCount]     = ns;
        ++mCount;

        if (mCount == 1) smallNs = ns;
        largeNs = ns;
        if (sizeMB == HEADLINE_MB) mHeadlineNs = ns;

        SetStatus(sizeMB, ns);
        TryReportProgress((Timer::ReadTSC() - runStart) / cyc);
    }

    // If 64 MB didn't fit, fall back to the largest measured size for the score.
    if (mHeadlineNs == 0) mHeadlineNs = largeNs;
    if (smallNs > 0)      mCliffX10   = (largeNs * 10ULL) / smallNs;
}

UINT32 L3CacheCliffBenchmark::GetSweep(UINT32* sizesMB, UINT64* values, UINT32 maxN) const {
    UINT32 n = mCount < maxN ? mCount : maxN;
    for (UINT32 i = 0; i < n; ++i) { sizesMB[i] = mSizeMB[i]; values[i] = mNs[i]; }
    return n;
}
