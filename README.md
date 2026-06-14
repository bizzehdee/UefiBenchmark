# UEFI Benchmark Suite

A freestanding C++ UEFI application that benchmarks CPU, memory, and AI readiness — and stress-tests overclocked systems — directly on bare metal, with no operating system required.

## Features

- **UEFI GOP graphics** — 1024×768 default; live resolution switching from the main menu; integer font scaling for 1080p
- **ConOut text-mode fallback** — works even when GOP is unavailable
- **TSC timer** — RDTSC-based timing calibrated against UEFI `Stall()`, with invariant-TSC detection and CPUID serialisation fences
- **Multi-core dispatch** — sends benchmarks to all APs via UEFI MP Services Protocol; graceful fallback to single-core when unavailable
- **Core-cycle mode** — runs each benchmark sequentially on every selected core, producing a per-core score table and highlighting outlier cores
- **User-selectable cores** — choose exactly which physical cores participate in multi-core and core-cycle runs
- **Per-benchmark threading mode** — each benchmark declares `SingleOnly`, `MultiOnly`, or `Either`; user toggles mode in the selection screen
- **Multi-run statistics** — configure 1–10 runs per benchmark with per-run min/max/avg
- **TimeBox runner** — time-budgeted execution engine (benchmarks run to a wall-clock target, not a fixed iteration count)
- **Live progress display** — real-time progress bar, elapsed/budget time, and current score updated at ~2 Hz during long benchmarks
- **BigBuffer allocator** — greedy whole-RAM capture (~85–90% of free memory) across discontiguous segments; worker-aware partitioning with cache-line alignment
- **CPU feature detection** — CPUID-based detection of SSE2, SSE4.2, AVX, AVX2, FMA, AES-NI; on-demand AVX state enablement on BSP and APs
- **Multiple colour themes** — Dark (default), Light, High-Contrast Dark; live switching from the main menu
- **AI Readiness scoring** — normalised score against AMD Ryzen 7 5800X reference; estimated LLM tokens/sec for 7B/14B/32B Q4 models
- **Extensible** — add new benchmarks by implementing `IBenchmark` and registering in `Main.cpp`; categories are discovered dynamically

## Benchmark Suites

### CPU Suite (~24 min, 8–9 time-boxed tests)

| Benchmark | What it measures | ISA | Score |
|-----------|-----------------|-----|-------|
| Int Throughput (ILP) | Peak integer IPC — 8 independent ALU chains | Baseline | MOPS |
| Int Latency (Serial) | Per-op serial dependency chain latency | Baseline | MOPS |
| FP Scalar + Divide | Scalar FPU incl. divider and `sqrt` | SSE2 | MFLOP/s |
| FP Vector (AVX2/FMA) | 256-bit FMA throughput — 10 YMM accumulators | AVX2+FMA | GFLOP/s |
| Branch Prediction | Data-dependent branches on random bytes | Baseline | Mbranch/s |
| AES-NI Encryption | AES-128 round chaining via `_mm_aesenc` | AES-NI | MB/s |
| Hash / CRC32 | `_mm_crc32_u64` stream over cache-resident block | SSE4.2 | MB/s |
| Mandelbrot (FP+Branch) | Mixed FP + data-dependent escape-time loop | SSE2 | Miter/s |

All CPU benchmarks are time-boxed (default 150–180 s each) and run multi-core.

### Memory Suite (~6–10 min, 5 tests)

All memory benchmarks operate on the whole of available RAM via `BigBuffer`.

| Benchmark | What it measures | Threading | Score |
|-----------|-----------------|-----------|-------|
| Mem Sequential Write | Non-temporal stream stores; true DRAM write BW | Multi | MB/s |
| Mem Sequential Read | Stream loads + reduce; DRAM read BW | Multi | MB/s |
| Mem Copy Bandwidth | Read+write copy (STREAM triad); combined BW | Multi | MB/s |
| Mem Random Latency | Pointer-chase across all RAM; full DRAM latency | Single | ns/access |
| Mem Integrity (March) | Write+verify 0x00/0xFF/0xAA/0x55 over all RAM | Multi | MB/s + errors |

### AI Readiness Suite (~6 min, 4 tests)

Scores are normalised against AMD Ryzen 7 5800X + 64 GB DDR4-3200 = 1000 points per test.

| Benchmark | What it measures | Score |
|-----------|-----------------|-------|
| AI INT8 Matrix | INT8×INT8→INT32 GEMM throughput (1K–4K matrices) | INT8 GOPS |
| AI INT4 Matrix | Packed 4-bit multiply-accumulate (approximates Q4 LLM weights) | INT4 GOPS |
| AI Memory Bandwidth | Sequential + random + mixed-stride reads at 64–4096 B blocks | GB/s |
| AI Cache Behaviour | Pointer-chase at 512 KB–64 MB working sets; L2/L3/miss latency | Score/1000 |

The suite produces a weighted **AI Readiness Score** and estimated LLM throughput for 7B, 14B, and 32B Q4 models.

### Stress Suite (30 min per test, user-adjustable)

Targets overclock validation. Runs to a configurable budget regardless of errors — faults are counted and displayed live, not used to abort.

| Benchmark | What it stresses | Score |
|-----------|-----------------|-------|
| Stress: Mem Clock Soak | Sequential write+verify across all RAM; exposes marginal data-clock stability | Errors (0 = stable) |
| Stress: Mem Latency Soak | Large-stride (64 KB) write+verify; maximises DRAM row activations to stress timing margins | Errors (0 = stable) |
| Stress: CPU Power Virus | AVX2+FMA all-core heat soak; monitors sustained throughput under thermal load | GFLOP/s |
| Stress: CPU Compute Verify | 1M-step deterministic LCG chain vs. golden value on every core; any deviation = marginal Vcore or clock | Errors (0 = stable) |

## Score Metrics

| Metric | Unit | Higher is better |
|--------|------|-----------------|
| MOPS | Million operations/sec | Yes |
| MFLOP/s | Megaflops/sec | Yes |
| GFLOP/s | Gigaflops/sec | Yes |
| MB/s | Megabytes/sec | Yes |
| Mbranch/s | Million branches/sec | Yes |
| Miter/s | Million iterations/sec | Yes |
| ns/access | Nanoseconds per access | No (lower = better) |
| Errors | Count of detected faults | No (0 = stable) |

## Building

The Makefile auto-detects the host platform. On Linux it uses the native LLVM toolchain (`clang++` + `lld-link`); on Windows it expects the MSYS2 **MINGW64** shell. Run `make help` to see detected settings.

### Debian / Ubuntu

```bash
sudo apt update
sudo apt install clang lld llvm make \
                 mtools dosfstools ovmf qemu-system-x86 genisoimage
make
make qemu   # test in QEMU
make iso    # optional: bootable UEFI ISO
```

### Fedora

```bash
sudo dnf install clang lld llvm make \
                  mtools dosfstools edk2-ovmf qemu-system-x86 genisoimage
make
make qemu
make iso
```

### Windows (MSYS2 / MinGW64)

1. Install [MSYS2](https://www.msys2.org/) and open the **MINGW64** shell.
2. Install packages:

```bash
pacman -S mingw-w64-x86_64-gcc make
# Optional — QEMU testing:
pacman -S mingw-w64-x86_64-qemu mingw-w64-x86_64-edk2-ovmf mtools
# Optional — ISO creation:
pacman -S xorriso
```

3. Build:

```bash
make
```

> **Note:** Windows builds require the **MINGW64** shell so that `g++`/`ld`/`objcopy` produce PE/COFF output.

### EDK II (any platform)

```bash
git clone https://github.com/tianocore/edk2.git
cd edk2 && git submodule update --init
source edksetup.sh
cp -r /path/to/UefiBenchmark ./UefiBenchmarkPkg
build -p UefiBenchmarkPkg/UefiBenchmark.dsc -a X64 -t GCC5 -b RELEASE
```

### Toolchain overrides

```bash
make CXX=clang++ LD=lld-link OBJCOPY=llvm-objcopy
make qemu OVMF=/path/to/OVMF.fd
```

## Running

Build output is `Build/UefiBenchmark.efi` — a PE32+ UEFI application.

| Method | Command / Steps |
|--------|----------------|
| QEMU | `make qemu` — creates a FAT32 disk image and boots with OVMF |
| ISO | `make iso` — bootable UEFI ISO for VMs or optical boot flows |
| USB stick | Format FAT32, copy `.efi` to `EFI/BOOT/BOOTX64.EFI` |
| VM | Attach the FAT32 image or ISO at boot |

## Screenshots

**Main Menu** — Launch a category suite, select individual benchmarks, or access system info and settings.

![Main Menu](screenshots/1.png)

**System Information** — CPU details, memory configuration (SPD timings, speed, channels), and the registered benchmark list.

![System Information](screenshots/2.png)

**Benchmark Selection** — Choose individual benchmarks, toggle single-core / multi-core / core-cycle mode per benchmark.

![Benchmark Selection](screenshots/3.png)

**Progress Display** — Live progress bar, elapsed/budget time, current score, and core count updated during a long benchmark.

![Running Benchmarks](screenshots/4.png)

**Results Summary** — Per-benchmark scores, min/max/avg run times, and per-core breakdown for core-cycle runs.

![Benchmark Results](screenshots/5.png)

**High Contrast Theme** — High-contrast dark palette for improved visibility.

![High Contrast Mode](screenshots/6.png)

**Core Selection** — Select exactly which physical cores participate in multi-core and core-cycle runs.

![Core Selection](screenshots/7.png)

**Resolution Selection** — Switch display resolution live from the main menu.

![Resolution Selection](screenshots/8.png)

## Adding a New Benchmark

Long-running benchmarks should extend `LongBenchmarkBase` (provides the progress callback, rate-limiting, and atomic render lock). Short-to-medium tests can implement `IBenchmark` directly.

```cpp
// Source/Benchmarks/MyBenchmark.h
#pragma once
#include "LongBenchmarkBase.h"

class MyBenchmark : public LongBenchmarkBase {
public:
    const char* GetName()        const override { return "My Benchmark"; }
    const char* GetDescription() const override { return "What it measures"; }
    const char* GetCategory()    const override { return "CPU"; }

    UINT64 GetBudgetUs() const override { return 180ULL * 1000000; } // 3 min
    UINT64      GetScore() const override { return mIter / GetBudgetUs(); }
    const char* GetUnit()  const override { return "MOPS"; }

    void PreRun()  override { mIter = 0; }
    void Run()     override { RunCore(0, 1); }
    void RunCore(UINT32 workerIndex, UINT32 totalWorkers) override;

private:
    volatile UINT64 mIter = 0;
};
```

```cpp
// Source/Benchmarks/MyBenchmark.cpp
#include "MyBenchmark.h"
#include "TimeBox.h"

void MyBenchmark::RunCore(UINT32, UINT32) {
    UINT64 local = TimeBox::RunWithProgress(GetBudgetUs(), 100000,
        [](UINT64 n) { /* kernel */ },
        [this](UINT64 e, UINT64) { TryReportProgress(e); });
    __atomic_fetch_add(const_cast<UINT64*>(&mIter), local, __ATOMIC_RELAXED);
}
```

Register in `Source/Main.cpp`, add the `.cpp` to `SOURCES` in the Makefile, and add it to `[Sources]` in `UefiBenchmark.inf`. The category appears automatically in the main menu.

## Design Notes

- **No exceptions / no RTTI** — compiled with `-fno-exceptions -fno-rtti -ffreestanding`
- **All benchmarks are time-boxed** — scores are throughput (work/time), not time-to-finish; total suite runtime is deterministic regardless of hardware speed
- **Multi-core via MP Services** — blocking `StartupAllAPs` dispatches to all APs; BSP orchestrates and renders; atomic worker index for compact 0-based per-core IDs
- **Core-cycle mode** — sequential `StartupThisAP` calls, one core at a time; per-core min/avg/max; outlier cores highlighted at >5% deviation from median
- **Per-file ISA flags** — only the files that use AVX2/FMA/AES/SSE4.2 are compiled with those flags; startup and renderer code stays SSE2-only so AVX instructions cannot appear before `EnableAvxState()`
- **Memory benchmark partitioning** — each AP gets a cache-line-aligned slice of the `BigBuffer` segment list; no false sharing
- **Stress tests** — count errors and continue to end of budget; do not abort on first fault; error count is the headline result
- **Pixel format aware** — detects RGB vs BGR from GOP and renders correctly
- **TSC quality** — checks invariant TSC bit (CPUID 0x80000007:EDX[8]); warns if absent
- **Memory map** — reports available (`EfiConventionalMemory`) pages, not installed RAM

## Constraints

- APs must not call UEFI Boot Services — only pure computation in `RunCore`
- Max 32 registered benchmarks (static registry array)
- Watchdog timer disabled at startup to prevent 5-minute firmware reset
- No filesystem output — results displayed on-screen only
- `BigBuffer` targets ~85–90% of free RAM; leave headroom for firmware/stack/GOP framebuffer
