#pragma once
// Machine Check Architecture (MCA) polling.
//
// Reads the architectural MCA error banks (IA32_MCi_STATUS, MSR 0x401+i*4)
// between benchmark work chunks to detect *recoverable* hardware errors that
// the CPU corrected silently (ECC scrubs, cache parity, bus errors) and never
// raised a #MC exception for. This is detection-by-polling, not exception
// handling: it only reads MSRs and clears STATUS (write 0) — it never installs
// an interrupt handler, so it cannot wedge the firmware.
//
// Coverage: each logical processor polls its own banks via PollLocal() — the
// BSP from the runner's render loop, every AP from the progress callback. Core
// banks are per-CPU; package banks (memory controller, LLC) are shared, so a
// shared error may be observed by more than one CPU in the same window. We
// treat any non-zero count as "instability detected", so a small overcount of a
// shared error is immaterial; the STATUS clear (write 0) is idempotent, so
// concurrent clears across CPUs are safe.
//
// Limits: a *fatal* #MC (processor context corrupt, STATUS.PCC=1) resets the
// machine before it can be polled — that case needs an exception handler, which
// is a separate piece. Uses the legacy 0x400-based banks (Intel + AMD portable);
// AMD Zen Scalable-MCA banks (0xC000_2000+) are a possible future enhancement.

#include "UefiTypes.h"

struct McaEvent {
    UINT32 Bank;
    UINT64 Status;        // raw IA32_MCi_STATUS
    UINT64 Addr;          // IA32_MCi_ADDR, valid only when Status.ADDRV
    bool   Corrected;     // Status.UC == 0 (hardware auto-recovered)
    bool   Overflow;      // Status.OVER (an earlier error was lost)
};

namespace MachineCheck {

// True if the CPU advertises MCE+MCA (CPUID.1:EDX bits 7 and 14). Cached.
bool Available();

// Reset the running totals and re-baseline every CPU for a new benchmark, so
// the first PollLocal() on each CPU clears (without counting) any pre-existing
// or firmware-logged errors. Call on the BSP before dispatching a benchmark.
void BeginRun();

// Poll THIS logical processor's banks. Safe on the BSP and on any AP. The first
// call on a given CPU after BeginRun() baselines (clears without counting);
// later calls are throttled to ~100 ms per CPU so the MSR access does not
// perturb the throughput being measured. No-op when MCA is unavailable.
void PollLocal();

// Totals observed since the last BeginRun(), aggregated across all CPUs.
UINT32 CorrectedCount();
UINT32 UncorrectedCount();

// Most recently captured event (for display). Returns false if none seen.
bool LastEvent(McaEvent* out);

} // namespace MachineCheck
