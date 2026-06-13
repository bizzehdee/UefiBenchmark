#pragma once
// Time-boxed benchmark runner. Loops a work kernel in chunks until a target
// wall-clock duration elapses. Returns total iterations completed.
// Uses Timer::ReadTSC() / Timer::CyclesPerUs() — no UEFI Boot Services.

#include "UefiTypes.h"
#include "Timer.h"

namespace TimeBox {

// Run kernel(chunkSize) repeatedly until budgetUs microseconds have elapsed.
// Returns total iterations completed (may exceed budgetUs by one chunk's worth).
template<typename Kernel>
UINT64 Run(UINT64 budgetUs, UINT64 chunkSize, Kernel&& kernel) {
    if (!Timer::IsCalibrated() || budgetUs == 0 || chunkSize == 0) {
        kernel(chunkSize);
        return chunkSize;
    }

    const UINT64 cyclesPerUs  = Timer::CyclesPerUs();
    const UINT64 budgetCycles = budgetUs * cyclesPerUs;
    const UINT64 start        = Timer::ReadTSC();
    UINT64 totalIter = 0;

    while (true) {
        kernel(chunkSize);
        totalIter += chunkSize;
        if ((Timer::ReadTSC() - start) >= budgetCycles) break;
    }

    return totalIter;
}

// Like Run, but calls onProgress(elapsedUs, budgetUs) after every chunk.
// Use this for long benchmarks to drive live progress updates.
template<typename Kernel, typename ProgressCb>
UINT64 RunWithProgress(UINT64 budgetUs, UINT64 chunkSize,
                       Kernel&& kernel, ProgressCb&& onProgress) {
    if (!Timer::IsCalibrated() || budgetUs == 0 || chunkSize == 0) {
        kernel(chunkSize);
        onProgress(0, budgetUs);
        return chunkSize;
    }

    const UINT64 cyclesPerUs  = Timer::CyclesPerUs();
    const UINT64 budgetCycles = budgetUs * cyclesPerUs;
    const UINT64 start        = Timer::ReadTSC();
    UINT64 totalIter = 0;

    while (true) {
        kernel(chunkSize);
        totalIter += chunkSize;
        UINT64 elapsed = Timer::ReadTSC() - start;
        onProgress(elapsed / cyclesPerUs, budgetUs);
        if (elapsed >= budgetCycles) break;
    }

    return totalIter;
}

} // namespace TimeBox
