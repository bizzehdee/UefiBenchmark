# Benchmark Expansion Plan — Long-Running CPU Suite + Whole-RAM Memory Suite

## Goal

Replace/augment the current five short benchmarks with:

1. A **CPU suite** of ~8 benchmarks that together run **~20–30 minutes** on a
   modern desktop (reference target: **AMD Ryzen 7 5800X**, Zen 3, 8C/16T,
   ~4.6–4.85 GHz boost, AVX2 + FMA + AES-NI + SHA, **no AVX-512**), each probing
   a *different* aspect of the core.
2. A **memory suite** of **3–5 benchmarks** that each exercise the **whole of the
   available RAM** (not the current fixed 32 MB), covering bandwidth, latency,
   and full-coverage integrity.

This document is the design/implementation plan only — no code yet.

---

## 1. Design principles ✅

### 1.1 Time-box the CPU benchmarks (do **not** hardcode iteration counts) ✅

The current benchmarks use fixed iteration counts (5M, 100M). Fixed counts make
total runtime depend on clock speed, IPC, and compiler output — fragile for
hitting a "20–30 minute" target. Instead, each long CPU benchmark should **run a
work kernel in chunks until a target wall-clock duration elapses**, then report
*work done per second* as its score.

- Add a helper (`Include/TimeBox.h`): given a target microsecond budget and a
  callable that performs one "chunk" of N kernel iterations, loop calling chunks
  and polling `Timer::ReadTSC()` (converted via `Timer::CyclesPerUs()`) until the
  budget is met. Return total iterations completed.
- This makes total suite time **deterministic** (sum of per-bench budgets)
  regardless of the machine, and turns the score into a meaningful throughput
  figure (ops/s, FLOP/s) instead of "time to do a fixed amount."
- Works on APs: `rdtsc` is pure compute and legal on APs. Each AP time-boxes
  itself to the same budget; the runner already measures the dispatch wall time.

**Default budget:** ~180 s (3 min) per CPU benchmark → 8 × 3 = **24 min**.
Make the budget a per-benchmark constant so individual tests can be tuned
(e.g. latency tests can be shorter, 90–120 s, since they're very stable).

### 1.2 Report throughput, not just time (small harness extension) ✅

Today `BenchmarkResult.Score/Unit/Iterations` are defined but never set or shown.
To display GB/s, MOPS, FLOP/s, ns/access, etc.:

- Extend `IBenchmark` with two optional virtuals:
  - `virtual UINT64 GetScore() const { return 0; }`  — last-run score (fixed-point or integer-scaled)
  - `virtual const char* GetUnit() const { return ""; }`
  - (and optionally `GetIterations()` for the work count)
- In `BenchmarkRunner::RunSingle`, after the run(s), copy `GetScore()/GetUnit()`
  into the `BenchmarkResult`. For multi-core runs, the benchmark accumulates work
  across cores via a shared atomic counter (see §1.3) and computes an aggregate
  score itself.
- Extend the results table in `Tui.cpp` (around line 396–427) with a **Score**
  and **Unit** column. The avg/min/max time columns can stay (now they'll be ~the
  per-bench budget for time-boxed tests, which is expected and fine).

Score is stored as `UINT64`; for fractional units (GB/s, ns) use a fixed scale
(e.g. score = value × 100) and document the divisor in the unit string, or store
integer milli-units. Keep it simple: integer MB/s, integer MOPS, integer ns.

### 1.3 Multi-core accounting ✅

For `Either`/`MultiOnly` benchmarks, `RunCore(workerIndex, totalWorkers)` runs on
each AP. To produce one aggregate score:

- Use a `volatile UINT64` member accumulated with `__atomic_fetch_add` (the
  codebase already uses `__atomic_fetch_add` for the worker index counter in
  `BenchmarkRunner.cpp`).
- Each AP adds its completed iteration count; after dispatch the BSP divides total
  work by the measured wall time to get aggregate throughput.
- Memory bandwidth/latency benchmarks should default to **multi-core** to saturate
  the memory controllers; pure ALU/FP throughput tests should also run multi-core
  to reflect full-chip capability. Provide single-core variants only where the
  per-core figure is the point (latency tests).

### 1.4 Enable AVX2/FMA/AES-NI/SHA under UEFI — **critical bare-metal caveat** ✅

Current `CXXFLAGS` target SSE2 only. SSE is enabled by UEFI firmware, but **AVX
state is typically NOT enabled at UEFI boot**. Executing AVX/AVX2/FMA instructions
without enabling them faults (#UD) or, worse, the OS-state bits being clear means
the upper YMM halves aren't preserved.

Before any AVX benchmark runs, the app must, on the BSP **and every AP that will
execute AVX**:

1. `CPUID` to confirm AVX/AVX2/FMA/AESNI/SHA support (leaf 1 ECX, leaf 7 EBX).
2. Set `CR4.OSXSAVE` (bit 18) and `CR4.OSFXSR`/`OSXMMEXCPT` (bits 9/10).
3. `XSETBV` to set `XCR0` bits 1 (SSE) and 2 (AVX) — requires OSXSAVE first.
4. Only then issue AVX instructions.

Implementation notes:
- Add a `CpuFeatures` module: detect features, and an `EnableAvxState()` routine
  that runs the CR4/XCR0 sequence. Must be called on each AP too (run it at the
  top of the relevant `RunCore`, or via a one-time `StartupAllAPs` init pass).
- ISA-specific source files need ISA flags. Add per-target-file compile flags
  (Makefile pattern rule or per-file overrides): `-mavx2 -mfma` for vector FP,
  `-maes` for AES-NI, `-msha` for SHA, `-msse4.2` for CRC32. Do **not** make these
  global, or the compiler may emit AVX in startup/Renderer code that runs before
  `EnableAvxState()`.
- Guard at runtime: if a feature is absent (e.g. in QEMU/TCG without the flag),
  the benchmark should fall back to an SSE2/scalar path or report "unsupported"
  rather than fault. QEMU note: run with `-cpu max` or `-cpu host,+avx2,+fma,...`
  so the instructions are available when testing.

### 1.5a Short-running vs. long-running classification (UI) ✅

The user wants the original 5 benchmarks shown as **"Short running"** and all new
benchmarks as **"Long running"** in the UI. This is an *orthogonal* dimension to
the existing CPU/Memory `GetCategory()` — keep both.

- Add a duration dimension to `IBenchmark`:
  - `enum class DurationClass { Short, Long };`
  - `virtual DurationClass GetDurationClass() const { return DurationClass::Short; }`
  - The 5 existing benchmarks inherit the default `Short`; every new benchmark in
    §2/§3 overrides to `Long`. (Default = `Short` means we don't have to touch the
    existing five at all.)
- A helper `const char* DurationClassName(DurationClass)` returns
  `"Short running"` / `"Long running"` for display.
- **UI changes** (`Source/Tui.cpp`):
  - **Selection screen** (~L239–264): group the list under two headers,
    **"Short running"** and **"Long running"**, iterating short entries first then
    long (the registry order already places the original 5 first; just emit a
    header row when the class changes). Keep the existing `[CPU]`/`[Memory]` tag on
    each row — the two dimensions are complementary.
  - **Results table** (~L396–427): the 6-char category column stays CPU/Memory.
    Optionally add a 1-char duration marker (`S`/`L`) or a short "Long"/"Short"
    column if width allows (grid is 100 cols).
  - **System-info / benchmark list** (~L505–528): same grouped headers.
- **Run-all behaviour:** because "Run all" of the long suite is ~24+ min, consider
  offering **"Run all short"** and **"Run all long"** as distinct menu actions (the
  duration class makes this a trivial filter on the registry). This also protects a
  user from accidentally kicking off the 24-minute suite when they wanted a quick
  check. Recommended.

This keeps the change small: one enum + one virtual on `IBenchmark`, a default that
leaves the existing five untouched, and grouping/headers in the three TUI lists.

### 1.5 Whole-RAM allocation (memory suite) ✅

`SystemInfo::GetTotalMemoryBytes()` already sums `EfiConventionalMemory`. A single
`AllocatePages` of ~all RAM will usually fail due to fragmentation. Plan:

- Add a `BigBuffer` abstraction (`Include/BigBuffer.h`): greedily allocate the
  largest available conventional regions into a **segment list** until a target
  fraction of free RAM is captured.
  - Strategy A (simple): repeatedly `AllocatePages(AllocateAnyPages, ...)` with a
    decreasing chunk size (e.g. start at 1 GB, halve on failure down to 2 MB),
    collecting each success into a `Vector<Segment{addr,bytes}>`.
  - Strategy B (precise): call `GetMemoryMap`, pick the largest
    `EfiConventionalMemory` descriptors, `AllocatePages(AllocateAddress, ...)` each.
  - Start with Strategy A; it's robust and firmware-portable.
- **Leave headroom**: reserve ~256–512 MB (firmware, stack, GOP framebuffer,
  our own image). Target ~85–90% of reported free RAM. Over-allocation risks
  firmware instability.
- All memory kernels iterate over the **segment list**, treating it as the logical
  buffer. Multi-core partitioning splits the *total* footprint across APs at
  segment + cache-line granularity (extend the existing partition logic in
  `MemoryBenchmark.cpp`).
- Allocate **once** in `Setup()`, reuse across runs, free in `Teardown()`. A
  shared owner (single big-buffer instance) avoids re-allocating per benchmark and
  avoids fragmenting between the five memory tests — consider a shared/static
  `BigBuffer` set up once and pointed to by all memory benchmarks.

---

## 2. CPU benchmark catalog (target ~24 min, 8 × ~3 min) ✅

Each is time-boxed (§1.1) and reports throughput (§1.2). All run multi-core unless
noted; latency tests are single-core-meaningful. **All CPU-suite benchmarks below
are `DurationClass::Long`** (§1.5a) and appear under the "Long running" UI group.

| # | Name | Aspect probed | Kernel | ISA | Threading | Budget | Score unit |
|---|------|---------------|--------|-----|-----------|--------|-----------|
| 1 | Integer Throughput (ILP) | Peak integer IPC; fills all ALU ports | Many **independent** add/sub/and/or/shift chains (8–16 parallel accumulators) | baseline | Multi | 180 s | MOPS |
| 2 | Integer Latency | Per-op latency / serial dependency | Single tight **dependency chain** (each op feeds next; add+mul+xor) | baseline | Single | 120 s | ops/s (1-core) |
| 3 | FP Scalar + Divide | Scalar FPU incl. slow divider | Mixed `add/mul/div/sqrt` on doubles, dependency-balanced (keep some div in chain) | SSE2 scalar | Multi | 180 s | MFLOP/s |
| 4 | Vector FP (AVX2/FMA) | 256-bit SIMD FMA throughput (2×256 FMA on Zen3) | Hand-written `_mm256_fmadd_pd` over many YMM accumulators | **AVX2+FMA** | Multi | 180 s | GFLOP/s |
| 5 | Branch Prediction | Branch predictor + mispredict penalty | Sum over array gated by **unpredictable** data-dependent branches (random bytes) | baseline | Multi | 150 s | M-branches/s |
| 6 | AES-NI Encryption | Dedicated AES units / crypto | AES-128 CBC/CTR rounds via `_mm_aesenc_si128` over a working block | **AES-NI** | Multi | 180 s | MB/s encrypted |
| 7 | Hashing / CRC32 | Integer mixing + SSE4.2 fixed-function | `_mm_crc32_u64` stream (and/or FNV-1a software hash for ALU mixing) | **SSE4.2** | Multi | 150 s | MB/s hashed |
| 8 | Mandelbrot (mixed FP+branch) | Realistic mixed FP + data-dependent loop exit | Per-pixel escape-time iteration over a fixed region, double precision | SSE2 (opt. AVX2) | Multi | 180 s | M-iter/s |

Optional 9th (cache subsystem, sits between CPU and memory):

| 9 | Cache Latency Ladder | L1/L2/L3 access latency | **Pointer-chase** over working sets sized to 16 KB / 256 KB / 4 MB / 32 MB; randomized permutation; report ns at each level | baseline | Single | 120 s | ns/access (per level) |

**Coverage rationale** — these touch distinct execution resources: integer ALU
throughput vs. latency, scalar FPU + divider, the vector/FMA units, the branch
predictor, the AES fixed-function units, the CRC fixed-function unit + integer
mixing, a mixed real-world FP kernel, and (optional) the cache hierarchy. That
spread means a regression or thermal/clock issue shows up in whichever dimension
it affects.

**Total budget:** 8 core tests ≈ 24 min (within 20–30). Adding #9 ≈ 26 min.
Tune individual budgets in code to land the exact total you want. Because the
tests are time-boxed, **the runtime is fixed by the budgets, not the hardware** —
on a slower CPU you get the same wall time but lower scores.

### Per-benchmark implementation notes

- **#1 Integer Throughput:** use ≥8 independent 64-bit accumulators so the
  scheduler can issue in parallel; `volatile` sink at the end. Avoid a single
  dependency chain (that's #2). Don't let the compiler fold — feed an input that
  varies per chunk.
- **#2 Integer Latency:** one accumulator, each op strictly dependent on the
  previous result. Score is meaningful per-core; report single-core.
- **#3 FP Scalar+Divide:** keep a real division in the dependency chain (the Zen3
  divider is a bottleneck and worth measuring); mix in `sqrt` via `_mm_sqrt_sd`.
- **#4 Vector FP:** the headline SIMD number. Many YMM accumulators (≥10) to hide
  FMA latency (~4 cycles) and saturate both FMA pipes. Requires §1.4 AVX enable.
  Provide an SSE2 fallback path for QEMU-without-AVX.
- **#5 Branch Prediction:** pre-generate a random byte array; branch on
  `if (arr[i] & 1)` style conditions that the predictor cannot learn. Working set
  small enough to stay in L1/L2 so you measure branches, not memory.
- **#6 AES-NI:** chain `aesenc` rounds; report bytes processed. Real AES key
  schedule via `_mm_aeskeygenassist`. Fallback: skip/report unsupported if no AESNI.
- **#7 CRC32:** `crc32` over a cache-resident block; optionally also an FNV-1a
  software path to stress the ALU rather than the fixed-function unit. Report MB/s.
- **#8 Mandelbrot:** fixed complex-plane window + max-iteration cap; the
  data-dependent early-exit stresses branch + FP together. Deterministic work per
  pixel grid; time-box by repeating the grid.
- **#9 Cache Ladder:** build a randomized cyclic permutation (pointer chase) per
  working-set size so the prefetcher can't help; report ns/access per level.
  This is the one test where the *latency*, not throughput, is the result.

---

## 3. Memory benchmark catalog (whole RAM, 3–5 tests) ✅

All use the shared `BigBuffer` (§1.5) covering ~85–90% of free RAM. Bandwidth
tests are multi-core; latency is single-core. These don't need to hit 20–30 min —
they should *cover all of RAM*; a few passes each (a few minutes total) is right.
**All memory-suite benchmarks below are `DurationClass::Long`** (§1.5a) — the
original 32 MB Sequential/Random memory tests remain `Short`.

| # | Name | What it measures | Method | Threading | Score unit |
|---|------|------------------|--------|-----------|-----------|
| 1 | Sequential Write Bandwidth | Sustained DRAM write BW | Stream stores across entire footprint; optional **non-temporal** `_mm_stream` to bypass cache and measure true DRAM write BW | Multi | GB/s |
| 2 | Sequential Read Bandwidth | Sustained DRAM read BW | Stream loads + reduce across entire footprint | Multi | GB/s |
| 3 | Copy Bandwidth (STREAM-like) | Combined read+write BW | `memcpy`/triad across the footprint (dst and src both in the big buffer) | Multi | GB/s |
| 4 | Random Access Latency | Full-RAM random latency incl. TLB/page-walk + DRAM | **Pointer-chase** over a randomized permutation spanning the *entire* footprint (working set ≫ caches) | Single | ns/access |
| 5 | Integrity / March Test | Correctness over **every byte** of RAM | Write known patterns (`0x00`, `0xFF`, `0xAA`, `0x55`, and address-encoded word), read back & **verify**; count mismatches | Multi | MB/s + error count |

**Why these five:** #1–#3 characterize bandwidth in all three access modes (write,
read, copy); #4 isolates latency, which bandwidth tests hide; #5 is the
"test the whole of the available memory" requirement in the strict sense — it
*touches and verifies every byte*, reporting any faults (bad RAM, marginal
overclock/EXPO instability). Together they both *benchmark* and *validate* RAM.

If you want exactly **3**: keep Sequential Read, Random Latency, and Integrity
(bandwidth + latency + coverage). Recommend shipping all five.

### Per-benchmark implementation notes

- **Partitioning:** extend the existing cache-line-aligned partition logic to walk
  the segment list and assign each AP a contiguous span of the *total* footprint;
  avoid false sharing (already handled at 64-byte granularity).
- **Non-temporal stores (#1):** `_mm_stream_si128` requires SSE2 (available) and an
  `sfence` at the end; this measures DRAM write BW without cache pollution. Provide
  a temporal variant too for comparison if desired.
- **Latency (#4):** the permutation must span all segments so the chase actually
  reaches DRAM and trips TLB misses; single-core so the result is per-access
  latency, not aggregate BW. Use a cycle-covering permutation (no short cycles).
- **Integrity (#5):** multi-pass, multiple patterns; on mismatch record the
  address and expected/actual. Report total bytes verified and error count. Extend
  `BenchmarkResult` (or use the score field) to surface the error count prominently
  — a non-zero count is the headline.
- **Runtime:** at ~40–50 GB/s effective BW, one full pass over ~28 GB is ~0.6–0.7 s,
  so loop several passes per run for stable timing. The integrity test is the
  longest (multiple patterns × passes) — still only minutes.

---

## 4. Time budget summary

| Suite | Tests | Per-test | Total |
|-------|-------|----------|-------|
| CPU | 8 (core) | ~150–180 s, time-boxed | **~24 min** |
| CPU | +1 cache ladder (optional) | ~120 s | ~26 min |
| Memory | 5 | multi-pass, ~0.5–3 min each | **~6–10 min** |

CPU suite hits the 20–30 min window by construction (sum of budgets). Adjust the
per-benchmark budget constants to fine-tune.

---

## 5. Cross-cutting caveats / risks

- **AVX state enablement (§1.4)** is the highest-risk item. Get `EnableAvxState()`
  + CPUID guards working and tested in QEMU (`-cpu max`) *before* writing the AVX
  kernels. Without it, benchmark #4 (and AVX paths of #8) fault on real hardware.
- **APs and Boot Services:** kernels in `RunCore` must remain pure compute — no
  `gBS` calls (already a documented constraint). All allocation happens on the BSP
  in `Setup()`; AVX-enable on APs uses only CR4/XCR0/CPUID (no Boot Services).
- **Watchdog:** already disabled in `Main.cpp` (`SetWatchdogTimer(0,...)`), so the
  long CPU suite won't trigger a firmware reset. Re-confirm it stays disabled.
- **TSC validity over 20–30 min:** the suite relies on invariant TSC for timing.
  `Timer` already detects/ warns on non-invariant TSC. On the 5800X invariant TSC
  is present; under heavy load the TSC stays constant-rate (it's decoupled from
  the core clock), which is exactly what we want for wall-time budgeting — but it
  means scores reflect *actual* delivered work under whatever boost/thermal state
  the chip sustains. Document that scores are sustained-load figures.
- **Thermals/boost:** a 24-min CPU run will push the 5800X past short-boost into
  sustained clocks; this is desirable (it's a stress + steady-state benchmark), but
  note it in results so users don't compare against single-shot boost numbers.
- **Memory over-allocation:** capping at ~85–90% of free RAM avoids starving the
  firmware. Test the `BigBuffer` greedy allocator on the real machine — firmware
  memory maps vary.
- **QEMU vs. real HW:** QEMU/TCG without `-cpu` flags lacks AVX2/AES/SHA and has
  unrealistic memory behavior. Use QEMU only for functional/boot validation; take
  performance numbers on the 5800X bare metal (or USB boot).
- **Score precision:** decide the fixed-point convention up front (integer MB/s,
  MOPS, ns) and apply consistently so the new Tui column formats cleanly with the
  existing `UintToStr`/`Pad` helpers (no float printing exists in the codebase).

---

## 6. Required infrastructure changes (before/with the benchmarks) ✅

New shared files:

- `Include/TimeBox.h` — time-boxed run helper (§1.1).
- `Include/CpuFeatures.h` + `Source/CpuFeatures.cpp` — CPUID feature detection and
  `EnableAvxState()` (CR4/XCR0) (§1.4).
- `Include/BigBuffer.h` + `Source/BigBuffer.cpp` — whole-RAM segmented allocator (§1.5).

Harness edits:

- `Include/IBenchmark.h` — add `GetScore()`/`GetUnit()` (+ optional
  `GetIterations()`) virtuals (§1.2), and the `DurationClass` enum +
  `GetDurationClass()` virtual defaulting to `Short` (§1.5a).
- `Source/BenchmarkRunner.cpp` — populate `result.Score/Unit/Iterations` from the
  benchmark after running; keep multi-core atomic accumulation in the benchmarks.
- `Source/Tui.cpp` — add Score/Unit columns to the results table (~L396–427); group
  the selection list (~L239–264), results table, and benchmark list (~L505–528)
  under "Short running" / "Long running" headers; optionally add "Run all short" /
  "Run all long" menu actions (§1.5a). Grid is 100 cols.
- `Include/BenchmarkResult.h` — already has `Score/Unit/Iterations`; optionally add
  an `ErrorCount` field for the integrity test.

New benchmark files (one header+cpp per benchmark, mirroring existing layout):

- `Source/Benchmarks/IntThroughputBenchmark.*`, `IntLatencyBenchmark.*`,
  `FpScalarBenchmark.*`, `FpVectorBenchmark.*` (AVX2), `BranchBenchmark.*`,
  `AesBenchmark.*` (AES-NI), `HashBenchmark.*` (CRC32), `MandelbrotBenchmark.*`,
  and optional `CacheLatencyBenchmark.*`.
- `Source/Benchmarks/MemBandwidthBenchmark.*` (read/write/copy variants),
  `MemLatencyBenchmark.*`, `MemIntegrityBenchmark.*` — or extend the existing
  `MemoryBenchmark.*` to use `BigBuffer` and add the new modes.

Wiring (per existing "Adding a New Benchmark" steps in README):

1. Register each new benchmark instance in `Source/Main.cpp` (`EfiMain`).
   - Note: `BenchmarkRegistry` cap is **32** (static array) — 8–9 CPU + 5 memory +
     keeping/removing the existing 5 is well under the cap, but confirm.
   - Decide whether to **retire the original 5** short benchmarks or keep them as a
     "quick" group. Recommendation: keep them (they're cheap) but the new suite is
     the headline.
2. Add each `.cpp` to `Makefile` `SOURCES` (L62–74) and the `Benchmarks/` pattern
   rule already exists (L149).
3. Add each `.cpp` to `UefiBenchmark.inf` `[Sources]` (L16–28).
4. Add ISA compile flags for the SIMD/crypto files (Makefile per-file overrides;
   `.inf` `[BuildOptions]` for EDK II — append `-mavx2 -mfma -maes -msha -msse4.2`
   to the specific objects, **not** globally — §1.4).

---

## 7. Suggested implementation phases

1. **Harness plumbing:** `TimeBox.h`, score/unit reporting, Tui column. Validate
   with the *existing* benchmarks (give them trivial scores) so the display path is
   proven before adding new kernels.
2. **CPU features:** `CpuFeatures` + `EnableAvxState()`; verify in QEMU `-cpu max`
   and (ideally) on the 5800X that AVX2/FMA/AES/SHA execute without faulting.
3. **CPU suite:** implement benchmarks #1–#3, #5 (baseline/SSE2 first — no ISA-enable
   risk), then #4/#6/#7 (ISA-gated), then #8 and optional #9. Time-box each; tune
   budgets to land ~24 min total.
4. **BigBuffer:** implement and test the whole-RAM segmented allocator on real
   hardware (allocation is the riskiest part).
5. **Memory suite:** read/write/copy bandwidth, random latency, integrity — all on
   `BigBuffer`, multi-core partitioned.
6. **Integration + docs:** wire into Main/Makefile/inf, update README's benchmark
   table and design notes, validate full run end-to-end on the 5800X.

---

## 8. Live progress display during long benchmarks ✅

Because the long CPU and memory benchmarks run for 2–5 minutes each, the screen must
show visible activity so users are reassured the system has not crashed or stalled.

### 8.1 What to display

Each long benchmark should push a **live progress update to the screen** at least once
per second while running. The update must be visible even when the benchmark is executing
on APs (the BSP is free to render while APs are blocked in `StartupAllAPs`).

Minimum viable content per update:

| Field | Example |
|-------|---------|
| Benchmark name | `FP Vector (AVX2/FMA)` |
| Elapsed time | `00:01:42 / 03:00` (elapsed / budget) |
| Progress bar | `[################........]  57%` |
| Current throughput | `1 284 GFLOP/s` (rolling, refreshed each chunk) |
| Core count / mode | `Multi-core (7 APs)` |

For the **integrity test**, also show:
- Pass currently running (e.g. `Pattern 2/4: 0xFF`)
- Bytes verified so far
- Error count (highlight in red if non-zero)

### 8.2 Implementation approach

The cleanest approach that keeps benchmark logic and rendering separate:

- Add a **`ProgressCallback`** mechanism to `IBenchmark` (or pass it via a new
  `RunContext` struct given to `Setup()`):
  ```cpp
  using ProgressFn = void(*)(UINT64 elapsedUs, UINT64 budgetUs,
                              UINT64 score, const char* unit, void* ctx);
  virtual void SetProgressCallback(ProgressFn fn, void* ctx) {}
  ```
- Each long benchmark calls the callback at the end of every TimeBox chunk (or every
  N chunks to avoid rendering overhead — once per second is sufficient).
- `BenchmarkRunner::RunSingle` passes a renderer callback when dispatching long
  benchmarks (`GetDurationClass() == Long`). For multi-core runs the callback is set
  before `StartupAllAPs`; APs call it through the shared benchmark pointer.
  **Only one AP should call the renderer at a time** — use a lightweight atomic flag
  (`volatile UINT32 mRenderLock`) so at most one AP renders per tick.
- The renderer callback calls `Renderer::Clear()` / `Renderer::DrawText()` /
  `Renderer::Present()` — these are BSP-safe from within an AP as long as GOP
  framebuffer writes are used (no Boot Services calls from APs).

Alternative (simpler, no callback plumbing): have `BenchmarkRunner` poll a shared
counter exposed by the benchmark (`virtual UINT64 GetLiveScore() const`) from the BSP
while `StartupAllAPs` is running asynchronously (non-blocking mode with a wait event).
However this requires changing from blocking to event-driven AP dispatch, which is
significantly more invasive.

**Recommendation:** use the callback approach. It is self-contained per benchmark,
does not require changing the dispatch model, and each benchmark controls its own
update cadence.

### 8.3 Progress bar maths

```
pct  = (elapsedUs * 100) / budgetUs          // 0–100
width = 32  (bar characters)
filled = pct * width / 100
```

Elapsed time display in `MM:SS / MM:SS` format requires a simple integer division
helper (`UintToMinSec`) — no floating point.

### 8.4 Caveats

- **GOP thread safety:** GOP `Blt()` is not guaranteed re-entrant. Calls from APs
  should be serialised with the same atomic lock used in §8.2.
- **Render overhead:** a full `Clear` + redraw per second is cheap (<1 ms) and
  negligible relative to a 3-minute budget.
- **Short benchmarks:** do not add progress callbacks to the five original short
  benchmarks. The existing `DrawProgress` screen in `BenchmarkRunner` is sufficient
  for sub-second runs.

---

## 9. Open decisions for the user

**Resolved (2026-06-13):**

- ✅ **Keep the original 5 short benchmarks** alongside the new suites (Short/Long
  grouping in §1.5a stands).
- ✅ **§10 resolution:** default to **1024×768**; do **not** force a prompt at boot —
  expose a **"Change resolution" option in the main menu** instead (§10.3).
- ✅ **§11 stress duration/faults:** **30-minute default** per stress test,
  user-overridable; **do not stop on the first fault** — catch it, report it to the
  screen, and keep a running **total fault count** (§11.2/§11.3).
- ✅ **§12/§10 persistence:** **in-memory only** for v1 (reset each boot). Follow-up
  task recorded for **EFI NVRAM persistence** of theme + resolution (§12.6).

**Resolved (2026-06-13, went with recommendations):**

- ✅ **CPU total: 9 tests** — **add the Cache Latency Ladder** (§2 #9) for L1/L2/L3/DRAM
  ns coverage, and **expose per-benchmark time budgets** in the settings menu (alongside
  the §11 stress duration).
- ✅ **Memory suite: keep all 5** (read/write/copy/latency/integrity, as built).
- ✅ **Write bandwidth: non-temporal only** — no separate temporal variant (the §11
  Stress suite already covers cache-inclusive DRAM hammering).
- ✅ **NEW — core selection:** let the user choose **which physical cores** multi-core
  tests run on (§14).

---

## 10. Display resolution selection + font scaling ✅

### 10.1 Goal

**Default to 1024×768.** Boot straight into the UI at that resolution without
prompting; expose a **"Change resolution" item in the main menu** so the user can
switch on demand. The preference order **1024×768 → 1920×1080 → 800×600** is only the
*auto-pick fallback* used when 1024×768 is unavailable. Scale the bitmap font so the
UI is legible at each. Today `Renderer::Init` (`Source/Renderer.cpp:50`) takes a
single `preferred{Width,Height}` and selects the GOP mode with the smallest distance
to it — there is no priority list, no chooser, and the font is locked at 8×16.

> **Decision (2026-06-13):** default 1024×768; no boot-time prompt; resolution is
> changed from a main-menu option, not forced at startup.

### 10.2 Resolution enumeration + selection

- Replace the single-target search in `Init` with a **mode table** pass: enumerate
  all GOP modes (already iterating `0..MaxMode`, filtering to the two accepted 32-bit
  pixel formats at `Renderer.cpp:77`), and record `{modeIndex, w, h, format}` into a
  small fixed array (cap ~64).
- Define the preference list as data: `{1920×1080, 1024×768, 800×600}`. Selection
  rule: pick the **first preference that exactly matches** an available mode. If none
  of the three exact targets exist, fall back to the current closest-distance logic so
  we always land *somewhere* valid.
- De-duplicate by resolution (same WxH offered by multiple pixel formats → one entry,
  prefer BGR which is the common UEFI default per `sIsBGR`).

### 10.3 User chooser

- New `Renderer::ListModes(ModeDesc* out, UINT32 cap)` returning the de-duplicated
  acceptable modes, and `Renderer::SetModeByIndex(...)` wrapping `SetMode` + the
  existing back-buffer (re)allocation block (`Renderer.cpp:104–125`).
- **No boot-time prompt.** At startup, auto-select 1024×768 (or the fallback order if
  absent) and go straight into the TUI. Instead add a **"Change resolution" entry to
  the main menu**: when chosen, draw an arrow-key list of `ListModes` results (reuse
  the selection-list drawing style from `Tui.cpp`), highlighting the current mode;
  Enter applies it live (re-init + re-render), Esc cancels. If only one acceptable
  mode exists, the menu item can be hidden or shown disabled.
- Re-allocating the back-buffer on mode change: free the old `sFramebuffer`
  (`AllocatePool`/`FreePool`) and re-run the allocation at `Renderer.cpp:112`. Init
  must be safe to call repeatedly.

### 10.4 Font scaling (the 8×16 font is fixed today)

The font is a single fixed 8×16 bitmap (`Include/BitmapFont.h`,
`CHAR_WIDTH=8`/`CHAR_HEIGHT=16`) blitted 1:1 in `DrawText` (`Renderer.cpp:148`). To
keep glyphs legible at 1080p (and not microscopic), add an **integer scale factor**:

- Add `static UINT32 sFontScale` to the renderer and an effective glyph size
  `EffW = CHAR_WIDTH*sFontScale`, `EffH = CHAR_HEIGHT*sFontScale`.
- `Columns()`/`Rows()` (`Renderer.h:20–21`, currently `ScreenWidth()/8`,
  `ScreenHeight()/16`) divide by `EffW`/`EffH` instead, so the **character grid the
  whole TUI is written against stays the same API** — no Tui.cpp coordinate changes
  needed; the grid just gets coarser/finer with scale.
- In `DrawText`/`DrawTextBg`/`FillRow` replace the `* CHAR_WIDTH` / `* CHAR_HEIGHT`
  pixel maths with `EffW`/`EffH`, and in the glyph inner loop emit each set bit as a
  `sFontScale × sFontScale` block (nested loop) instead of a single pixel.
- **Scale policy by chosen resolution:** 800×600 → ×1, 1024×768 → ×1 (or ×2 if the
  grid stays wide enough — 1024/8=128 cols at ×1; the TUI assumes 100 cols so ×1 is
  safe, ×2 gives 64 cols which is too narrow → keep ×1), 1920×1080 → ×2
  (1920/16=120 cols, 1080/32=33 rows — comfortably ≥100 cols the layout expects).
  Encode this as a `(resolution → scale)` lookup chosen at `SetMode` time.
- **Verify the 100-col assumption holds** at the chosen scale: the results grid and
  headers in `Tui.cpp` assume ~100 columns. Compute `Columns()` after scaling and, if
  it drops below the layout minimum, step the scale down. A single guard in Init.

### 10.5 Risks / notes

- Back-buffer size tracks `sPitch*sHeight`; re-allocate on every mode change or the
  buffer under/over-runs. The `Present()` `memcpy` uses the same dims — keep them in
  lockstep.
- Pixel-format can differ per mode; re-read `sIsBGR` after each `SetMode`
  (`Renderer.cpp:110`).
- Text-mode fallback (no GOP) ignores all of this — unchanged.

---

## 11. Third category: Stress tests (overclock validation) + fault containment ⬜

### 11.1 Goal

Add a **third category `"Stress"`** alongside `"CPU"`/`"Memory"` (`GetCategory()` is a
free-form string, `IBenchmark.h:35`, so the category itself is trivial). The suite is
aimed at **overclock validation**, split into:

- **Memory OC stress:** hammer DRAM at sustained bandwidth (clock-speed stress) and
  with worst-case access patterns (latency / signal-integrity stress) over the whole
  `BigBuffer` footprint, continuously verifying written data — surfacing instability
  from too-aggressive memory clocks / sub-timings / insufficient voltage.
- **CPU OC stress:** sustained max-power kernels (AVX2/FMA heat soak, mixed
  ALU+FP+branch) plus result-verification kernels (compute a value with a known answer
  in a long loop; any deviation = instability) to expose marginal core clock / Vcore.

**Critical requirement:** *any exception generated during these tests must be caught
and reported, not crash the app.* On real overclock failures the CPU will throw
`#UD`/`#GP`/`#PF`/`#MC`/`#DE` etc.; uncaught, those triple-fault or hang the firmware.

### 11.2 Category wiring

- Reuse the existing category string mechanism — return `"Stress"` from
  `GetCategory()`. Confirm everywhere category is switched on (Tui grouping/coloring,
  `BenchmarkRunner`) handles an unknown third value gracefully (it's just a string
  compared/printed). Add `"Stress"` to any place that special-cases `"CPU"`/`"Memory"`.
- Stress tests are `DurationClass::Long` and should get their own UI grouping. With
  §1.5a's Short/Long grouping plus the CPU/Memory/Stress category tag, the selection
  list reads e.g. `[Stress] Memory Clock Soak`. Consider a "Run all stress" action
  mirroring "Run all long".
- Stress tests are **open-ended**: drive them by a user-set duration (**default 30
  minutes**, user-overridable from the settings menu; TimeBox §1.1 already supports a
  wall-clock budget) rather than a fixed work count. 30 min is a meaningful OC-stability
  soak; let the user dial it up (e.g. overnight) or down.

> **Decision (2026-06-13):** 30-minute default per stress test, user-overridable. On a
> detected fault, **do not stop** — catch it, render it to the screen immediately, and
> keep a running **total fault count**; the test runs to the end of its budget. See
> §11.3.

### 11.3 Fault containment — the hard part (no C++ EH on bare metal)

There is no C++ exception machinery in this freestanding environment, so `try/catch`
is unavailable. To "catch" a CPU fault we must install **CPU exception handlers**:

- **Preferred:** locate `EFI_CPU_ARCH_PROTOCOL` (`gEfiCpuArchProtocolGuid`) and use
  `RegisterInterruptHandler` to hook the relevant exception vectors (#DE 0, #UD 6,
  #GP 13, #PF 14, #MC 18, etc.). The handler records the vector + faulting context into
  a global `volatile StressFault {bool occurred; UINT64 vector, ripGuess, errCode;}`,
  then **recovers** rather than returning into the faulting instruction.
- **Fallback (if that protocol is absent):** install our own IDT entries via `sidt`/
  `lidt`. More invasive; gate behind a capability check and document it as phase 2.
- **Recovery strategy:** the cleanest non-crashing recovery is a `setjmp`/`longjmp`
  style escape — establish a recovery point at the top of each stress **work chunk**,
  and have the exception handler restore that context (custom `SaveContext`/
  `RestoreContext` in asm, since no libc). On fault: increment a global
  `volatile UINT64 mFaultCount`, record the latest `{vector, errCode}`, `longjmp` back
  to the chunk boundary, **render the fault to the screen immediately** (vector name +
  running total), and **continue the test** to the end of its 30-min budget rather than
  aborting. The headline result is the **total fault count** (0 = stable). This
  continue-and-tally loop is the bare-metal equivalent of `try/catch` inside a retry
  loop and is the core piece of work for this feature.
  - Guard against a fault storm: if the *same* instruction refaults immediately, skip
    forward (advance the work pointer / re-seed inputs) so we make progress instead of
    livelocking on one bad address; still counted.
- **AP faults:** if a stress kernel runs multi-core, an AP can fault. Exception
  handlers must be installed **on every AP** too, and AP recovery must unwind that AP's
  worker cleanly (per-AP recovery context, not a shared one) so one bad core doesn't
  wedge `StartupAllAPs`. The runner aggregates "core N faulted with #GP" into the
  report. This is the trickiest sub-case — validate single-core fault containment
  first, then extend to APs.
- **#MC (machine check):** may be unrecoverable depending on the error; catch and
  report it but document that some MCEs will still reset the box (firmware-dependent).

### 11.4 Stress benchmark catalog (proposed)

| Category | Name | Stresses | Method |
|----------|------|----------|--------|
| Stress (mem) | Memory Clock Soak | DRAM data-clock stability | Sustained streaming write+read over full `BigBuffer` with inline verify of an address-encoded pattern; any mismatch = instability, reported with address |
| Stress (mem) | Memory Latency Soak | Command/timing margins | Randomized pointer-chase + read-modify-write across full footprint (worst-case row activations), verifying RMW results |
| Stress (cpu) | CPU Power Virus | Vcore / thermal / clock under max draw | AVX2+FMA dense kernel (reuse §2 #4 path) run continuously for the budget; heat soak |
| Stress (cpu) | CPU Compute Verify | Marginal-clock logic errors | Long deterministic compute (e.g. iterated hash / matrix mul) with a precomputed golden result; **any** deviation = instability |

- Memory stress reuses `BigBuffer` (§1.5) and the partitioning logic; CPU stress
  reuses the AVX-enable path (§1.4) and CpuFeatures.
- Headline result for every stress test = **fault/error count** (0 = stable). Surface
  it prominently like the integrity test (§3 #5) — reuse the `ErrorCount` field
  proposed in §6.

### 11.5 Files

- `Include/CpuExceptions.h` + `Source/CpuExceptions.cpp` — handler install/teardown,
  the `StressFault` record, and `SaveContext`/`RestoreContext` (small asm).
- `Source/Benchmarks/StressMemClock.*`, `StressMemLatency.*`, `StressCpuPower.*`,
  `StressCpuVerify.*` (mirroring existing benchmark layout; wire into Main/Makefile/inf
  per §6).

---

## 12. Colour schemes: dark / light / high-contrast dark ✅

### 12.1 Goal

Ship three palettes — **Dark (default)**, **Light**, **High-contrast dark** — and let
the user pick. Today `Theme::*` are compile-time `constexpr Color` constants in a
namespace (`Include/ColorTheme.h`), referenced directly as `Theme::Background` etc.
across `Main.cpp`, `BenchmarkRunner.cpp`, and `Tui.cpp`. Runtime selection means those
constants must become a **selectable palette**.

### 12.2 Refactor constants → runtime palette

- Define a `struct Palette { Color Background, HeaderBorder, HeaderText, Text,
  TextDim, Highlight, HighlightTxt, Accent, Success, Warning, Error, Separator,
  CheckMark, Footer; };` (the existing field set).
- Provide three `constexpr Palette` instances: `kDark` (the current values verbatim),
  `kLight`, `kHighContrastDark`.
- Add `const Palette& Theme::Current()` plus `Theme::Set(ThemeId)` backed by a single
  global pointer/index (default `kDark`).
- **Migration of call sites:** every `Theme::Background` becomes
  `Theme::Current().Background`. There are ~30 references (grep `Theme::`). Two options:
  - (a) mechanical edit of all references (clear, explicit), or
  - (b) keep the `Theme::Foo` spelling working by replacing each `constexpr Color Foo`
    with an `inline Color Foo()` accessor or a macro — less churn but changes them to
    function calls. **Recommend (a)** for clarity; it's a contained find/replace.
- Keep the `Color`/`Pack`/`ToPixel` path unchanged — only where the values come from
  changes.

### 12.3 Palette definitions (proposed)

- **Dark (default):** current values (`ColorTheme.h:27–40`) unchanged.
- **Light:** near-white `Background` (e.g. 245/245/245), dark `Text` (~30/30/30),
  retain accent hue but darken for contrast on white; `Highlight` a light blue fill
  with dark text. Verify every fg/bg pair stays readable (the TUI relies on
  `DrawTextBg` for selected rows).
- **High-contrast dark:** pure black `Background` (0/0/0), pure white `Text`
  (255/255/255), saturated `Accent`/`Success`/`Warning`/`Error` at full intensity,
  bright yellow `Highlight`/selection — maximal luminance separation for accessibility.

### 12.4 Chooser + interaction

- Offer theme selection on the same pre-TUI settings step as the resolution chooser
  (§10.3), and/or a hotkey in the main menu (e.g. `T` cycles themes live — trivial once
  `Theme::Set` exists, just re-render). Default = Dark with no prompt required.
- Live re-render: after `Theme::Set`, the next `Renderer::Clear`/draw/`Present` cycle
  picks up new colours automatically since everything reads `Theme::Current()`.

### 12.5 Notes

- `Renderer::Clear()` with no arg currently clears to `Theme::Background`
  (`Renderer.cpp:136`) — make it read `Theme::Current().Background`.

> **Decision (2026-06-13):** v1 keeps theme **and** resolution choices **in memory
> only** — they reset to defaults (Dark / 1024×768) each boot. NVRAM persistence is a
> deferred follow-up (§12.6).

### 12.6 Follow-up task — NVRAM persistence (deferred, not v1) ⬜

Persist the user's chosen **theme** and **resolution** (and the §11 stress duration /
§2 per-benchmark budgets if those settings land) across reboots via an EFI NVRAM
variable.

- Use `gRT->SetVariable` / `gRT->GetVariable` with a vendor GUID and a single packed
  `Settings` struct (themeId, resolution WxH or mode index, fontScale, durations).
  Attributes: `NON_VOLATILE | BOOTSERVICE_ACCESS | RUNTIME_ACCESS`.
- Load once at startup (before `Renderer::Init`, so the saved resolution applies on
  first paint); fall back to defaults if the variable is absent or fails a version/size
  check. Save when the user changes a setting from the menu.
- **Caveats:** the variable store is firmware-owned and finite — keep the struct tiny
  and version-tagged; tolerate read failures gracefully (some firmware restricts NVRAM
  writes). Resolution saved as WxH (not raw mode index) since mode indices aren't
  stable across firmware/GPUs — re-resolve to a mode at boot.

---

## 13. More readable font ✅

### 13.1 Diagnosis

The current font (`Source/BitmapFont.cpp`, `sFontData[CHAR_COUNT*CHAR_HEIGHT]`) is a
hand-rolled 8×16 bitmap whose glyphs only occupy a thin ~6px-wide / ~7-row-tall
sub-region of each 8×16 cell (e.g. the digits/letters use values like `0x1C/0x36/0x26`
— narrow, single-pixel strokes with lots of empty rows top and bottom). At native res
this reads as **spindly and faint**; when block-scaled ×2 for 1080p (§10.4) those
1-pixel strokes become isolated 2-pixel blocks with hard jaggies, which is the
"ugly/blurry" look. It is not literal blur (we never bilinear-filter — `DrawText`
blits discrete pixels), it's a **low-quality, under-filled glyph set**.

### 13.2 Fix — replace the glyph set (highest impact)

- Swap `sFontData` for a **proven, well-formed 8×16 font** that fills the cell with
  consistent ~2px-equivalent stroke weight and proper x-height/baseline — e.g. the
  classic IBM VGA 8×16 console font (public-domain bitmaps are widely available) or a
  clean open bitmap font. This is a **drop-in data replacement**: same
  `CHAR_COUNT*CHAR_HEIGHT` byte layout, same `FIRST_CHAR=32..LAST_CHAR=126` range, so
  `DrawText`/`DrawChar` need **no code change**. Pure data swap = lowest risk, biggest
  readability win.
- Keep the glyph table as a generated `.cpp` (document the source/license of whatever
  font is used in a header comment, since the repo already tracks a LICENCE).

### 13.3 Keep scaling crisp, never blurry

- Confirm scaling stays **nearest-neighbour / integer block** (the §10.4 approach:
  emit each set bit as a solid `scale×scale` block). Never introduce any averaging/
  bilinear path — that's what would make it genuinely blurry. Integer scaling of a
  well-formed glyph stays sharp.
- Prefer a **bigger native glyph over scaling** where the panel allows: an optional
  **8×16 → dedicated higher-res font** (e.g. a 16×32 glyph set) gives much crisper
  large text at 1080p than ×2-scaling an 8×16 cell. Heavier (new data + a width/height
  field instead of the hard-coded 8/16), so treat as a phase-2 upgrade; the §13.2 data
  swap + §10.4 integer scaling is the v1 fix.

### 13.4 Optional legibility extras (cheap, in `DrawText`)

- **Faux-bold** for headers: OR each glyph row with itself shifted right 1px when a
  `bold` flag is set — sharpens headers without a second font.
- Ensure strong fg/bg contrast via the §12 palettes (the readability win compounds
  with high-contrast mode).

### 13.5 Files

- `Source/BitmapFont.cpp` — replace `sFontData` (and only that). If going to a larger
  native glyph (§13.3 phase 2): generalise `CHAR_WIDTH/HEIGHT` in `Include/BitmapFont.h`
  and the `* CHAR_WIDTH`/`* CHAR_HEIGHT` maths in `Renderer.cpp` to read the font's own
  dimensions.

---

## 14. User-selectable cores for multi-core tests ✅

### 14.1 Goal

Let the user pick **exactly which processors** a multi-core test runs on (e.g. "cores
0–3 only", "even cores", "one core per CCX") instead of always using every enabled AP.
Useful for isolating a weak/unstable core during the §11 stress soak, comparing CCX
scaling, or pinning work away from a hot core.

### 14.2 How dispatch works today (and why it's all-or-nothing)

`BenchmarkRunner::RunSingle` (`Source/BenchmarkRunner.cpp:189–225`) computes
`apCount = enabledProcessors - 1` and dispatches with **`StartupAllAPs`** — that runs
the kernel on *every* enabled AP, no per-core control. `RunCore(workerIndex,
totalWorkers)` partitions work by those two numbers. To select cores we must move off
`StartupAllAPs`.

### 14.3 Selection model

- Add a **core-selection set** (bitmask over processor indices, cap 256 → `UINT64[4]`
  or a small `Vector<UINT32>` of selected indices) held in the runner/settings.
- Enumerate processors via the MP Services protocol already wrapped by
  `SystemInfo::GetMpServices()`: use `GetNumberOfProcessors` and `GetProcessorInfo` to
  list each AP's index, and its **location** (`EFI_CPU_PHYSICAL_LOCATION`:
  package/core/thread) so the chooser can show meaningful labels (e.g.
  `P0 C3 T0`) and offer presets (all / physical-cores-only / per-CCX-one).
- The **BSP (processor 0)** is special — it's the one running the UI and can't be an AP
  worker in the blocking model. Decide per the existing code: today work runs on APs
  only (BSP renders). Keep that; the selection covers **AP indices**. (Optionally allow
  "include BSP" by also calling `Run()` on the BSP after kicking the APs, but that
  complicates timing — defer.)

### 14.4 Dispatch change: per-AP startup instead of StartupAllAPs

- Replace the single `StartupAllAPs` call with a loop over the **selected** AP indices
  using **`StartupThisAP`** in **non-blocking** mode (pass a per-AP completion event),
  then `WaitForEvent` on all of them — so the selected cores run concurrently and we
  still get one wall-clock span. (Blocking `StartupThisAP` would serialise them, which
  defeats a parallel benchmark — must use the event form.)
- `TotalWorkers` becomes the **count of selected cores**, and each selected AP gets a
  dense `workerIndex` 0..N-1 (map selected-physical-index → dense worker index) so the
  existing `RunCore` partitioning (and the §1.3 atomic accumulation) is unchanged.
- Fallback: if MP Services is absent or 0 cores selected, run single-core on the BSP
  (mirrors the existing `apCount == 0 → multiCore=false` path at
  `BenchmarkRunner.cpp:194`).
- `result.CoreCount` = number of selected cores (already surfaced in the live progress
  "Multi-core (N APs)" line, §8 — it will now reflect the selection).

### 14.5 UI

- Add a **"Select cores" main-menu item** (same settings area as resolution/theme/
  budgets). Draw the processor list from §14.3 with a checkbox per core (Space toggles,
  arrow keys move), plus quick presets (All, Physical only, Per-CCX). Default = **all
  APs selected** (current behaviour, so nothing changes unless the user opts in).
- Persist with the rest of the settings: in-memory for v1, NVRAM follow-up (§12.6) —
  store as a bitmask.

### 14.6 Caveats

- **Disabled/unavailable APs:** `StartupThisAP` fails for a processor that's disabled or
  in a bad state — skip and report it, don't abort the run.
- **Hyperthreads:** the package/core/thread location lets the chooser distinguish SMT
  siblings; a "physical cores only" preset picks one thread per core (good for clean
  per-core OC stress).
- **Stress + core selection (§11):** this is the natural pairing — run the CPU stress
  kernel on a single suspect core to confirm it's the unstable one. The §11 exception
  handlers must be installed on whichever cores are selected.
- **AP exception handlers (§11.3):** install/teardown handlers only on the selected
  APs, matching the dispatch set.

---

## 15. Core-cycle mode (third run mode for multi-core benchmarks) ✅

### 15.1 Goal

Every benchmark that currently supports single-core and multi-core modes gains a **third
option: core-cycle mode**. In this mode the test kernel is run **N times on each selected
core in sequence** (one core at a time), producing a per-core score table. This reveals
inter-core variance that the aggregate multi-core score hides — essential for:

- Identifying a **weak or thermally-throttled core** during overclock validation (§11).
- Confirming that every physical core boosts to the same clock bin.
- Isolating a marginal core during §11 CPU stress without guessing which one it is.
- Producing a **per-core performance fingerprint** for the results display.

### 15.2 Run model

```
for each selected core C in order:
    for i = 1..N:
        dispatch kernel to C alone (blocking StartupThisAP)
        record per-run score
    emit per-core row: C → min/avg/max score over N runs
```

- Sequential dispatch means **only one core is active at a time** — this is deliberate.
  It isolates each core's contribution and avoids inter-core noise (cache sharing, power
  competition, thermal crosstalk).
- N is a user-configurable repeat count (see §15.4). Default **N = 3** balances coverage
  vs. total time; a higher N gives a tighter variance estimate.
- The BSP remains the UI/coordinator — it drives the loop and renders progress between
  each core's runs (exactly one `StartupThisAP` call at a time, blocking mode, so no
  event plumbing needed beyond what §14.4 already defines).

### 15.3 Data model changes

- Add `CoreCycle` to a new `RunMode` enum (or extend the existing single/multi choice
  already implied by `multiCore` bool in `BenchmarkRunner.cpp:194`):
  ```cpp
  enum class RunMode { SingleCore, MultiCore, CoreCycle };
  ```
- `BenchmarkResult` gains a `PerCoreScores` array (cap 256, matching the §14.3 bitmask
  cap): `UINT64 PerCoreScore[256]; UINT32 PerCoreSampleCount;`. Populate `PerCoreScore[i]`
  with the **average** of the N runs for core i; also record `PerCoreMin[i]` /
  `PerCoreMax[i]` for the results table. Only entries for actually-cycled cores are valid
  (guard with the selected-core bitmask).
- The aggregate `Score` in `BenchmarkResult` is set to the **median** across all cycled
  cores (robust to one outlier core), with `Unit` unchanged from the benchmark's normal
  unit. This lets the summary row show a meaningful single number alongside the per-core
  breakdown.

### 15.4 Settings / UI

- **N (repeats per core):** add to the settings menu alongside the per-benchmark time
  budgets (§9 open decision on budget exposure). Range 1–10; default 3. For time-boxed
  benchmarks (§1.1) each repeat runs for the full TimeBox budget, so total time =
  `selected_cores × N × budget_per_bench` — display this estimate in the settings menu
  before the user kicks off a run (e.g. "8 cores × 3 runs × 3 min = ~72 min").
- **Mode selector:** expose the three modes (Single / Multi / Core-cycle) per-benchmark
  or globally. A global default (e.g. Multi) with a per-run override is simplest for v1.
  Add a mode indicator to the selection list next to the `[CPU]`/`[Memory]` tag and
  duration marker, e.g. `[CC]` for core-cycle.
- **Results display:** when `RunMode == CoreCycle`, the results table expands to show one
  row per core below the aggregate row:
  ```
  FP Vector (AVX2/FMA)  [CPU]  CC    avg:1284 GFLOP/s
    Core 0 (P0 C0 T0)         min:1281  avg:1284  max:1286
    Core 1 (P0 C1 T0)         min:1279  avg:1282  max:1283
    ...
    Core 7 (P0 C7 T0)         min: 843  avg: 846  max: 849  ← outlier
  ```
  Highlight any core whose average deviates >5% from the median in `Theme::Warning`
  colour — the outlier core pops immediately without the user doing mental arithmetic.
  The deviation threshold (5%) should be a named constant, easy to tune.

### 15.5 Live progress during core-cycle runs

Extend the §8 progress display to show the current phase:

| Field | Example |
|-------|---------|
| Mode | `Core-cycle (3 runs × 8 cores)` |
| Current core | `Core 3 of 8 — P0 C3 T0` |
| Current repeat | `Run 2 of 3` |
| Overall progress | `[#########...........] 42%` (cores done / total cores) |
| Per-core score so far | `avg 1282 GFLOP/s` (rolling over completed runs for this core) |

The outer loop (BSP drives the core-cycle) calls `Renderer::Present()` between each
core's N runs at minimum, and the inner TimeBox callback (§8.2) handles within-run
updates exactly as in the normal long-benchmark case.

### 15.6 Interaction with §11 (stress) and §14 (core selection)

- **Core-cycle + stress:** the natural combination for OC fault isolation. Run a 30-min
  stress kernel per-core in sequence; fault counts are reported **per core**, making it
  trivial to identify the bad one. The §11.3 exception handler is installed fresh for
  each core's stint and torn down between (reducing the risk of handler state
  accumulating across cores).
- **Core-cycle + §14 core selection:** offer the user an explicit **core scope** option
  when launching a core-cycle run:
  - **"Selected cores only"** — iterate over exactly the §14 selection in index order.
    Useful when the user has already narrowed to a suspect set (e.g. a known-bad core
    they want to isolate, or a single CCX).
  - **"All cores"** — ignore the §14 selection and cycle over every enabled AP regardless.
    Useful for a full-chip per-core fingerprint without having to reset the selection first.
  - Default: **"All cores"**, so a naive core-cycle run always covers the whole chip and
    the §14 selection remains a deliberate override rather than a silent filter.
  - Present the choice as a toggle/option in the same pre-run picker that asks for N
    repeats (§15.4). Display the resolved core list (e.g. "8 cores: P0C0..P0C7") so
    the user can confirm before starting.
- **Memory benchmarks:** core-cycle mode is less useful for memory bandwidth tests (the
  interesting figure is aggregate BW across all cores hitting the DRAM controllers), but
  it still makes sense for `Random Access Latency` (§3 #4) and the §11 memory stress
  tests where per-core fault isolation matters. Allow it universally; individual
  benchmarks can document when it gives uninformative results.

### 15.7 Files

- `Include/IBenchmark.h` — add `CoreCycle` to `RunMode` enum.
- `Include/BenchmarkResult.h` — add `PerCoreScore[256]`, `PerCoreMin[256]`,
  `PerCoreMax[256]`, `PerCoreSampleCount` fields; add `RunMode RunModeUsed`.
- `Source/BenchmarkRunner.cpp` — new `RunCoreCycle(bench, selectedCores, N)` method
  that drives the sequential per-core loop and fills `PerCoreScores`; called from
  `RunSingle` when mode is `CoreCycle`. Reuses the existing `StartupThisAP` (blocking
  form) already available from §14.4.
- `Source/Tui.cpp` — results table: detect `RunModeUsed == CoreCycle` and emit per-core
  sub-rows with outlier highlighting; settings menu: N-repeats field and mode selector.

---

# 16. AI Readiness Benchmark Suite ⬜

## 16.1 Goal

Add a new benchmark category:

```cpp
GetCategory() -> "AI"
```

and a new benchmark suite that estimates a system's suitability for:

* Local LLM inference
* Ollama workloads
* llama.cpp workloads
* LM Studio workloads
* Quantized transformer models (4-bit and 8-bit)

The benchmark should:

1. Run entirely locally.
2. Not require downloading a model.
3. Produce repeatable scores.
4. Scale across future CPUs.
5. Correlate with real-world tokens/sec.

Unlike synthetic CPU benchmarks, the AI score should approximate the workload characteristics of modern transformer inference.

---

## 16.2 Benchmark Structure

The AI Readiness benchmark consists of four sub-tests:

| Test                   | Weight |
| ---------------------- | ------ |
| INT8 Matrix Throughput | 35%    |
| INT4 Matrix Throughput | 25%    |
| Memory Bandwidth       | 25%    |
| Cache Behaviour        | 15%    |

Total runtime:

```text
4 x 90 seconds = 6 minutes
```

or configurable.

These are reported individually and combined into a single AI Readiness Score.

---

## 16.3 Test 1 – INT8 Matrix Throughput

### Purpose

Measure low-precision neural-network compute.

### Kernel

Perform repeated GEMM operations:

```text
C = A × B
```

using:

```text
INT8 x INT8 → INT32 accumulate
```

Matrix sizes:

```text
1024 × 1024
2048 × 2048
4096 × 4096
```

rotated between chunks.

### ISA

Use:

```text
AVX2
AVX512-VNNI
AMX (if available)
```

with runtime dispatch.

### Score

Report:

```text
INT8 GOPS
```

(integer operations per second)

Example:

```text
45,000 GOPS
```

---

## 16.4 Test 2 – INT4 Matrix Throughput

### Purpose

Approximate modern quantized models.

Most local models now use:

```text
Q4_K_M
Q4_0
IQ4_XS
```

which effectively operate on 4-bit weights.

### Kernel

Packed 4-bit matrices:

```text
2 values per byte
```

Perform:

```text
INT4 × INT4 → INT32
```

multiply-accumulate.

### Score

Report:

```text
INT4 GOPS
```

This score is often more predictive of real LLM performance than FP32 or FP64.

---

## 16.5 Test 3 – AI Memory Bandwidth

### Purpose

Measure ability to stream model weights.

For LLM inference:

```text
weight read speed
```

is frequently the bottleneck.

### Method

Re-use the BigBuffer infrastructure.

Perform:

```text
Sequential read
Random block read
Mixed stride read
```

across the entire footprint.

Block sizes:

```text
64 bytes
256 bytes
1024 bytes
4096 bytes
```

### Score

Report:

```text
GB/s
```

using harmonic averaging of all patterns.

---

## 16.6 Test 4 – Cache Behaviour

### Purpose

Measure how effectively a CPU keeps model weights close to execution units.

### Method

Pointer-chase and matrix access patterns using working sets:

```text
512 KB
2 MB
8 MB
32 MB
64 MB
```

Measure:

```text
L2 hit latency
L3 hit latency
Miss penalty
```

### Score

Produce:

```text
Cache Efficiency Score
```

scaled 0–1000.

Large-cache CPUs such as X3D parts benefit naturally.

---

# 16.7 Score Normalisation

Raw benchmark numbers are not directly comparable.

Instead each sub-test is converted into a normalised score.

Define reference hardware:

```text
AMD Ryzen 7 5800X
64 GB DDR4-3200
```

This becomes:

```text
1000 points
```

per sub-test.

Formula:

```text
NormalisedScore =
(RawResult / ReferenceResult) × 1000
```

Examples:

```text
5800X = 1000
7800X3D = 1450
9950X = 2200
```

This makes scores stable across versions.

The reference values should be hardcoded and versioned:

```cpp
AI_SCORE_VERSION = 1
```

so future changes do not invalidate old results.

---

# 16.8 Final AI Readiness Score

Weighted formula:

```text
AI Score =
INT8 × 0.35 +
INT4 × 0.25 +
Memory × 0.25 +
Cache × 0.15
```

Result rounded to integer.

Examples:

| CPU       | AI Score |
| --------- | -------- |
| 5800X     | 1000     |
| 7800X3D   | 1450     |
| 9950X     | 2400     |
| EPYC 9654 | 3200     |

---

# 16.9 Estimated LLM Performance

Convert AI Score into a predicted local LLM capability.

Using calibration data gathered during development:

```text
7B Q4 model
14B Q4 model
32B Q4 model
```

Store lookup tables.

Example output:

```text
AI Readiness Score: 1840

Estimated LLM Performance

7B Q4:
~24 tokens/sec

14B Q4:
~13 tokens/sec

32B Q4:
~5 tokens/sec
```

This is the figure users actually care about.

---

# 16.10 Results Display

Add a dedicated AI section:

```text
AI READINESS

INT8 Compute      1620
INT4 Compute      1810
Memory            1750
Cache             1430

AI Readiness      1668
```

and:

```text
Estimated LLM Performance

7B Q4      22 tok/s
14B Q4     11 tok/s
32B Q4      4 tok/s
```

---

# 16.11 Versioning Requirement

To keep scores comparable forever:

```cpp
static constexpr UINT32 AI_SCORE_VERSION = 1;
```

Any change to:

* matrix sizes
* weighting
* cache methodology
* reference CPU

must increment:

```cpp
AI_SCORE_VERSION
```

and results should display:

```text
AI Readiness Score v1
```

This prevents historical results becoming meaningless after benchmark updates.

---

Yes. Given how the benchmark suite has evolved, I would actually go further than "Run All Tests in a Category" and make categories a first-class concept in the framework.

The current plan already has:

* CPU
* Memory
* AI
* Stress

and potentially more in the future:

* Storage
* GPU
* Network
* Power Efficiency

A category runner becomes extremely valuable.

---

# 17. Run All Tests In A Category ⬜

## 17.1 Goal

Allow users to execute every benchmark belonging to a specific category without manually selecting each benchmark.

Examples:

```text
Run All CPU Tests
Run All Memory Tests
Run All AI Tests
Run All Stress Tests
```

This is particularly important for:

* AI benchmarking (run the complete AI suite)
* Overclock validation (run all Stress tests)
* Memory validation (run all Memory tests)
* Regression testing

The feature should scale automatically as new benchmarks are added.

No benchmark should need explicit registration into a "Run All AI" menu item.

Instead, category membership is determined by the benchmark itself.

---

## 17.2 Current State

Today benchmarks expose:

```cpp
virtual const char* GetCategory() const;
```

Examples:

```cpp
"CPU"
"Memory"
"AI"
"Stress"
```

This is sufficient to drive category execution.

No additional metadata is required.

---

## 17.3 Category Enumeration

Add helper functions to `BenchmarkRegistry`.

```cpp
UINT32 GetCategoryCount();
const char* GetCategoryName(UINT32 index);

UINT32 GetBenchmarksInCategory(
    const char* category,
    IBenchmark** outBenchmarks,
    UINT32 maxCount);
```

Internally:

```cpp
CPU
Memory
AI
Stress
```

are discovered dynamically from registered benchmarks.

No hardcoded category list.

---

## 17.4 Main Menu Changes

Add a new menu section:

```text
RUN BENCHMARKS

Run Selected Benchmark
Run All Short
Run All Long

Run All CPU
Run All Memory
Run All AI
Run All Stress
```

Generated dynamically from registry contents.

If a category does not exist:

```text
Run All AI
```

does not appear.

This future-proofs the UI.

---

## 17.5 Execution Behaviour

Selecting:

```text
Run All AI
```

creates a filtered benchmark list:

```text
AI Readiness
AI Memory Bandwidth
AI Cache Behaviour
AI Matrix Compute
```

and executes them sequentially.

The existing `RunAll()` logic should be reused.

Only the benchmark filter changes.

Pseudo-code:

```cpp
for each benchmark
{
    if benchmark->GetCategory() == "AI"
        RunSingle(benchmark);
}
```

---

## 17.6 Progress Display

Category runs need a higher-level progress indicator.

Current benchmark progress:

```text
FP Vector (AVX2)
01:43 / 03:00
```

should become:

```text
AI Suite
Benchmark 2 of 4

AI Memory Bandwidth
01:43 / 03:00
```

Display:

| Field                      | Example             |
| -------------------------- | ------------------- |
| Category                   | AI                  |
| Current Benchmark          | AI Memory Bandwidth |
| Benchmark Index            | 2 / 4               |
| Overall Progress           | 50%                 |
| Current Benchmark Progress | 57%                 |

---

## 17.7 Category Result Summary

After completion show a category summary screen.

Example:

```text
AI BENCHMARK RESULTS

AI Readiness            1668
AI Matrix Throughput    1821
AI Memory Score         1544
AI Cache Score          1432

Overall AI Score        1668
```

For CPU:

```text
CPU BENCHMARK RESULTS

Integer Throughput      2121
Integer Latency         1784
FP Scalar               1911
FP Vector               2423
AES                     3877

CPU Composite Score     2310
```

---

## 17.8 Composite Category Scores

A category score should be automatically calculated.

Formula:

```text
Category Score =
Average of all benchmark scores
```

using normalized benchmark scores.

Example:

```text
CPU:
(2121 + 1784 + 1911 + 2423 + 3877) / 5
```

This creates:

```text
CPU Composite Score
Memory Composite Score
AI Composite Score
Stress Stability Score
```

without requiring custom code per category.

---

## 17.9 Benchmark Metadata Addition

To support category summaries, add:

```cpp
virtual bool IncludeInCategoryScore() const
{
    return true;
}
```

Some benchmarks should not influence composite scores.

Examples:

```cpp
Memory Integrity
Stress Fault Counter
```

These are pass/fail tests.

They should still run but not distort averages.

---

## 17.10 Results History

Store category runs as a grouped result.

Example:

```text
2026-06-13 14:32

AI Suite

AI Readiness          1668
AI Matrix             1821
AI Memory             1544
AI Cache              1432

Composite             1668
```

This allows easy comparison between:

* BIOS versions
* Memory overclocks
* CPU upgrades
* Cooling changes

---

## 17.11 Integration with Core-Cycle Mode

When running:

```text
Run All CPU
```

and Core-Cycle mode is enabled:

Each benchmark runs normally in Core-Cycle mode.

The category runner does not override benchmark execution mode.

It only selects which benchmarks execute.

This keeps behaviour consistent.

---

## 17.12 Integration with Stress Tests

Stress tests are slightly different.

Instead of a composite performance score:

```text
Stress Stability Score
```

should be calculated as:

```text
1000 - (FaultCount × Penalty)
```

or simply:

```text
PASS
FAIL (12 faults)
```

Performance metrics are secondary.

For stress testing, stability is the primary outcome.

---

## 17.13 Future Extension – Run Benchmark Groups

Once category execution exists, benchmark groups become trivial.

Examples:

```text
Quick Validation
    CPU Short
    Memory Short

Overclock Validation
    Memory Stress
    CPU Stress

AI Validation
    AI Readiness
    AI Memory
```

Implementation:

```cpp
virtual const char* GetGroup();
```

This can be added later without redesigning the category runner.

---

## 17.14 Files

### New

```text
Source/CategoryRunner.cpp
Include/CategoryRunner.h
```

### Modified

```text
Include/IBenchmark.h
Include/BenchmarkRegistry.h
Source/BenchmarkRegistry.cpp
Source/BenchmarkRunner.cpp
Source/Tui.cpp
```

### New APIs

```cpp
RunCategory(const char* category);

GetCategories();

GetBenchmarksForCategory();
```

---

### Recommendation

I'd also add one more menu option while implementing this:

```text
Run All CPU
Run All Memory
Run All AI
Run All Stress

Run Everything
```

Where "Run Everything" simply iterates all registered benchmarks in registry order.

That gives users three levels of execution:

1. Single benchmark
2. Category suite
3. Entire benchmark suite

which maps very naturally to how people actually use benchmarking software.

---

Here is the implementation plan for the final section to complete your design document. This section establishes a robust, bare-metal unit testing framework tailored to the unique constraints of a freestanding UEFI environment (no standard `libc`, no OS-provided thread/process isolation, and restricted memory allocation).

---

## # 18. Bare-Metal Unit Testing Framework ⬜

### 18.1 Goal

Provide a comprehensive, automated unit testing suite (`GetCategory() -> "Test"`) to validate the core infrastructure components before running resource-intensive or hardware-critical benchmarks. Because this is a freestanding environment without access to mainstream testing frameworks (like GoogleTest or Catch2), we will implement a lightweight, self-contained **assertion-driven test harness** that runs safe, isolated test cases on the BSP.

The primary objective is to catch regressions in math helpers, allocation strategy, state management, and assembly routines before they trigger system hangs or hard faults during a 30-minute stress loop.

---

### 18.2 Core Components to Validate

The framework will target five critical cross-cutting infrastructure areas:

| Infrastructure Subsystem | Target Verification | Risk Mitigated |
| --- | --- | --- |
| **`TimeBox` Mechanics** | Cycle conversion, time-slice tracking, accumulation logic | Infinite loops or short-circuiting in long benchmarks. |
| **`CpuFeatures` & AVX State** | `CPUID` bitwise mapping, CR4/XCR0 manipulation assembly | Fatal `#UD` faults on deployment. |
| **`BigBuffer` Allocator** | Fragmented page consumption, block boundary constraints | Memory starvation or corruption of the UEFI runtime. |
| **Exception Registration** | Interrupt vector hooks, context capture, `longjmp` recovery | Uncontained crashes or infinite livelocks during OC stress. |
| **TUI Calculations** | Matrix bounds, font scale math, fixed-point string formatting | Screen buffer overflows or rendering lockouts at 1080p. |

---

### 18.3 Test Harness Architecture

To keep infrastructure simple and avoid dynamic registration overhead, tests are written as simple, self-contained functional routines that execute on the BSP.

#### 18.3.1 Assertion Blueprint

Since standard library diagnostics are unavailable, we will use a macro-based system that captures line numbers, file paths, and expressions without relying on exceptions:

```cpp
struct TestContext {
    const char* CurrentTestName;
    UINT64 PassCount;
    UINT64 FailCount;
};

#define TEST_ASSERT(context, expression) \
    do { \
        if (!(expression)) { \
            context.FailCount++; \
            Renderer::DrawTextFormat(Theme::Current().Error, "  [FAIL] %s:%d: %s\n", __FILE__, __LINE__, #expression); \
            return; /* Abort current test case early on fault */ \
        } \
    } while (0)

```

#### 18.3.2 Execution Workflow

* **Isolate Allocations:** Every test case requiring memory must allocate and free its own resources locally to prevent environmental drift between runs.
* **Granular Tracking:** A coordinator method runs every test case sequentially, providing clear visual separation in the TUI.

---

### 18.4 Test Specification Catalog

#### Test Suite 1: Timing & Time-Boxing

* **`Test_TSC_Delta_Validation`:** Verifies `Timer::ReadTSC()` strictly increments. Forces a microsecond delay using the firmware stall service and asserts that `CyclesPerUs()` produces a sane, non-zero scaling factor.
* **`Test_TimeBox_Exhaustion`:** Mocks a dummy loop kernel. Asserts that passing a 50,000 $\mu$s budget to `TimeBox` returns control within a $\pm 5\%$ tolerance band of the actual measured wall-clock time.

#### Test Suite 2: CPU Feature State Protection

* **`Test_AvxState_Idempotency`:** Asserts that calling `EnableAvxState()` multiple times on the same core safely handles pre-existing CR4/XCR0 bits without generating fault vectors.
* **`Test_CpuId_Guard_Mapping`:** Feeds a simulated mock register payload to the feature detector to verify that feature fallbacks (e.g., SSE2 gracefully taking over if AVX2 is absent) trigger correctly.

#### Test Suite 3: BigBuffer Fragmentation Performance

* **`Test_BigBuffer_Greedy_Allocation`:** Allocates a controlled chunk size, checking that segments are cache-aligned ($64$-byte boundaries) and that total reported bytes accurately match the sum of individual segment capacities.
* **`Test_BigBuffer_Boundary_Overflow`:** Intentionally attempts an out-of-bounds write across a multi-segment boundary using mock data, verifying that partition math correctly clamps indices before memory corruption occurs.

#### Test Suite 4: Exception Capture & Jump Recovery

* **`Test_Software_Interrupt_Containment`:** Installs a handler on an unused vector, triggers it via software instruction, and verifies that `SaveContext`/`RestoreContext` wraps the context, records the fault, increments the error counter, and safely steps past the instruction.

#### Test Suite 5: Layout Formatting & Math Limits

* **`Test_Font_Scale_Grid_Math`:** Asserts that when switching scales ($\times 1$ vs $\times 2$), rows and columns scale deterministically according to the rule:

$$\text{EffW} = \text{CHAR\_WIDTH} \times \text{sFontScale}$$



Verifies that total screen column math never violates the minimum $100$-column interface target.
* **`Test_FixedPoint_String_Conversion`:** Validates integer-scaled throughput values (e.g., passing `128450` with an implied divisor of `100` outputs exactly `"1284.50"` without relying on floating-point hardware).

---

### 18.5 User Interface & Lifecycle Integration

#### 18.5.1 TUI Display Layout

When triggered, the unit test runner presents a clean, scannable execution grid:

```text
SYSTEM INFRASTRUCTURE UNIT TESTS

[PASS] Test_TSC_Delta_Validation
[PASS] Test_TimeBox_Exhaustion
[PASS] Test_AvxState_Idempotency
[FAIL] Test_BigBuffer_Greedy_Allocation
       -> Source/Tests/BufferTests.cpp:42: Assert failed: (totalBytes > 0)
[PASS] Test_Software_Interrupt_Containment
[PASS] Test_Font_Scale_Grid_Math

UNIT TEST SUMMARY: 5 PASSED, 1 FAILED

```

#### 18.5.2 Main Menu Injection

Add a new structural command to the primary navigation interface:

```text
SYSTEM TASKS

Run Selected Benchmark
Run All Suites
Run Infrastructure Unit Tests  <-- New Option

```

#### 18.5.3 Pre-Flight Safety Policy

To ensure system safety, the benchmark runner can implement a "Pre-Flight Guard" option in settings:

* When enabled, clicking **"Run All Stress Tests"** or **"Run Everything"** triggers a silent execution of the unit test suite first.
* If any test case fails, the harness aborts the run before launching long benchmarks. This protects the firmware from entering an unrecoverable state due to known underlying math or allocation errors.

---

### 18.6 File Structure Additions

#### New Files

* `Include/Tests/TestHarness.h` — Basic assertion macros, diagnostic structures, and test case declarations.
* `Source/Tests/TestHarness.cpp` — Main execution loop, execution routing, and summary table formatting code.
* `Source/Tests/CoreInfrastructureTests.cpp` — Concrete implementation of the individual test cases listed in section 18.4.

#### Build Pipeline Updates

* Add `Source/Tests/TestHarness.cpp` and `Source/Tests/CoreInfrastructureTests.cpp` directly to the `Makefile` `SOURCES` list and the `UefiBenchmark.inf` `[Sources]` manifest.
* No additional compilation flags are required for these units since they rely exclusively on baseline bare-metal primitives.

---

## # 19. All-Core Selection with Dynamic TUI Thread Migration ⬜

### 19.1 Goal

Provide the ability to run benchmarks across all available physical and logical cores (`Select Cores -> [All Cores (0-N)]`). Because Core 0 is responsible for driving the text user interface (TUI) and responding to user input, executing a heavy benchmark kernel on it requires dynamically migrating the TUI execution thread to an idle auxiliary Application Processor (AP) for the duration of the test. This prevents the user interface from freezing or locking up, allowing for real-time progress bars, continuous hardware readouts, and operational cancellation keystrokes while Core 0 runs at 100% compute utilization.

---

### 19.2 Technical Strategy: AP Interleave Migration

Because UEFI runs in a bare-metal environment without an operating system scheduler, thread migration must be orchestrated via explicit CPU state handoffs using the `EFI_MP_SERVICES_PROTOCOL` and shared atomic memory registers acting as a mailbox.

```text
       [Normal Operation] ──> Core 0 runs TUI; APs are Idle/Parked
               │
               ▼
    [Select Migrant Core] ──> Identify an AP *not* targeted by the benchmark
               │              (Fallback to time-slicing if All-Cores selected)
               ▼
     [Handoff Execution]  ──> Core 0 signals AP via Mailbox to take over TUI
               │
               ▼
     [Execute Benchmark]  ──> Core 0 joins workload; Migrant AP updates screen
               │
               ▼
   [Reclaim Control (M1)] ──> Benchmark ends; Core 0 signals AP to yield
               │
               ▼
    [Restore Original]    ──> AP parks back in firmware loop; Core 0 resumes TUI

```

---

### 19.3 Migration Mechanics & Core Allocation Scenarios

To handle allocations safely, the system must navigate two distinct environmental configurations determined by the user's core mask selection:

#### Scenario A: Partial Multi-Core Selection (Dedicated AP Migration)

* **Condition:** The user selects Core 0 along with some, but not all, APs (e.g., Cores 0, 1, 2, and 3 on an 8-core CPU).
* **Strategy:** The runner selects an unused AP (e.g., Core 7) to act as the temporary TUI host. Core 7 handles keyboard polling and screen blitting smoothly at 30 FPS while Cores 0–3 run the benchmark kernel unhindered.

#### Scenario B: Absolute All-Core Selection (Time-Sliced Interleave)

* **Condition:** Every single available core (0 through $N$) is targeted by the benchmark suite. No idle core exists to host the TUI.
* **Strategy:** To maintain rendering, the benchmark execution kernel on Core 0 explicitly yields a small quantum of time. At the end of every work chunk cycle, Core 0 briefly calls a non-blocking `Tui::Tick()` before jumping back into the next compute iteration.

---

### 19.4 Detailed Changes to Core Primitives

#### 19.4.1 Shared Execution Mailbox

Establish a volatile control structure to manage state handoffs between Core 0 and the designated Migrant AP.

```cpp
enum class TuiHostState : UINT32 {
    HostedOnCoreZero,
    MigrationRequested,
    HostedOnAP,
    YieldRequested,
    Terminated
};

struct MigrationMailbox {
    volatile TuiHostState State;
    volatile UINT32        ActiveHostCoreId;
    EFI_EVENT              MigrationEvent;
};

```

#### 19.4.2 Core Mask Tracking & Architecture Integration

Modify the `BenchmarkRunner` core synchronization loop to parse the allocation layout before triggering worker initialization:

```cpp
struct CoreAllocation {
    UINT64 SelectedCoreMask; // Bit 0 = Core 0, Bit 1 = Core 1, etc.
    BOOLEAN IsCoreZeroTargeted;
};

void BenchmarkRunner::ExecuteOnSelectedCores(IBenchmark* benchmark, CoreAllocation allocation) {
    MigrationMailbox mailbox = {};
    UINT32 migrantCoreId = 0;
    BOOLEAN dynamicMigrationActive = FALSE;

    if (allocation.IsCoreZeroTargeted && FindUnusedAP(&migrantCoreId, allocation.SelectedCoreMask)) {
        mailbox.State = TuiHostState::MigrationRequested;
        // Wake target AP and point its instruction pointer to the TUI background loop
        MpServices->StartupThisAP(migrantCoreId, Tui::RunMigratedLoop, &mailbox);
        
        // Wait for the AP to take ownership
        while (mailbox.State != TuiHostState::HostedOnAP) {
            CpuPause();
        }
        dynamicMigrationActive = TRUE;
    }

    // 1. Wake up target APs for the actual workload
    MpServices->StartupSelectedAPs(benchmark, allocation.SelectedCoreMask & ~1ULL);

    // 2. Core 0 joins the fray directly as a compute worker
    if (allocation.IsCoreZeroTargeted) {
        benchmark->RunOnCurrentCore(); 
    }

    // 3. Sync point: Wait for all running worker APs to hit the final barrier
    while (!AllAPsFinished()) {
        CpuPause();
    }

    // 4. Reclaim TUI control if it was shifted
    if (dynamicMigrationActive) {
        mailbox.State = TuiHostState::YieldRequested;
        while (mailbox.State != TuiHostState::HostedOnCoreZero) {
            CpuPause();
        }
    }
}

```

#### 19.4.3 The AP Migration Loop

The designated AP runs this isolated execution frame while Core 0 is executing the workload:

```cpp
void Tui::RunMigratedLoop(void* Context) {
    MigrationMailbox* mailbox = (MigrationMailbox*)Context;
    
    mailbox->ActiveHostCoreId = GetCurrentCoreId();
    mailbox->State = TuiHostState::HostedOnAP;

    while (mailbox->State == TuiHostState::HostedOnAP) {
        // 1. Poll for keyboard input (allows for live cancellation)
        Tui::PollInput();

        // 2. Query global atomic benchmark counters to update progress tracks
        Tui::UpdateActiveProgressBars();

        // 3. Render frame to hardware console buffer
        Tui::RefreshScreen();

        // 4. Bound the loop to prevent AP from starving the system bus
        gBS->Stall(33000); // ~30 FPS
    }

    // Acknowledge the yield request and exit to park the core back in firmware
    mailbox->State = TuiHostState::HostedOnCoreZero;
}

```

---

### 19.5 Thread Safety & Hardware Constraints

Because UEFI firmware drivers are typically **not thread-safe**, migrating text output functions across different physical cores requires strict coordination:

* **Console Serial Lock:** If your TUI issues raw direct calls to `ST->ConOut->OutputString`, these firmware protocols must only be accessed by one core at a time. The migration design guarantees mutual exclusion because Core 0 stops calling console functions completely before the AP begins its rendering loop.
* **Shared Heap Protection:** Memory allocations (`AllocatePool`) performed during the background TUI thread loop must be wrapped in an atomic spinlock if the compute kernels running on other cores are also dynamic allocators. (As per Section 18.3.1, benchmarks use localized buffers to minimize this conflict risk).

---

### 19.6 User Interface Updates

#### 19.6.1 Settings Menu Configuration

Update the "Target Cores" toggle list within the configuration interface to expose the full range of selection topologies:

```text
BENCHMARK CONFIGURATION

Target Cores: [ All Cores (0-15)    ]
               [ AP Cores Only (1-15) ]
               [ Custom Mask...       ]
               [ Single Core (Core 0) ]

```

#### 19.6.2 Visual Indication During Core 0 Execution

While a benchmark is active and the TUI loop is running on an auxiliary processor, a status line notifies the user of the active core topology:

```text
┌────────────────────────────────────────────────────────────────────────┐
│ UNIFORM HARDWARE STRESS SUITE                                         │
├────────────────────────────────────────────────────────────────────────┤
│                                                                        │
│  Executing: AVX2 Complex Matrix Stress Math                           │
│  Progress : [██████████████████████░░░░░░░░░░] 68%                     │
│                                                                        │
│  [STATUS] TUI Thread migrated to AP Core 7.                            │
│           Core 0 is actively engaged in compute workloads.             │
│                                                                        │
└────────────────────────────────────────────────────────────────────────┘

```

---

### 19.7 Verification Tests for Bare-Metal Framework

To validate that thread migration does not corrupt the CPU context or memory registers, we append these assertions to the unit testing suite outlined in Section 18:

* **`Test_Tui_Thread_Migration_Handoff`:** Programmatically triggers a migration to an available AP, simulates 3 screen refreshes from that AP, triggers a recall to Core 0, and asserts that the `ActiveHostCoreId` reads accurately at every stage of the lifecycle.
* **`Test_Interleaved_Input_Capture`:** Verifies that keystrokes registered by the firmware's input buffer while the TUI loop is executing on an AP core are successfully captured, buffered, and parsed without losing data packet integrity.
* **`Test_CoreZero_Context_Sanity`:** Asserts that after `Tui` ownership reverts to Core 0, the firmware's standard text output protocols (`ST->ConOut`) retain valid function pointers and do not throw General Protection faults (`#GP`).

---

## # 20. Scrollable Viewports & Low-Level DRAM Timing Extraction ⬜

### 20.1 Goal

Upgrade the System Information page to support vertical scrolling (`Up`/`Down` arrow keys or `PageUp`/`PageDown`) to handle an arbitrary number of hardware telemetry lines. Concurrently, expand the hardware detection subsystem to query and display primary memory timings—**$t_{CL}$** (CAS Latency), **$t_{RCD}$** (RAS to CAS Delay), **$t_{RP}$** (RAS Precharge), **$t_{RAS}$** (Active to Precharge Delay), and **$t_{RC}$** (Row Cycle Time)—by parsing the motherboard’s Memory Controller configuration space or SMBus SPD data.

---

### 20.2 Part A: The TUI Scrollable Viewport

Currently, the TUI draws static components directly to absolute screen coordinates. To support scrolling, we will introduce a `ScrollViewport` abstraction that manages a virtual line buffer and maps a visible window subset to the physical screen.

#### 20.2.1 Architectural Layout

```text
  Virtual Document (N Lines)             Physical Screen (80x25 or 100x30)
┌───────────────────────────┐          ┌──────────────────────────────────┐
│ Line 0: CPU Information   │          │ SYSTEM INFORMATION               │
│ Line 1: Cores & Topology  │          ├──────────────────────────────────┤
│ Line 2: Cache Topology    │ ┌───────>│ Line 2: Cache Topology           │
│ Line 3: Memory Controller │ │        │ Line 3: Memory Controller        │
│ Line 4: tCL  = 16 cycles  │─┼───────>│ Line 4: tCL  = 16 cycles         │ Window Height
│ Line 5: tRCD = 18 cycles  │ │        │ Line 5: tRCD = 18 cycles         │ (e.g., 18 lines)
│ Line 6: tRP  = 18 cycles  │ │        │ Line 6: tRP  = 18 cycles         │
│ Line 7: tRAS = 36 cycles  │ └───────>│ Line 7: tRAS = 36 cycles         │
│ Line 8: tRC  = 54 cycles  │          ├──────────────────────────────────┤
│ Line 9: SPD Manufacturer  │          │ [▲/▼] Scroll  [ESC] Main Menu    │
└───────────────────────────┘          └──────────────────────────────────┘
        ▲ Viewport Offset (ScrollPosition = 2)

```

#### 20.2.2 Primitive Architecture

```cpp
class ScrollViewport {
public:
    void Clear();
    void AddLine(const CHAR16* Format, ...);
    void HandleInput(EFI_INPUT_KEY Key);
    void Render(UINT32 StartRow, UINT32 MaxRows);

private:
    static const UINT32 MAX_VIRTUAL_LINES = 256;
    static const UINT32 MAX_LINE_LENGTH = 120;
    
    CHAR16 m_Buffer[MAX_VIRTUAL_LINES][MAX_LINE_LENGTH];
    UINT32 m_TotalLines = 0;
    UINT32 m_ScrollPosition = 0; // Current top visible line index
};

```

#### 20.2.3 Input and Bounds Handling

* **`SCAN_UP` / `SCAN_DOWN`:** Increments or decrements `m_ScrollPosition` by `1`, clamped strictly between `0` and `(m_TotalLines - MaxRows)`.
* **`SCAN_PAGE_UP` / `SCAN_PAGE_DOWN`:** Shifts `m_ScrollPosition` by `MaxRows - 2` to preserve a visual tracking anchor line.
* **Rendering Transformation:** When redrawing, the loop shifts coordinates:

$$\text{TargetScreenRow} = \text{StartRow} + (\text{CurrentLineIndex} - \text{m\_ScrollPosition})$$



---

### 20.3 Part B: Low-Level DRAM Timing Extraction

Retrieving memory timings in a freestanding environment without an OS driver requires interacting with hardware configuration spaces. We will implement a multi-tiered lookup engine that attempts high-speed extraction methods, falling back gracefully if access parameters are locked down by the firmware.

```text
                  [Lookup Initiation]
                           │
                           ▼
          Method 1: EFI SMBus ID Lookup (SPD) ───[Success]───► Parse Bytes
                           │
                        [Fail]
                           │
                           ▼
          Method 2: PCI Config Space Probing   ───[Success]───► Map Registers
                           │
                        [Fail]
                           │
                           ▼
          Method 3: SMBIOS Type 17 Table Lookups ──[Success]───► Fallback Parse
                           │
                        [Fail]
                           │
                           ▼
               Display "[Unknown / Locked]"

```

#### 20.3.1 Method 1: The EFI SMBus Protocol (Reading SPD EEPROM)

The most reliable platform-agnostic approach is reading the JEDEC Serial Presence Detect (SPD) non-volatile data from the memory modules via the `EFI_SMBUS_HC_PROTOCOL`.

* **The Strategy:** Locate the protocol, scan standard JEDEC SMBus target addresses (`0x50` through `0x57`), and read the raw configuration bytes defined by the memory standard (DDR4 vs DDR5).
* **DDR4 SPD Register Offsets:**
* **$t_{CL}$:** Byte 24 (Bits 0-7 describe supported CAS Latencies), Byte 25 ($t_{CK}$ min base value).
* **$tRCD$:** Byte 29 (Minimum RAS to CAS delay time in picoseconds).
* **$tRP$:** Byte 30 (Minimum Row Precharge delay time in picoseconds).
* **$tRAS$:** Byte 31 & 32 (Upper/Lower bits for Minimum Active to Precharge delay).


* **DDR5 SPD Register Offsets:** DDR5 moves to a dual-channel-per-DIMM infrastructure with an altered byte catalog (e.g., Base $t_{CK}$ moves to Byte 20, $t_{CL}$ options are mapped across Bytes 26–29).

#### 20.3.2 Method 2: Host Bridge PCI Configuration Space Probing

If the firmware hides the SMBus protocol, the system can fallback to reading memory controller registers mapped via the PCI Bus.

* **Targeting:** Locate Bus 0, Device 0, Function 0 (typically the Host Bridge Controller).
* **Architecture-Specific Map:**
* **AMD Zen 3 (Ryzen 5000 Series):** Timings are exposed via Data Fabric Configuration Spaces accessed via indirect indexing hooks at PCI configuration registers `0x60` and `0x64` (DF_FICAA / DF_FICAD).
* **Intel Core Architecture:** Timings are derived by finding the Base Address Register (BAR) for the Memory Controller MMIO space (MCHBAR), typically exposed at PCI configuration offset `0x48` or `0x68`, and parsing offsets relative to that mapped pointer (e.g., `MCHBAR + 0x4000`).



#### 20.3.3 Method 3: SMBIOS Type 17 Table Fallback

If direct hardware access is blocked by the platform's security policy, the suite parses the framework's asset tables.

* **The Strategy:** Traverse the EFI System Configuration Tables to match the `SMBIOS_TABLE_GUID`. Loop through structures matching `SMBIOS_TYPE_MEMORY_DEVICE` (Type 17).
* **Limitations:** While Type 17 reliably reports memory manufacturer, size, and configured clock speed, structural definitions for individual core cycles ($t_{RCD}$, $t_{RP}$) are often left unpopulated (`0x00` or `0xFF`) by consumer UEFI implementations.

---

### 20.4 Data Structures & UI Output Mapping

Add a dedicated timing layout structure to the hardware payload catalog:

```cpp
struct DramTimings {
    BOOLEAN IsValid;
    UINT32  ClCycles;   // tCL
    UINT32  RcdCycles;  // tRCD
    UINT32  RpCycles;   // tRP
    UINT32  RasCycles;  // tRAS
    UINT32  RcCycles;   // tRC
    UINT32  VoltageMv;  // Configured VDD in millivolts
};

```

When rendering the scrollable data stack, the system formats these metrics dynamically into explicit cycle strings:

```text
MEMORY CONTROLLER & TIMINGS
  Controller Mode  : Dual-Channel interleaved (128-bit)
  Memory Frequency : 1800.3 MHz (DDR4-3600 effective)
  
  Primary Timings  : 16 - 18 - 18 - 36 - 54
    tCL  (CAS Latency)                : 16 cycles
    tRCD (RAS to CAS Delay)           : 18 cycles
    tRP  (Row Precharge Time)         : 18 cycles
    tRAS (Active to Precharge Delay)  : 36 cycles
    tRC  (Row Cycle Time)             : 54 cycles
  Memory Voltage   : 1.350 V

```

---

### 20.5 Verification & Unit Testing

To ensure the scrolling mechanics and low-level probes do not cause memory access faults, add these assertions to the testing infrastructure:

* **`Test_ScrollViewport_Bounds_Clamp`:** Feeds exactly 100 mock strings into a `ScrollViewport` with a simulated 15-line display ceiling. Artificially passes `SCAN_DOWN` events 200 times and asserts that `m_ScrollPosition` cleanly halts at exactly `85`, preventing buffer under-read or over-read faults.
* **`Test_Memory_Timing_Parity`:** Validates that if `DramTimings::IsValid` evaluates to `TRUE`, the calculated $t_{RC}$ cycle count is logically greater than or equal to the sum of $t_{RAS} + t_{RP}$. This protects the readout reporting code from displaying garbage or corrupted register initialization data.

---

## # 21. AI Suitability Matrix & Hardware Feature Mapping

### 21.1 Goal

Provide a transparent, deterministic audit of the CPU’s capability to execute performant, low-latency AI and machine learning inference workloads. The system will map specific CPU capabilities extracted via `CPUID` and `XCR0` extended state registers to an overall **AI Suitability Rating**, showing a clear breakdown of critical, high-performance, and next-generation instruction sets.

---

### 21.2 The AI Suitability Tiering Matrix

Rather than using vague subjective criteria, suitability is categorized into rigid, hardware-bounded tiers determined by vector registers, operational widths, and specialized data-type acceleration:

| Tier Rating | Required Capabilities | Practical Performance Impact |
| --- | --- | --- |
| **Excellent** | AVX-512 (F, BW, DQ, VL) + **AVX512_VNNI** *OR* AVX10 | Native hardware acceleration for INT8 quantization. Negligible overhead for deep integer neural network execution. |
| **Very Good** | AVX2 + FMA3 + **AVX_VNNI** *OR* **AMX** (if applicable) | High-throughput 256-bit vector lane operations with dedicated fused integer dot-product processing. |
| **Good** | AVX2 + FMA3 (Reference Target Baseline) | Full 256-bit floating-point execution, but lacks specialized hardware dot-product accumulation formatting. |
| **Limited** | AVX (128-bit execution) *OR* SSE4.2 Only | Massive performance penalties due to instruction splitting, register spills, and scalar emulation fallback loops. |

---

### 21.3 Core Primitives & Instruction Mapping

To accurately assess suitability, the `HardwareDetector` layer will evaluate features across three specific operational priority bands:

#### 1. Core Vector Extensions (The Data Highway)

* **AVX / AVX2:** Checked via `CPUID.01H:ECX[bit 28]` and `CPUID.07H.0H:EBX[bit 5]`. Provides the 256-bit wide SIMD registers (`YMM`) necessary to process multiple weight weights simultaneously.
* **AVX-512F (Foundation):** Checked via `CPUID.07H.0H:EBX[bit 16]`. Expands registers to 512-bit (`ZMM`), doubling execution capacity per clock cycle over AVX2.

#### 2. Precision & Accumulation Helpers (The Engine)

* **FMA3 (Fused Multiply-Add):** Checked via `CPUID.01H:ECX[bit 12]`. Computes $A \times B + C$ in a single clock cycle with a single rounding step. This is the atomic foundational instruction for all matrix-matrix multiplications ($GEMM$).
* **VNNI (Vector Neural Network Instructions):** * *AVX-512 Variant:* Checked via `CPUID.07H.0H:ECX[bit 11]`.
* *AVX2 Variant (AVX_VNNI):* Checked via `CPUID.07H.1H:EAX[bit 4]`.
* *Impact:* Combines three separate instructions into one for horizontal byte/word additions, executing `VPDPBUSD` (Multiply and Accumulate Signed/Unsigned Bytes) to vastly accelerate INT8 quantized token processing.



#### 3. Cryptography & Key Generation (Auxiliary Pipelines)

* **AES-NI & SHA:** Checked via `CPUID.01H:ECX[bit 25]` and `CPUID.07H.0H:EBX[bit 29]`. While not used directly for matrix math, these accelerate tensor payload hashing, weights verification, and secure context streaming over networks.

---

### 21.4 Implementation Architecture & Data Structure

Extend the detection layer data models to encapsulate the evaluation criteria payload:

```cpp
enum class AiSuitabilityTier : UINT32 {
    Limited,
    Good,
    VeryGood,
    Excellent
};

struct AiFeatureStatus {
    const char* FeatureName;
    const char* UtilityDescription;
    BOOLEAN     IsSupported;
    BOOLEAN     IsCritical; // TRUE if it changes structural tier bounds
};

struct AiSuitabilityPayload {
    AiSuitabilityTier Tier;
    AiFeatureStatus   Features[8];
    UINT32            TotalTrackedFeatures;
};

```

#### Evaluation Logic Example

```cpp
AiSuitabilityTier EvaluateAiTier(const CpuFeatures& cpu) {
    if (cpu.HasAvx512F && cpu.HasAvx512Vnni) return AiSuitabilityTier::Excellent;
    if (cpu.HasAvx2 && cpu.HasFma3 && cpu.HasAvxVnni) return AiSuitabilityTier::VeryGood;
    if (cpu.HasAvx2 && cpu.HasFma3) return AiSuitabilityTier::Good;
    return AiSuitabilityTier::Limited;
}

```

---

### 21.5 TUI Display Interface Layout

When rendering inside the newly designed scrollable viewport from Section 20, the AI Suitability page outputs a clean, visually structured checklist. For a system matching your hardware target specification (e.g., AMD Ryzen 7 5800X), it would dynamically render as follows:

```text
APPLICATION SUITABILITY: LOCAL AI INFERENCE
  Overall Rating: [ VERY GOOD ]
  
  Instruction Set Matrix:
  [  OK  ] AVX2   (256-bit Vector Execution) : Found
  [  OK  ] FMA3   (Fused Multiply-Add Math)  : Found
  [ MISS ] VNNI   (Vector Neural Net Int8)   : Not Supported
  [ MISS ] AVX512 (512-bit Ultra-Wide SIMD)  : Not Supported
  [  OK  ] AVX_VN (AVX2-Extended Dot Product): Found
  [  OK  ] AES-NI (Model Weights Validation) : Found
  
  Architectural Assessment:
  The CPU provides optimal 256-bit matrix paths via AVX2 and FMA3.
  Hardware INT8 optimization is active via AVX_VNNI extensions.
  Highly suitable for small-scale quantized LLM model execution (e.g., Llama-3 8B Q4).

```

---

### 21.6 Unit Test Specifications

Add these boundary verification checks to the `Source/Tests/CoreInfrastructureTests.cpp` test catalog:

* **`Test_AiTier_Evaluation_Logic`:** Injects simulated `CpuFeatures` structs representing historic architectures (e.g., Haswell, Zen 1, Zen 3, Skylake-X) to verify that the tier evaluator assigns the correct rating profile based strictly on the instruction matrix intersections.
* **`Test_Feature_String_Safety`:** Asserts that descriptive diagnostic strings mapped to missing instructions do not exceed the `MAX_LINE_LENGTH` constraints of the `ScrollViewport`, ensuring zero overflow risk when rendered onto constrained terminals.

---

## # 22. High-Performance Double-Buffered Video Engine

### 22.1 Goal

Eliminate text rendering latency and sub-dword PCIe write overhead on bare-metal systems running at resolutions above 800x600. The TUI will switch from direct-to-firmware line writing to a high-speed **shadow frame-buffer architecture** that uses aligned 64-byte `SSE/AVX` block-writes into system RAM, executing exactly one synchronous vertical-blank aligned cache blit per refresh frame.

---

### 22.2 Technical Strategy: Shadow Frame Architecture

Instead of modifying the hardware VRAM layout character-by-character, the application establishes a system-side mirror buffer.

* All layout positioning, font scaling, matrix alignment, and color processing are performed completely in system RAM (L1/L2 cache speeds).
* When a frame refresh tick occurs, the layout is converted into a linear 32-bit RGBA pixel stream in a staging buffer.
* This staging buffer is subsequently pushed to the raw GOP hardware destination address using continuous, aligned vector instructions.

```text
  [TUI Engine Layout] ──> Modifies local text structures in system cache
                                │
                                ▼
  [Local Render Frame] ──> Generates raw 32-bpp BGRA array in Local RAM
                                │
                                ▼
  [Hardware Blit Engine] ──> Aligned Vector Stream Writes (SSE/AVX)
                                │ (Bypasses firmware API layers completely)
                                ▼
  [GOP PCIe Framebuffer] ──> Physical Hardware Panel Display

```

---

### 22.3 Implementation Primitives

#### 22.3.1 Video Engine Buffer Structure

We will update `Include/Tui.h` to abstract the system-side allocation targets:

```cpp
struct FrameBufferTopology {
    UINT32* HardwareBaseAddress;
    UINT32* ShadowBuffer;         // Allocated in cacheable system pool
    UINTN    BufferSizeInBytes;
    UINT32   HorizontalResolution;
    UINT32   VerticalResolution;
    UINT32   PixelsPerScanLine;
};

class VideoEngine {
public:
    static EFI_STATUS Initialize(EFI_GRAPHICS_OUTPUT_PROTOCOL* Gop);
    static void       MarkDirty(UINT32 StartRow, UINT32 EndRow);
    static void       Present(); // Flushes local buffer changes to hardware
    
private:
    static FrameBufferTopology m_Topology;
    static UINT32              m_DirtyStartRow;
    static UINT32              m_DirtyEndRow;
};

```

#### 22.3.2 Optimization 1: Aligned Vector Block Copying

To maximize throughput across the PCIe bus, the `Present()` mechanism drops down to assembly or compiler-intrinsic streaming configurations. Instead of basic byte-copy loops, it pushes chunks using 128-bit (`XMM`) or 256-bit (`YMM`) non-temporal store hints (`_mm256_stream_si256`), which bypass the CPU cache hierarchy to aggregate writes directly into continuous PCIe transaction packets.

```cpp
// High-throughput cache-line line blitting implementation
void VideoEngine::Present() {
    if (m_DirtyStartRow >= m_DirtyEndRow) return;

    UINTN bytesPerLine = m_Topology.PixelsPerScanLine * sizeof(UINT32);
    UINTN startByteOffset = m_DirtyStartRow * bytesPerLine;
    UINTN totalBytesToFlush = (m_DirtyEndRow - m_DirtyStartRow) * bytesPerLine;

    UINT8* src = (UINT8*)m_Topology.ShadowBuffer + startByteOffset;
    UINT8* dest = (UINT8*)m_Topology.HardwareBaseAddress + startByteOffset;

    // Fast vector tracking block loop
    UINTN blocks = totalBytesToFlush / 32;
    for (UINTN i = 0; i < blocks; ++i) {
        // Load aligned 32 bytes from system cache memory
        __m256i vectorData = _mm256_load_si256((__m256i const*)(src + (i * 32)));
        // Force stream non-temporal write directly to PCIe VRAM
        _mm256_stream_si256((__m256i*)(dest + (i * 32)), vectorData);
    }

    // Handle any byte-level fractional stragglers
    UINTN trailingOffset = blocks * 32;
    for (UINTN j = trailingOffset; j < totalBytesToFlush; ++j) {
        dest[j] = src[j];
    }

    // Reset row tracking boundaries
    m_DirtyStartRow = m_Topology.VerticalResolution;
    m_DirtyEndRow = 0;
}

```

#### 22.3.3 Optimization 2: Granular Vertical Dirty Tracking

To optimize high-frequency rendering performance (such as during multi-core thread migrations outlined in Section 19), the TUI updates rows selectively. If a text string is rewritten in the middle of the display matrix:

1. The engine calculates the corresponding pixel rows.
2. It expands `m_DirtyStartRow` and `m_DirtyEndRow` to wrap only that vertical bounding block.
3. The next `Present()` frame copy skips unchanged header and footer rows completely, decreasing the overall frame data transit footprint by up to 85%.

---

### 22.4 Integration with the Thread Migration Architecture

When the TUI loop is migrated to an auxiliary core during all-core benchmark evaluation loops, the double buffer prevents the AP from generating concurrent bus friction with Core 0:

1. **Isolation:** The background AP manipulates **only** the `ShadowBuffer` in system RAM.
2. **Deterministic Footprint:** The AP limits memory transactions on the physical motherboard system bus to a single swift frame blit operation inside `Tui::RunMigratedLoop()` at fixed 30 FPS boundary tracks:

```cpp
// Updated background execution frame inside section 19.4.3
while (mailbox->State == TuiHostState::HostedOnAP) {
    Tui::PollInput();
    Tui::UpdateActiveProgressBars(); // Updates ShadowBuffer layout arrays
    
    // Perform fast RAM-to-RAM drawing operations
    Tui::RasterizeTextToShadowBuffer(); 
    
    // Execute single high-efficiency vector copy to the hardware
    VideoEngine::Present();

    gBS->Stall(33000); 
}

```

---

### 22.5 Verification Tests for Real-Hardware Suitability

* **`Test_ShadowBuffer_Alignment_Parity`:** Asserts that the memory block generated for `ShadowBuffer` is strictly aligned onto a $64$-byte boundary pool limit. This prevents system fault trap conditions (`#GP`) when executing performance-critical SIMD vector loads.
* **`Test_DirtyBounds_Clamping_Logic`:** Simulates out-of-bounds line manipulation inputs and validates that dirty rows are clipped gracefully to the active terminal pixel limitations (`VerticalResolution`). This eliminates buffer overflow or pointer wraparound vulnerabilities.

