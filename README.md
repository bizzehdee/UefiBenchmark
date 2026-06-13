# UEFI Benchmark Suite

A freestanding C++ UEFI application that benchmarks CPU and memory performance directly on bare metal — no operating system required.

## Features

- **UEFI GOP graphics** — 800×600 framebuffer with 8×16 bitmap font (100×37 text grid)
- **ConOut text-mode fallback** — works even when GOP is unavailable
- **TSC timer** — RDTSC-based timing calibrated against UEFI Stall(), with invariant-TSC detection and CPUID serialisation fences
- **Multi-core support** — dispatches benchmarks to all APs via UEFI MP Services Protocol; graceful fallback to single-core when unavailable
- **Per-benchmark threading mode** — each benchmark declares SingleOnly, MultiOnly, or Either; user toggles mode in the selection screen
- **Multi-run benchmarks** — configure 1–10 runs per benchmark with per-run min/max/avg statistics
- **Keyboard-driven TUI** — arrow keys, Enter, Space, Esc navigation
- **Extensible** — add new benchmarks by implementing `IBenchmark` and registering in `Main.cpp`

## Included Benchmarks

### CPU Benchmarks

| Benchmark | Description | Score Metric | Duration |
|-----------|-------------|--------------|----------|
| Integer Arithmetic | Add/Mul/Div/XOR throughput with dependency chain | MOPS | Fixed iterations |
| Int Latency (Serial) | Single-core serial dependency chain (stresses core latency) | MOPS | Fixed iterations |
| Int Throughput (ILP) | 8 independent 64-bit add/shift/and/xor chains (fills ALU ports) | MOPS | 180s time-boxed |
| Pi (Scalar) | Leibniz series, 100M terms, scalar double (no SIMD) | MFLOP/s | Fixed iterations |
| Pi (SIMD/SSE2) | Leibniz series, 100M terms, SSE2 packed doubles (2 per cycle) | GFLOP/s | Fixed iterations |
| FP Scalar + Divide | Mixed scalar add/mul/div on doubles (stresses FP divider) | MFLOP/s | 180s time-boxed |
| FP Vector (AVX2/FMA) | AVX2 FMA operations on packed doubles | GFLOP/s | 180s time-boxed |
| AES-NI Encryption | AES-128 round chaining via `_mm_aesenc` (measures AES unit throughput) | MB/s | 180s time-boxed |
| Hash / CRC32 | CRC32 hashing via `_mm_crc32_u64` over cache-resident block | MB/s | 150s time-boxed |
| Branch Prediction | Data-dependent branches on random bytes (stresses branch predictor) | Mbranch/s | 150s time-boxed |
| Mandelbrot (FP+Branch) | Escape-time per-pixel FP loop (mixes FP units with branch predictor) | Miter/s | 180s time-boxed |

### Memory Benchmarks

| Benchmark | Description | Score Metric | Duration |
|-----------|-------------|--------------|----------|
| Mem Sequential Read | Sequential reads across whole RAM | MB/s | Multi-pass |
| Mem Sequential Write | Non-temporal stream stores across whole RAM | MB/s | Multi-pass |
| Memory Random | Random 4-byte reads over 32 MB buffer (pre-computed indices) | MB/s | Fixed iterations |
| Mem Random Latency | Pointer-chase across all RAM (measures full-DRAM random latency) | ns/access | Single-core |
| Mem Integrity (March) | Write+verify 0x00/0xFF/0xAA/0x55 patterns over all RAM | MB/s verified + Error count | Multi-core |

## Benchmark Score Criteria

- **MOPS** (Million OPerations/Second) — higher is better; integer throughput or latency-bound chains
- **GFLOP/s** (GigaFLOPs/Second) — higher is better; vector floating-point throughput
- **MFLOP/s** (MegaFLOPs/Second) — higher is better; scalar floating-point throughput  
- **MB/s** (MegaBytes/Second) — higher is better; memory or encryption throughput
- **Mbranch/s** (Million branches/Second) — higher is better; branch throughput
- **Miter/s** (Million iterations/Second) — higher is better; algorithm-dependent iterations
- **ns/access** (nanoseconds/access) — lower is better; memory latency
- **Error count** — lower is better; memory integrity errors detected

### Benchmark Modes

- **Time-boxed** — runs for a fixed duration (150–180 seconds) and counts total iterations; score = iterations / time
- **Fixed iterations** — runs for a fixed iteration count and reports timing
- **Multi-pass** — iterates until convergence or a fixed number of passes over RAM
- **Single-core** — runs on primary processor only (no multi-threading)
- **Multi-core** — dispatches to all available processor cores via UEFI MP Services Protocol

## Building

The Makefile auto-detects the host platform and selects the appropriate toolchain. On Linux it uses the native LLVM toolchain (`clang++` + `lld-link`); on Windows it expects the MSYS2 **MINGW64** shell. Run `make help` to see detected settings.

### Debian / Ubuntu

```bash
# Install native LLVM toolchain + QEMU/ISO test tools
sudo apt update
sudo apt install clang lld llvm make \
                 mtools dosfstools ovmf qemu-system-x86 genisoimage

# Build
make

# Test in QEMU
make qemu

# Optional: build a bootable UEFI ISO
make iso
```

### Fedora

```bash
# Install native LLVM toolchain + QEMU/ISO test tools
sudo dnf install clang lld llvm make \
                  mtools dosfstools edk2-ovmf qemu-system-x86 genisoimage

# Build
make

# Test in QEMU (Fedora puts OVMF in a different path — auto-detected)
make qemu

# Optional: build a bootable UEFI ISO
make iso
```

### Windows (MSYS2 / MinGW64)

1. Install [MSYS2](https://www.msys2.org/)
2. Open the **MINGW64** shell (not MSYS or UCRT64)
3. Install packages:

```bash
pacman -S mingw-w64-x86_64-gcc make
# Optional — for QEMU testing:
pacman -S mingw-w64-x86_64-qemu mingw-w64-x86_64-edk2-ovmf mtools
# Optional — for ISO creation:
pacman -S xorriso
```

4. Build:

```bash
make
# Optional: make qemu
# Optional: make iso
```

> **Note:** Windows builds require the **MINGW64** shell so that `g++`/`ld`/`objcopy` produce PE/COFF output. The MSYS shell targets Cygwin and will not produce a valid UEFI binary.

### EDK II (any platform)

```bash
# 1. Clone EDK II and set up the build environment
git clone https://github.com/tianocore/edk2.git
cd edk2 && git submodule update --init
source edksetup.sh    # or edksetup.bat on Windows

# 2. Copy this project into the workspace
cp -r /path/to/UefiBenchmark ./UefiBenchmarkPkg

# 3. Build
build -p UefiBenchmarkPkg/UefiBenchmark.dsc -a X64 -t GCC5 -b RELEASE
# On Windows with VS2022: -t VS2022
```

### Toolchain override

```bash
# Linux native LLVM toolchain (default)
make CXX=clang++ LD=lld-link OBJCOPY=llvm-objcopy

# Custom OVMF path
make qemu OVMF=/path/to/OVMF.fd
```

## Running

The output is a `UefiBenchmark.efi` PE32+ executable. To boot it:

1. **QEMU**: `make qemu` creates a FAT32 disk image and boots with OVMF
2. **ISO**: `make iso` creates a bootable UEFI ISO for VMs and optical-media style boot flows
3. **USB stick**: Format a USB drive as FAT32, copy the .efi to `EFI/BOOT/BOOTX64.EFI`
4. **VM**: Attach a FAT32 virtual disk or the ISO at boot

## Adding a New Benchmark

1. Create a header and source file in `Source/Benchmarks/`:

```cpp
// Source/Benchmarks/MyBenchmark.h
#pragma once
#include "IBenchmark.h"

class MyBenchmark : public IBenchmark {
public:
    const char* GetName() const override        { return "My Benchmark"; }
    const char* GetDescription() const override { return "What it measures"; }
    const char* GetCategory() const override    { return "CPU"; }
    void Run() override;
};
```

```cpp
// Source/Benchmarks/MyBenchmark.cpp
#include "MyBenchmark.h"

void MyBenchmark::Run() {
    volatile int sink = 0;
    for (int i = 0; i < 1000000; ++i)
        sink += i;
}
```

2. Register it in `Source/Main.cpp`:

```cpp
#include "Benchmarks/MyBenchmark.h"
static MyBenchmark sMyBench;
// In EfiMain():
BenchmarkRegistry::Register(&sMyBench);
```

3. Add the .cpp to the `SOURCES` list in the `Makefile` and the `[Sources]` section in `UefiBenchmark.inf`.

## Project Structure

```
Include/
  UefiTypes.h           Minimal self-contained UEFI type definitions (x86-64)
  Freestanding.h        Vector<T>, operator new/delete, memset/memcpy, string utils
  IBenchmark.h          Abstract benchmark interface
  BenchmarkResult.h     Per-benchmark result with multi-run timing data
  BenchmarkRegistry.h   Static fixed-capacity registry (max 32 benchmarks)
  BenchmarkRunner.h     Sequential runner with configurable run count
  Statistics.h          Min / Max / Average / Sum helpers
  Timer.h               TSC-based stopwatch with UEFI Stall() calibration
  SystemInfo.h          CPUID + GetMemoryMap detection
  BitmapFont.h          8×16 bitmap font renderer
  Renderer.h            GOP framebuffer + character-grid API + ConOut fallback
  ColorTheme.h          Colour palette (dark theme, fully customisable)
  Tui.h                 TUI manager (menus, results, system info)

Source/
  Main.cpp              EfiMain entry point — init, register benchmarks, launch TUI
  Freestanding.cpp      Memory primitives, operator new/delete, string utils
  Timer.cpp             TSC calibration (3 samples, median, CPUID fence)
  SystemInfo.cpp        CPUID vendor/brand/topology + UEFI memory map
  BitmapFont.cpp        Font bitmap data (ASCII 32–126)
  Renderer.cpp          GOP init, pixel format detection, back-buffer, text drawing
  BenchmarkRegistry.cpp Registry implementation
  BenchmarkRunner.cpp   Run engine with progress display
  Tui.cpp               Full TUI: main menu, selection, run picker, results, sysinfo
  Benchmarks/
    *.h / *.cpp         CPU and memory benchmarks (see Included Benchmarks section)

UefiBenchmark.inf     EDK II module definition
UefiBenchmark.dsc     EDK II platform descriptor
Makefile              MinGW cross-compiler build + QEMU test target
```

## Design Notes

- **No exceptions / no RTTI** — compiled with `-fno-exceptions -fno-rtti`
- **Multi-core via MP Services** — blocking `StartupAllAPs` dispatches to all APs; BSP orchestrates only (portable across firmware); atomic worker index for compact 0-based per-core IDs
- **Memory benchmark partitioning** — in multi-core mode, each AP gets a cache-line-aligned slice of the buffer (no false sharing)
- **Pixel format aware** — detects RGB vs BGR from GOP and renders correctly
- **TSC quality** — checks invariant TSC bit (CPUID 0x80000007:EDX[8]), warns if absent
- **Memory map** — reports available (EfiConventionalMemory) pages, not installed RAM
- **CPUID fallback chain** — tries leaf 0x1F → 0x0B → 0x01 for core count

## Constraints

- Single-threaded BSP when no MP Services available (graceful fallback)
- APs must NOT call UEFI Boot Services (not reentrant) — only pure computation in RunCore
- No filesystem output (results displayed on-screen only)
- Max 32 registered benchmarks (static array, no heap for registry)
- Watchdog timer disabled at startup to prevent 5-minute reset
