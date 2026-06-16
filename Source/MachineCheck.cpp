// Machine Check Architecture (MCA) polling — see MachineCheck.h.

#include "MachineCheck.h"
#include "Timer.h"

// ── MSR primitives (ring-0; we run in UEFI at CPL 0) ─────────

static inline UINT64 ReadMsr(UINT32 msr) {
    UINT32 lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return (static_cast<UINT64>(hi) << 32) | lo;
}

static inline void WriteMsr(UINT32 msr, UINT64 val) {
    __asm__ volatile("wrmsr"
                     :
                     : "c"(msr), "a"(static_cast<UINT32>(val)),
                       "d"(static_cast<UINT32>(val >> 32)));
}

// Initial APIC id (CPUID.1:EBX[31:24]). 8-bit; aliases past 256 logical CPUs,
// which only costs a shared throttle slot — never correctness.
static inline UINT32 ApicId() {
    UINT32 a, b, c, d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1) : );
    return (b >> 24) & 0xFFU;
}

// ── MCA register map ──────────────────────────────────────────

static constexpr UINT32 IA32_MCG_CAP = 0x179;       // [7:0] = bank count
static constexpr UINT32 MCi_STATUS_0 = 0x401;       // bank i STATUS = 0x401 + i*4
static constexpr UINT32 MCi_ADDR_0   = 0x402;       // bank i ADDR   = 0x402 + i*4
static constexpr UINT32 MCi_STRIDE   = 4;

static constexpr UINT64 MCi_VAL   = 1ULL << 63;     // entry valid
static constexpr UINT64 MCi_OVER  = 1ULL << 62;     // overflow (errors lost)
static constexpr UINT64 MCi_UC    = 1ULL << 61;     // 1 = uncorrected, 0 = corrected
static constexpr UINT64 MCi_ADDRV = 1ULL << 58;     // ADDR register valid

// ── State ─────────────────────────────────────────────────────

static constexpr UINT32 MAX_CPUS = 256;

struct CpuPollState {
    volatile UINT64 LastPollTsc;
    volatile UINT32 Baselined;     // 0 until first poll of the current run
};

static CpuPollState sCpu[MAX_CPUS];
static volatile UINT32 sBankCount   = 0;            // lazily read from MCG_CAP
static int             sAvail       = -1;           // -1 unknown, 0 no, 1 yes
static volatile UINT32 sCorrected   = 0;
static volatile UINT32 sUncorrected = 0;

// Last captured event (display only). Plain last-wins; minor races are fine
// because the counts above are the authoritative signal.
static volatile bool sHasLast = false;
static McaEvent       sLast    = {};

// ── Helpers ───────────────────────────────────────────────────

bool MachineCheck::Available() {
    if (sAvail < 0) {
        UINT32 a, b, c, d;
        __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1) : );
        // EDX bit 7 = MCE, bit 14 = MCA
        sAvail = ((d & (1U << 7)) && (d & (1U << 14))) ? 1 : 0;
    }
    return sAvail == 1;
}

static UINT32 EnsureBankCount() {
    UINT32 n = sBankCount;
    if (n == 0) {
        n = static_cast<UINT32>(ReadMsr(IA32_MCG_CAP) & 0xFFU);
        if (n == 0)  n = 1;
        if (n > 64)  n = 64;          // legacy banks; clamp defensively
        sBankCount = n;
    }
    return n;
}

static void ClearAllBanks(UINT32 nBanks) {
    // Write 0 to STATUS to clear the VAL bit; the architected way to ack a bank.
    for (UINT32 i = 0; i < nBanks; ++i)
        WriteMsr(MCi_STATUS_0 + i * MCi_STRIDE, 0);
}

// ── Public API ────────────────────────────────────────────────

void MachineCheck::BeginRun() {
    if (!Available()) return;
    sCorrected   = 0;
    sUncorrected = 0;
    sHasLast     = false;
    for (UINT32 i = 0; i < MAX_CPUS; ++i)
        sCpu[i].Baselined = 0;        // force each CPU to re-baseline on next poll
}

void MachineCheck::PollLocal() {
    if (!Available()) return;

    const UINT32 nBanks = EnsureBankCount();
    const UINT32 cpu    = ApicId() & (MAX_CPUS - 1);
    const UINT64 now    = Timer::ReadTSC();

    // First poll on this CPU this run: baseline-clear, do not count stale errors.
    if (!sCpu[cpu].Baselined) {
        ClearAllBanks(nBanks);
        sCpu[cpu].LastPollTsc = now;
        sCpu[cpu].Baselined   = 1;
        return;
    }

    // Throttle to ~100 ms per CPU so MSR access doesn't perturb the measurement.
    if (Timer::IsCalibrated()) {
        const UINT64 gap = Timer::CyclesPerUs() * 100000ULL;
        if (now - sCpu[cpu].LastPollTsc < gap) return;
    }
    sCpu[cpu].LastPollTsc = now;

    for (UINT32 i = 0; i < nBanks; ++i) {
        const UINT64 st = ReadMsr(MCi_STATUS_0 + i * MCi_STRIDE);
        if (!(st & MCi_VAL)) continue;

        const bool corrected = !(st & MCi_UC);
        if (corrected) __atomic_fetch_add(&sCorrected,   1U, __ATOMIC_RELAXED);
        else           __atomic_fetch_add(&sUncorrected, 1U, __ATOMIC_RELAXED);

        sLast.Bank      = i;
        sLast.Status    = st;
        sLast.Addr      = (st & MCi_ADDRV) ? ReadMsr(MCi_ADDR_0 + i * MCi_STRIDE) : 0;
        sLast.Corrected = corrected;
        sLast.Overflow  = (st & MCi_OVER) != 0;
        sHasLast        = true;

        WriteMsr(MCi_STATUS_0 + i * MCi_STRIDE, 0);   // clear so next poll sees fresh
    }
}

UINT32 MachineCheck::CorrectedCount()   { return sCorrected; }
UINT32 MachineCheck::UncorrectedCount() { return sUncorrected; }

bool MachineCheck::LastEvent(McaEvent* out) {
    if (!sHasLast || !out) return false;
    *out = sLast;
    return true;
}
