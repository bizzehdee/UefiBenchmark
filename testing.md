# Test Strategy & Unit-Test Framework Plan

Planning document only — **no tests are built here**. This defines *what* we test,
*how* the harness is wired, and *which behaviours* matter. The guiding rule
throughout: **test observable behaviour, not implementation.** A test asserts what a
unit promises to its caller (its contract), never how it achieves it. If an
implementation is rewritten and the contract still holds, the tests must still pass.

---

## 1. The core problem: this is a freestanding UEFI binary

The application is `-ffreestanding`, no libc, no C++ exceptions, no host runtime. It
targets PE/COFF and boots under firmware. That shapes everything:

- **We cannot run the real binary in a normal test process.** It expects `gBS`,
  `gST`, GOP, MP Services, a calibrated TSC, and AVX state enabled by hand.
- **Most of the value, though, is in pure logic** that happens to be compiled into
  that binary: string/number formatting, the `Vector<T>` container, statistics,
  the time-box loop, score/tier evaluation, buffer partitioning maths, core-selection
  presets, scroll/viewport state, registry bookkeeping, `RunConfig` budget maths.
  None of this needs firmware — it needs only the project's own types (`UINT64`,
  `UINTN`, …) and a heap.
- **The actual benchmark kernels** (AVX2/FMA, AES-NI, CRC32, pointer-chase, DRAM
  streaming, INT8/INT4 GEMM) and the hardware-enable sequences (`CR4`/`XCR0`, MP
  dispatch, GOP blit) are **not unit-testable by nature** — their behaviour *is* the
  hardware. They are covered by a different layer (QEMU smoke + on-hardware runs),
  not by unit tests.

So the strategy splits cleanly into three tiers. The bulk of the document is Tier 1.

| Tier | What | Where it runs | This plan |
|------|------|---------------|-----------|
| 1. Unit (behavioural) | Pure logic, deterministic, host-compilable | Native host (g++/clang) | **Primary focus** |
| 2. Integration / boot smoke | App boots, menus render, benchmarks complete, results display | QEMU + OVMF | Scripted checklist |
| 3. Hardware truth | Real kernels, real scores, fault containment, whole-RAM alloc | 5800X bare metal / USB boot | Manual checklist |

---

## 2. Tier 1 — unit test framework

### 2.1 Framework choice: **doctest**

Recommendation: **[doctest](https://github.com/doctest/doctest)** — a single header,
zero dependencies, fast to compile, and trivially vendorable into `tests/third_party/`.

Why doctest over the alternatives:

- **Single header, no build system intrusion.** Drop `doctest.h` in the tree; the
  test target is one extra `g++` invocation. Matches a project that already hand-rolls
  its Makefile and vendors everything.
- **Catch2** is equally capable but heavier to compile and larger; no benefit here.
- **GoogleTest** needs a built library and CMake-ish wiring — too much ceremony for a
  repo with no host build today.
- We are *not* writing tests inside the production `.cpp` files (doctest supports that,
  but it would pull the test framework into the freestanding translation units). Tests
  live entirely in `tests/`.

The framework choice is deliberately low-stakes: tests are written as
`CHECK(actual == expected)` behavioural assertions, so swapping frameworks later is a
mechanical find/replace.

### 2.2 The central design challenge: compiling freestanding code on the host

The production headers include `UefiTypes.h` and call into firmware-backed primitives
(`operator new` → `AllocatePool`, `Timer::ReadTSC`, `Renderer::DrawText`,
`gBS->AllocatePages`). To compile a unit under a host test we provide a **thin host
shim** that satisfies those symbols *without changing production code*:

```
tests/
  doctest.h                  # vendored framework
  host/
    UefiShim.h / .cpp        # host-side definitions of the freestanding seams
  unit/
    test_freestanding.cpp
    test_statistics.cpp
    test_timebox.cpp
    test_runconfig.cpp
    test_longbenchmarkbase.cpp
    ...
  Makefile                   # `make -C tests` builds + runs; or a top-level `make test`
```

The shim provides exactly three things:

1. **A heap.** Route the project's `operator new/delete` and `memset/memcpy/memmove/
   memcmp` to the host's `malloc`/`<cstring>` so `Vector<T>`, `ScrollViewport`, etc.
   allocate normally. (The production `Freestanding.cpp` versions call Boot Services;
   for host tests we link the shim's versions instead of `Freestanding.cpp`'s
   allocation path — string/number helpers from `Freestanding.cpp` are kept and
   tested directly.)
2. **Type compatibility.** `UefiTypes.h` is plain typedefs and POD structs — it
   compiles on the host as-is. No shim needed beyond making sure it's on the include
   path. Verify it has no MSVC/PE-only constructs; if any leak in, isolate them.
3. **Seams for hardware singletons** — see §2.3.

**Important constraint:** the shim exists to *enable compilation and provide a heap*,
not to fake behaviour. We do **not** mock the unit under test. We only stub the
firmware boundary it sits on.

### 2.3 Seams — making hardware-coupled units testable without touching the kernels

Several production singletons are hardware-coupled but wrap pure logic we want to test.
The plan is to test the **pure logic** through a seam and leave the hardware call
behind it untested at this tier.

- **`Timer`** (`TimeBox`, stopwatch maths). `TimeBox::Run` is a template that calls
  `Timer::ReadTSC()`, `Timer::CyclesPerUs()`, `Timer::IsCalibrated()`. For host tests
  we link a **fake Timer** whose `ReadTSC()` advances by a controllable amount each
  call and whose `IsCalibrated()`/`CyclesPerUs()` are set per-test. This lets us drive
  the time-box loop deterministically — *we control simulated time*, so we can assert
  the loop's contract (see §3.3) without any real clock.
- **`Renderer`** (`ScrollViewport::Render`). The viewport's *state* logic (scroll
  clamping, key handling, cursor-following) is pure; only `Render()` paints. Link a
  **recording fake Renderer** that captures `DrawText`/`DrawTextBg` calls into a list
  of `(row, col, text)` tuples. Tests assert *what would be drawn* (which lines, in
  which order, with which highlight) — behaviour — without a framebuffer. `Columns()`
  /`Rows()` return test-set values so we can exercise different grid sizes.
- **MP Services / `BigBuffer` allocation.** `CoreSelection` presets and
  `BigBuffer`'s offset/partition maths are pure functions of a roster / segment list.
  The seam: expose (or test through) a path that lets a test **inject a roster**
  (`ApInfo[]`) and an **injected segment list** (`BigSegment[]` + total size) instead
  of going through MP Services / `AllocatePages`. The real allocation and enumeration
  are Tier 2/3 concerns.
- **`LongBenchmarkBase`** (`TryReportProgress` rate-limiting). The Timer seam above
  also gates the 500 ms render interval in `TryReportProgress`. With the fake Timer
  the rate-limiting logic is deterministic — tests can advance simulated time by
  exactly the threshold and assert the callback fires or is suppressed. The atomic
  trylock behavior can be tested single-threaded by calling from one "winner" and one
  "loser" in sequence.

If a unit cannot be reached without invoking real firmware and has *no* extractable
pure core, it is explicitly **out of scope for Tier 1** (listed in §5).

### 2.4 Build & run

- New target `make test` (or `make -C tests`) using the **native** host compiler
  (`g++`/`clang++`), **not** the PE/COFF cross flags. No `-ffreestanding`, normal
  hosted C++17. The test TU includes the same production headers from `Include/` and
  links the specific production `.cpp` under test plus `UefiShim.cpp`.
- Each test binary links the *minimum* production translation units it needs, so a
  failure points at one module. Keep a single combined `run-all` binary too for CI
  convenience.
- **Determinism:** no test may depend on wall-clock time, RNG without a fixed seed,
  allocation addresses, iteration timing, or thread scheduling. The fake Timer makes
  time deterministic; any randomized permutation logic is seeded explicitly.

---

## 3. Behaviour catalogue (Tier 1)

Each module below lists the **contracts** to assert — phrased as observable behaviour.
These are *what to test*, not test code. "Implementation-coupled" anti-tests we
deliberately avoid are noted where the temptation is strong.

### 3.1 `Freestanding` — strings, numbers, memory primitives

- `StrLen` returns the count of bytes before the NUL; `0` for empty string.
- `StrCmp` orders lexicographically; `0` only on equal content; sign matches first
  differing byte. Equal-prefix-but-shorter sorts before longer.
- `StrCopy` never writes past `maxLen`, always NUL-terminates within bounds, and
  copies the full source when it fits. (Behaviour at the boundary `len == maxLen` is
  the key case — assert no overrun, defined truncation.)
- `UintToStr`: `0` → `"0"`; max `UINT64` round-trips to the correct decimal; no leading
  zeros. `IntToStr`: negatives get a single leading `-`; `INT64_MIN` is handled (the
  classic edge — assert the exact string, not "it doesn't crash").
- `HexToStr(value, digits)`: zero-padded to `digits` width; correct nibbles;
  `digits` smaller than needed and larger than needed both behave per contract.
- `memset/memcpy/memmove/memcmp`: standard contracts, **plus** `memmove` correctness on
  overlapping forward and backward ranges (the property that distinguishes it from
  `memcpy`); `memcmp` sign on first differing byte and `0` on equal.
- *Avoid:* asserting the address returned by `IntToStr`'s static buffer, or that two
  calls reuse it — that's implementation. Test the string value of a single call.

### 3.2 `Vector<T>` — the move-aware container

- Starts empty: `Size()==0`, `Empty()==true`.
- `PushBack` grows size by one and preserves element order and values across reallocs
  (push enough to force several `Grow()`s, then read all back in order).
- Move-only element types work: push a non-copyable, move-only `T` and confirm values
  survive a growth-triggered reallocation (the move-construct path).
- `Reserve(n)` makes capacity ≥ n without changing observable size or contents;
  `Reserve` smaller than current capacity is a no-op observable as "contents unchanged".
- `Clear()` runs destructors (observe via a `T` that increments a counter in `~T`) and
  resets size to 0 while leaving the vector reusable.
- Move-construct / move-assign transfer ownership: source becomes empty, destination
  holds the original elements; self-move-assign is safe.
- Destructor destroys exactly the live elements once each (counter-instrumented `T`:
  constructed count == destructed count, no double-destroy).
- *Avoid:* asserting the exact capacity sequence (8, 16, 32…). Growth *policy* is
  implementation; the *contract* is "capacity is enough and elements survive".

### 3.3 `TimeBox::Run` / `RunWithProgress` — the time-box loop

Driven by the fake Timer (§2.3). Behaviours:

- **Uncalibrated / zero-budget / zero-chunk fallback:** runs the kernel exactly once
  with the given chunk size and returns that chunk size. (Assert kernel invoked once;
  return value equals chunk.) `RunWithProgress` additionally fires one progress call.
- **Normal loop:** with `IsCalibrated()==true` and a Timer that advances a fixed amount
  per `ReadTSC()`, the loop runs until `budgetCycles` elapse, then stops. Assert: total
  iterations returned == (chunks executed × chunkSize); at least one chunk always runs;
  the loop stops on the first poll at-or-past budget (never spins forever).
- **Return value is total work done**, the loop's sole numeric promise — assert it
  equals chunkSize × number of kernel invocations the fake counted.
- **Progress callback cadence:** `RunWithProgress` calls `onProgress` once per chunk,
  with `elapsedUs` non-decreasing and `budgetUs` passed through unchanged; final
  `elapsedUs` ≥ budget when it exits via the budget path.
- *Avoid:* asserting an exact iteration count tied to specific cycle arithmetic beyond
  what the fake Timer makes deterministic — keep the fake's per-call increment simple
  (e.g. 1 µs-worth) so the expected count is derived from the contract, not reverse-
  engineered from the implementation.

### 3.4 `Stats` — min / max / average / sum

- Empty vector: all four return `0` (the documented sentinel) — explicit edge.
- Single element: min == max == average == sum == that element.
- Known multiset: `GetMin`/`GetMax` pick the extremes regardless of position
  (first, middle, last); `GetSum` is the total; `GetAverage` is integer `sum/size`
  (assert the truncation behaviour on a non-divisible sum — e.g. {1,2} → avg 1).
- Overflow awareness: document and test behaviour on values that sum past `UINT64`
  (this is a real risk with timing sums) — assert the *current* contract, and flag in
  the test if it wraps, so the behaviour is pinned and any future fix is intentional.

### 3.5 `AiSuitability::Evaluate` + `TierName` — feature → tier mapping

Pure function of a `CpuFeatures::Features` struct (construct the struct directly).

- AVX-512F **and** AVX-512VNNI → `Excellent`.
- AVX2 + FMA + **AVX-VNNI** (`HasAVXVNNI`) → `VeryGood`. (Note: AVX-VNNI is the
  third required flag — AVX2+FMA alone is only `Good`, not `VeryGood`.)
- AVX2 + FMA only (no AVX-VNNI) → `Good`.
- Anything less (no AVX2) → `Limited`.
- **Boundary/precedence:** a chip with AVX-512F but *not* VNNI does **not** reach
  Excellent (falls to whatever the lower flags grant) — assert this, it's the easy
  bug. FMA without AVX2, AVX2 without FMA → both `Limited`. AVX2+FMA+AVX-VNNI without
  AVX-512 → `VeryGood`, not `Excellent`.
- `TierName`/`TierSummary` return the right label/string per tier (assert the
  name→tier mapping exactly; `TierSummary` just needs to be non-empty and return
  a different string per tier — don't pin prose content).

### 3.6 AI score & category-composite maths

The AI scoring system lives across `Include/AiScore.h` (reference constants, weights),
`Source/Benchmarks/AiInt8Benchmark.*`, `AiInt4Benchmark.*`, `AiMemBenchmark.*`,
`AiCacheBenchmark.*`, and `Include/AiSuitability.h`. Test the **pure formula** only —
inject synthetic raw metrics without running any kernel.

**Reference system:** AMD Ryzen 9 5950X, 32 GB DDR4-3766 → **1000 AI pts per sub-test**.
`AI_SCORE_VERSION = 2` (bump when reference values or weights change).

**Per-sub-test normalization:**
```
NormScore = (raw / AI_REF_*) × 1000
```
- Raw == reference value → exactly 1000 pts.
- Raw == 2× reference → ~2000 pts (proportional).
- Raw == 0 → 0 pts (no divide-by-zero).

**Weighted composite:**
```
AIScore = INT8_score × 35 + INT4_score × 25 + Mem_score × 25 + Cache_score × 15
        (all divided by 100)
```
Weights sum to 100. Assert a hand-computed example where each sub-score is 1000 →
composite is exactly 1000. A heavier sub-test (INT8 at 35%) moves the composite
more than a lighter one (Cache at 15%) for the same delta — assert this.

**Category composite** (`IncludeInCategoryScore()`/`GetCategoryWeight()`):
- A benchmark returning `IncludeInCategoryScore()==false` is excluded from the
  composite; adding or removing one must leave the composite unchanged.
- `GetCategoryWeight()` defaults to 100; a benchmark returning 50 contributes half
  as much to the weighted average as one returning 100.

- *Avoid:* asserting the magic `AI_REF_*` constants themselves — those are
  calibration data, not behaviour. Test the *formula's response* to inputs.

### 3.7 `BigBuffer` — offset & partition maths (injected segments)

Inject a synthetic segment list (e.g. three segments of 100/50/200 bytes, total 350)
— no real allocation. Behaviours:

- `TotalSize()` == sum of segment sizes; `SegmentCount()` matches.
- `ByteAt(offset)` maps every offset to the correct physical address: offset 0 → seg0
  base; last byte of seg0 → seg0 base+size-1; first byte of seg1 → seg1 base; last
  valid offset → last segment's last byte. (Drive the *boundaries* — segment crossings
  are where prefix-sum logic breaks.)
- `SlotAddress(i)` == `ByteAt(i*64)`.
- `GetWorkerRange(w, total)` **partitions the whole footprint with no gaps and no
  overlap**: ranges are contiguous, union == `[0, TotalSize)` (rounded to 64-byte
  alignment), each aligned to a cache line, and worker `total-1` reaches the end.
  Property test: for several `total` values, sum of range lengths covers everything
  and no two ranges intersect. Single worker → whole buffer. `total` larger than
  there are cache lines → some workers get empty ranges, none get invalid ones.
- `GetSpans(start, end)` returns spans whose sizes sum to `end-start` and which lie
  within real segments; a range spanning a segment boundary splits into ≥2 spans;
  `maxSpans` cap is respected (returns ≤ cap, reports truncation per contract).
- *Avoid:* testing `Allocate()`/`Free()` here — those are Tier 3 (real firmware memory
  map). Only the mapping/partition maths is Tier 1.

### 3.8 `CoreSelection` — preset selection (injected roster)

Inject a synthetic `ApInfo[]` roster (mix of packages/cores/threads, some
`Available==false`). Behaviours:

- `SelectAll()` selects every **available** AP and none of the unavailable ones;
  `SelectedCount()` reflects that.
- `SelectPhysicalCoresOnly()` selects exactly one thread per physical core (Thread==0
  or the sole thread), never an SMT sibling — assert on a roster with HT pairs.
- `SelectOnePerPackage()` selects exactly one AP per distinct package.
- `GetSelectedIndices(out, cap)` writes the `ProcIndex` of each selected+available AP,
  returns the count, and never writes more than `cap` (truncation contract).
- An unavailable AP is never selected by any preset.
- `SetIncludeBsp`/`GetIncludeBsp` round-trip.
- *Avoid:* testing `Init()` (it reads MP Services) — Tier 2/3.

### 3.9 `ScrollViewport` — scroll state & key handling (recording fake Renderer)

State logic is pure; behaviours:

- Empty viewport: `TotalLines()==0`, `ScrollPos()==0`, `Render` draws nothing past the
  blank rows (recording fake confirms no content lines).
- `AddLine` overloads increment `TotalLines()`; content ≤ `MAX_LINES` (adding past the
  cap behaves per contract — assert whatever it promises: drop or ignore, not corrupt).
- `HandleKey` returns **true only for scroll keys** (Up/Down/PageUp/PageDown/Home/End)
  and false otherwise (so callers know whether the key was consumed) — the documented
  contract.
- Scroll clamping: Up at top stays at 0; Down at bottom stays at the last full page;
  `ScrollPos` is always within `[0, max(0, TotalLines-viewRows)]` after any key. Drive
  a sequence of keys and assert the invariant holds throughout.
- PageUp/PageDown move by `viewRows`; Home → 0; End → last page.
- `ScrollToLine(line)` adjusts scroll the minimum needed so `line` is within the
  visible window, and does nothing if it's already visible (cursor-following contract).
- `Render(startRow, viewRows)` draws exactly the visible slice in order, uses
  `DrawTextBg` for lines added with a background and `DrawText` otherwise, and clears
  trailing rows when content is shorter than the window. Assert via the recorded draw
  list — *which* lines and *which* draw call, i.e. behaviour, not pixels.
- *Avoid:* asserting pixel coordinates or colour packing — that's Renderer's job.

### 3.10 `BenchmarkRegistry` — registration & category bookkeeping

`Clear()` between tests for isolation (it's static global state — each test must reset).

- `Register` then `Count()`/`GetAll()` reflect the additions in registration order.
- The `MAX_BENCHMARKS` (32) cap: registering past it behaves per contract (drop extras,
  don't overflow) — assert `Count()` never exceeds the cap and earlier entries survive.
- **Category discovery** via the new API:
  - `GetCategoryCount()`/`GetCategoryName(i)` return unique category names in order of
    first appearance (register CPU, Memory, CPU, Stress → categories are CPU, Memory,
    Stress, in that order, no dupes). `GetCategoryName(GetCategoryCount())` is out of
    range — assert it returns null or an empty string, not garbage.
  - `GetBenchmarksInCategory("CPU", out, max)` returns only matching benchmarks, count
    correct, respects `maxCount` cap (truncation: when more benchmarks match than cap,
    returns exactly cap, none is invalid).
  - A category not present in registered benchmarks: `GetBenchmarksInCategory` returns
    0 count; `GetCategoryCount()` does not include it.
  - Registering multiple AI benchmarks then `GetBenchmarksInCategory("AI", ...)` returns
    all of them — drive this with the full `CPU` / `Memory` / `AI` / `Stress` split.
- Use **fake `IBenchmark`** instances (trivial subclasses returning canned
  name/category) — we test the registry's bookkeeping, not any real benchmark.

### 3.11 `RunConfig` — runtime-configurable budgets

`RunConfig` is pure logic (no hardware or firmware calls). Test through its public API.

- **Defaults:** `GetTestMinutes() == 3`; `GetStressMinutes() == 30` (after a process
  restart / before any `Set*` call — reset between tests if the implementation uses
  globals).
- **Budget maths:** `GetTestBudgetUs() == GetTestMinutes() * 60 * 1000000ULL`;
  `GetStressBudgetUs() == GetStressMinutes() * 60 * 1000000ULL`. Assert exact values
  for the defaults and for a non-default setting.
- **Round-trip:** `SetTestMinutes(10); GetTestMinutes() == 10`. Likewise for stress.
- **Clamping:** `SetTestMinutes(0)` and `SetTestMinutes(kMaxMinutes + 1)` stay within
  `[kMinMinutes, kMaxMinutes]` — assert `GetTestMinutes()` is clamped, never outside
  the documented range. Same for stress.
- *Avoid:* asserting the exact clamp direction or sentinel (e.g. "sets to min on
  underflow") — just assert the observable result is within `[kMinMinutes, kMaxMinutes]`.

### 3.12 `LongBenchmarkBase` — progress plumbing & rate-limiting

`LongBenchmarkBase` wraps `TryReportProgress()` which has extractable pure logic:
`ScoreDurationUs()` and the rate-limit gate. Test via a concrete subclass that
overrides `GetBudgetUs()`, `GetScore()`, `GetUnit()`, and `GetStatus()` to return
canned values (no real kernel), and sets a fake Timer.

- **`ScoreDurationUs()` contract:**
  - Before the first `TryReportProgress()` call (i.e. `mLastElapsedUs == 0`), returns
    `GetBudgetUs()` — so the caller gets the full-budget divisor, not zero.
  - After a `TryReportProgress(elapsedUs)` call, returns that `elapsedUs` on the next
    call — unconditional, regardless of whether the render was skipped by rate-limit or
    trylock.
- **`TryReportProgress()` fires the callback:**
  - When `IsCalibrated() == false`, rate-limiting is skipped — the callback fires
    every call (useful for tests that want unconditional firing).
  - When `IsCalibrated() == true` and the fake Timer is advanced past 500 ms-worth of
    cycles between calls: the callback fires.
  - When `IsCalibrated() == true` and the fake Timer has *not* advanced 500 ms since
    the last render: the callback is **suppressed** (not fired).
  - Assert: elapsed is published (via `ScoreDurationUs()`) whether or not the render
    was suppressed — rate-limiting only gates the callback, not the elapsed update.
- **`GetStatusNote()` / `GetIsaPath()` round-trips:** `SetNote("bad RAM")` →
  `GetStatusNote() == "bad RAM"`; `ClearNote()` → `GetStatusNote() == nullptr`.
  `SetIsa("AVX2")` → `GetIsaPath() == "AVX2"`.
- *Avoid:* testing the atomic trylock correctness (that requires actual concurrency);
  assert single-threaded: with no contention, `TryReportProgress()` always fires when
  the rate-limit allows.

### 3.13 `IBenchmark` — extended interface contracts

Test via fake subclasses that return canned values.

- **Default contracts:** `GetScore() == 0`, `GetUnit() == ""`, `GetBudgetUs() == 0`,
  `GetRawMetric() == 0`, `GetRawUnit() == ""`, `GetStatus() == nullptr`,
  `GetStatusNote() == nullptr`, `GetIsaPath() == nullptr`,
  `IncludeInCategoryScore() == true`, `GetCategoryWeight() == 100`. A benchmark that
  overrides none of these inherits all defaults — assert each default explicitly.
- **`IncludeInCategoryScore()` opt-out:** an integrity or stress-fault benchmark that
  returns `false` is distinguishable from one that returns `true`; the caller (category
  composite code) must exclude it. Test by building a category with one `true` and one
  `false` benchmark and asserting only the `true` one contributes to the composite.
- **`GetSweep()` default:** returns `0` and writes nothing — safe to call with a null
  or small output buffer. For a benchmark that implements a working-set sweep (e.g. L3
  Cache Cliff), the returned count ≤ `maxN`, the `sizesMB[]` entries are non-zero and
  in ascending order, and the `values[]` entries are non-zero (no silent nulls inside
  the returned count).
- **`ThreadingMode` values:** `SingleOnly`, `MultiOnly`, `Either` are distinct and
  map to their integer constants (the runner uses them as a branch condition; confirm
  no aliasing). Purely a static check, not a runtime test.
- **`RunMode` values:** `SingleCore`, `MultiCore`, `CoreCycle` are distinct. The runner
  uses `CoreCycle` to drive the per-core loop (§15 in plan.md); confirm the enum is
  available to the runner without a separate include.
- *Avoid:* testing that `RunCore()` default calls `Run()` (that's implementation);
  test via the behaviour contract that a benchmark that only overrides `Run()` still
  produces a result when dispatched through `RunCore()`.

### 3.14 `BenchmarkConstants` — shared constants

These are compile-time constants, not logic. Pin a small set to catch accidental edits:

- `US_PER_SECOND == 1000000ULL`.
- `PATTERN_ZEROS == 0`, `PATTERN_ONES == 0xFFFFFFFFFFFFFFFFULL`,
  `PATTERN_ALT_AA == 0xAAAAAAAAAAAAAAAAULL`, `PATTERN_ALT_55 == 0x5555555555555555ULL`.
- `PATTERN_ALT_AA ^ PATTERN_ALT_55 == PATTERN_ONES` (the two alternating patterns are
  bitwise complements — a property the stress test relies on to flip all bits).
- `LCG_KNUTH_A` and `LCG_KNUTH_C` satisfy the full-period LCG conditions: `A ≡ 1 (mod 4)`,
  `C` is odd. Assert these arithmetic properties rather than pinning the raw values —
  that way the test is self-documenting and catches a mis-paste.
- *Avoid:* asserting `LCG_KNUTH_A == 6364136223846793005` verbatim; the point is the
  mathematical property, not the decimal digits.

### 3.15 `DurationClass` — short/long benchmark classification

**Status: planned but not yet implemented.** The plan (§1.5a in `plan.md`) describes
a `DurationClass` enum and `GetDurationClass()` virtual on `IBenchmark`, plus a
`DurationClassName()` helper. As of the current codebase this enum is **absent from
`IBenchmark.h`** — the TUI grouping under "Short running" / "Long running" headers is
also not yet wired. When the implementation lands:

- `DurationClass::Short` and `DurationClass::Long` are distinct values.
- All original 5 short benchmarks return `Short` (the default); every new long
  benchmark overrides to `Long`.
- `DurationClassName(DurationClass::Short) == "Short running"`;
  `DurationClassName(DurationClass::Long) == "Long running"`.
- `DurationClass::Short != DurationClass::Long` (trivial, but pin it — the TUI
  branches on equality).
- *This section should be activated (tests written and enabled) once the enum lands in
  `IBenchmark.h`.*

---

## 4. Tier 2 — QEMU boot/integration smoke (scripted checklist)

Not unit tests; a **scripted manual/CI checklist** run against `make run` (QEMU +
OVMF, `-cpu max` so AVX2/AES/SHA/AVX-VNNI exist where supported). Asserts the wiring
the unit tests can't:

1. Image boots to the main menu without firmware error.
2. **Secure Boot** status line on the main menu shows "Signed" or "Not signed"
   correctly (test with and without the signing cert enrolled in OVMF).
3. `SelfCheck::Verify()` passes at boot (sentinels intact); UI shows no self-check
   warning banner.
4. Selection list renders with **Short running / Long running** groups (once
   implemented) and `[CPU]`/`[Memory]`/`[AI]`/`[Stress]` tags on each row.
5. Running **one short** benchmark completes and shows a result row with Score/Unit.
6. Running **one long** benchmark (e.g. FP Vector, 3-min budget) shows the live
   progress screen (name, elapsed/budget bar, current throughput) updating at least
   once every 2 seconds, then completes and shows a result row.
7. Running **one stress** benchmark shows the fault-count display (0 faults in QEMU).
8. Running **one AI benchmark** produces a non-zero AI Score; the AI Suitability
   screen shows the correct tier for `-cpu max` (expect `VeryGood` if OVMF exposes
   AVX-VNNI, `Good` if not).
9. **"Run All [Category]"** for CPU, Memory, AI, and Stress completes and transitions
   to the **Category Results screen** with a composite score.
10. Resolution-change menu lists modes and applies one live; theme cycling re-renders
    with the new palette.
11. Core-selection menu lists processors and toggles.
12. Timer reports calibrated / invariant-TSC status (or the documented warning under
    TCG where TSC may be non-invariant).
13. Graceful behaviour when a feature is **absent** (QEMU without a flag): the relevant
    benchmark reports "unsupported" via `GetStatusNote()` — **does not #UD/triple-fault**.
14. `RunConfig` settings (test budget, stress budget) persist in-memory: change the
    budget in the settings menu, run a long benchmark, confirm the live display shows
    the new duration.

Capture: this is where the `screenshots/` artifacts come from; a short script that
boots, sends keystrokes, and screenshots each screen would make it repeatable. Keep it
a documented checklist if scripting the input is too fiddly initially.

## 5. Tier 3 — on-hardware truth (manual checklist, 5800X)

Things only real silicon validates — **explicitly out of unit-test scope**:

- AVX2/FMA/AES-NI/SHA/CRC32/INT8/INT4 kernels execute without faulting after
  `EnableAvxState()` on BSP **and** every selected AP.
- Scores land in sane ranges vs. the documented 5800X references (GFLOP/s, GB/s, ns,
  AI pts). The 5950X is the calibration reference (1000 AI pts); the 5800X should
  score close to but below that.
- `BigBuffer` greedily captures ~85–90% of real free RAM across real segments and frees
  cleanly; memory integrity test verifies every byte and reports a **0 error count** on
  good RAM (and a non-zero count + address on bad/unstable RAM).
- Stress-test **fault containment**: a deliberately unstable OC produces caught,
  counted, on-screen faults (#GP/#UD/#PF…) without crashing the app — the headline
  bare-metal correctness property, untestable anywhere but hardware.
- `MachineCheck::PollLocal()` detects and counts corrected ECC errors (requires real
  MCi_STATUS MSRs); confirmed by running with marginal RAM.
- Multi-core dispatch (`StartupThisAP` per selected core), per-core score table (core-
  cycle mode), watchdog stays disabled across a full ~24-min CPU suite.
- `SelfCheck::SecureBootSigned()` returns true when Secure Boot is enrolled and the
  image is signed (test with the signing cert in the UEFI key database).

These get a **pre-release manual checklist**, not automation.

---

## 6. What we deliberately do **not** unit test (and why)

| Not unit tested | Why | Covered by |
|---|---|---|
| AVX/AES/CRC kernels | Behaviour *is* the ISA; nothing to fake | Tier 2/3 |
| INT8/INT4 GEMM kernels | Same — ISA + data-size dependent | Tier 2/3 |
| `EnableAvxState`, CR4/XCR0, IDT/exception handlers | Privileged hardware state | Tier 3 |
| `Timer::Calibrate`, real TSC | Needs real clock / `Stall()` | Tier 2/3 |
| `Renderer` blit / GOP / font raster pixels | Framebuffer-bound; visual | Tier 2 (screenshots) |
| `BigBuffer::Allocate/Free`, MP enumeration | Firmware memory map / MP Services | Tier 3 |
| `BenchmarkRunner` AP dispatch & timing | Real concurrency + firmware | Tier 2/3 |
| `MachineCheck::PollLocal()` | Reads MSRs — hardware only | Tier 3 |
| `SelfCheck::Verify()` | Requires `gBS` / `gImageHandle` | Tier 2/3 |
| `SelfCheck::SecureBootSigned()` | Reads UEFI NVRAM variables | Tier 2/3 |
| Stress kernel correctness (golden results) | Deterministic result needs real silicon timing | Tier 3 |

Where one of these wraps extractable pure logic (partition maths, presets, tier eval,
score formula, budget maths, rate-limiting), **that logic is pulled to a seam and
unit-tested**; the hardware call behind it is not.

---

## 7. Anti-patterns to keep out (the "behaviour not implementation" guardrails)

- **No asserting private state or internals.** Test through the public contract only.
- **No pinning growth policy, buffer reuse, or capacity sequences** — those are free to
  change. Pin *contents survive* and *bounds respected*.
- **No timing-dependent assertions.** All time is the fake Timer's simulated time.
- **No "golden string" tests on prose** (tier summaries, benchmark descriptions) beyond
  correct-selection + non-empty. They'll churn; the *mapping* is the behaviour.
- **One contract per test, named for the behaviour** (e.g.
  `"GetWorkerRange partitions whole buffer with no overlap"`), so a failure reads as a
  broken promise, not a broken line of code.
- **Reset global state** (`BenchmarkRegistry`, `CoreSelection`, `RunConfig`) in each
  test — isolation is part of correctness here.

---

## 8. Suggested phasing

1. **Harness bring-up:** `tests/` skeleton, vendor `doctest.h`, write `UefiShim`
   (heap + memory primitives), prove `Include/UefiTypes.h` compiles on the host, and
   get one trivial `Freestanding` test green via `make test`. This de-risks the whole
   approach before writing breadth.
2. **Pure, dependency-free units:** Freestanding, Vector, Stats, AiSuitability,
   BenchmarkConstants, RunConfig. Highest value/effort ratio; no seams needed.
3. **Fake-Timer units:** TimeBox (Run + RunWithProgress), LongBenchmarkBase
   (ScoreDurationUs + rate-limiting).
4. **Injected-data units:** BigBuffer maths, CoreSelection presets, AI score formula,
   IBenchmark extended defaults.
5. **Recording-Renderer units:** ScrollViewport; **registry** bookkeeping including
   the new category enumeration API (`GetCategoryCount`/`GetCategoryName`/
   `GetBenchmarksInCategory`).
6. **DurationClass:** activate §3.15 tests once `DurationClass` lands in `IBenchmark.h`.
7. **CI:** run `make test` on every push (host job — no QEMU needed); keep the Tier 2/3
   checklists in this file for release gating.

The pay-off ordering front-loads the logic most likely to harbour off-by-one and
boundary bugs (formatting, partitioning, scroll clamping, tier precedence) — exactly
the behaviour that silently corrupts a result table or starves a firmware allocator,
and exactly the behaviour a host unit test can pin precisely.
