// Memory throughput benchmark: sequential write+read and random access.
// Allocates buffers via UEFI AllocatePages for page-aligned memory.

#include "MemoryBenchmark.h"
#include "Freestanding.h"

// ── Sequential benchmark ─────────────────────────────────────

void MemoryBenchmarkSeq::Setup() {
    // Allocate page-aligned buffer via AllocatePages
    UINTN pages = (BUFFER_SIZE + EFI_PAGE_SIZE - 1) / EFI_PAGE_SIZE;
    EFI_PHYSICAL_ADDRESS addr = 0;
    EFI_STATUS status = gBS->AllocatePages(
        AllocateAnyPages, EfiLoaderData, pages, &addr);
    mBuffer = EFI_ERROR(status) ? nullptr : reinterpret_cast<UINT8*>(addr);
}

void MemoryBenchmarkSeq::Run() {
    if (!mBuffer) return;

    // Sequential write
    for (UINTN i = 0; i < BUFFER_SIZE; ++i) {
        mBuffer[i] = static_cast<UINT8>(i & 0xFF);
    }

    // Sequential read with dependency to prevent optimisation
    volatile UINT64 sum = 0;
    for (UINTN i = 0; i < BUFFER_SIZE; ++i) {
        sum += mBuffer[i];
    }
    (void)sum;
}

// Multi-core: each worker gets a cache-line-aligned partition of the buffer.
void MemoryBenchmarkSeq::RunCore(UINT32 workerIndex, UINT32 totalWorkers) {
    if (!mBuffer || totalWorkers == 0) return;

    // Align partition to 64-byte cache lines to avoid false sharing
    UINTN perWorker = (BUFFER_SIZE / totalWorkers) & ~static_cast<UINTN>(63);
    if (perWorker == 0) perWorker = 64;
    UINTN start = workerIndex * perWorker;
    UINTN end   = (workerIndex == totalWorkers - 1) ? BUFFER_SIZE : start + perWorker;
    if (start >= BUFFER_SIZE) return;

    for (UINTN i = start; i < end; ++i)
        mBuffer[i] = static_cast<UINT8>(i & 0xFF);

    volatile UINT64 sum = 0;
    for (UINTN i = start; i < end; ++i)
        sum += mBuffer[i];
    (void)sum;
}

void MemoryBenchmarkSeq::Teardown() {
    if (mBuffer) {
        UINTN pages = (BUFFER_SIZE + EFI_PAGE_SIZE - 1) / EFI_PAGE_SIZE;
        gBS->FreePages(reinterpret_cast<EFI_PHYSICAL_ADDRESS>(mBuffer), pages);
        mBuffer = nullptr;
    }
}

// ── Random-access benchmark ──────────────────────────────────

// Simple xorshift32 PRNG (no external dependency)
static UINT32 Xorshift32(UINT32 state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

void MemoryBenchmarkRandom::Setup() {
    // Allocate data buffer
    UINTN dataPages = (BUFFER_SIZE + EFI_PAGE_SIZE - 1) / EFI_PAGE_SIZE;
    EFI_PHYSICAL_ADDRESS addr = 0;
    EFI_STATUS status = gBS->AllocatePages(
        AllocateAnyPages, EfiLoaderData, dataPages, &addr);
    mBuffer = EFI_ERROR(status) ? nullptr : reinterpret_cast<UINT8*>(addr);

    // Allocate index table
    UINTN idxSize = INDEX_COUNT * sizeof(UINT32);
    UINTN idxPages = (idxSize + EFI_PAGE_SIZE - 1) / EFI_PAGE_SIZE;
    addr = 0;
    status = gBS->AllocatePages(
        AllocateAnyPages, EfiLoaderData, idxPages, &addr);
    mIndices = EFI_ERROR(status) ? nullptr : reinterpret_cast<UINT32*>(addr);

    if (mBuffer && mIndices) {
        // Fill buffer with pattern
        for (UINTN i = 0; i < BUFFER_SIZE; ++i)
            mBuffer[i] = static_cast<UINT8>(i & 0xFF);

        // Pre-compute random indices to avoid RNG cost during measurement
        UINT32 rng = 0xDEADBEEF;
        for (UINTN i = 0; i < INDEX_COUNT; ++i) {
            rng = Xorshift32(rng);
            mIndices[i] = rng % (BUFFER_SIZE - 4);
        }
    }
}

void MemoryBenchmarkRandom::Run() {
    if (!mBuffer || !mIndices) return;

    volatile UINT32 acc = 0;
    for (UINTN i = 0; i < INDEX_COUNT; ++i) {
        UINT32 idx = mIndices[i];
        UINT32 val;
        memcpy(&val, mBuffer + idx, sizeof(UINT32));
        acc += val;
    }
    (void)acc;
}

// Multi-core: each worker reads a partition of the index array.
// All workers read from the shared buffer (read-only — no contention).
void MemoryBenchmarkRandom::RunCore(UINT32 workerIndex, UINT32 totalWorkers) {
    if (!mBuffer || !mIndices || totalWorkers == 0) return;

    UINTN perWorker = INDEX_COUNT / totalWorkers;
    UINTN start = workerIndex * perWorker;
    UINTN end   = (workerIndex == totalWorkers - 1) ? INDEX_COUNT : start + perWorker;
    if (start >= INDEX_COUNT) return;

    volatile UINT32 acc = 0;
    for (UINTN i = start; i < end; ++i) {
        UINT32 idx = mIndices[i];
        UINT32 val;
        memcpy(&val, mBuffer + idx, sizeof(UINT32));
        acc += val;
    }
    (void)acc;
}

void MemoryBenchmarkRandom::Teardown() {
    if (mBuffer) {
        UINTN pages = (BUFFER_SIZE + EFI_PAGE_SIZE - 1) / EFI_PAGE_SIZE;
        gBS->FreePages(reinterpret_cast<EFI_PHYSICAL_ADDRESS>(mBuffer), pages);
        mBuffer = nullptr;
    }
    if (mIndices) {
        UINTN idxSize = INDEX_COUNT * sizeof(UINT32);
        UINTN pages = (idxSize + EFI_PAGE_SIZE - 1) / EFI_PAGE_SIZE;
        gBS->FreePages(reinterpret_cast<EFI_PHYSICAL_ADDRESS>(mIndices), pages);
        mIndices = nullptr;
    }
}
