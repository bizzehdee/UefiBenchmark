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

| Benchmark | Category | Description |
|-----------|----------|-------------|
| Integer Arithmetic | CPU | Add/mul/div/xor dependency chain (5M iterations) |
| Pi (Scalar) | CPU | Leibniz series, 100M terms, scalar double (no SIMD) |
| Pi (SIMD/SSE2) | CPU | Leibniz series, 100M terms, SSE2 packed doubles (2 per cycle) |
| Memory Sequential | Memory | Sequential R/W over 32 MB page-aligned buffer |
| Memory Random | Memory | 1M random 4-byte reads over 32 MB buffer (pre-computed indices) |

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
    CpuBenchmark.h/.cpp      Integer arithmetic with dependency chain
    MemoryBenchmark.h/.cpp    Sequential + random memory access (32 MB, page-aligned)
    PiBenchmark.h/.cpp        Pi via Leibniz series — scalar vs SSE2 SIMD

UefiBenchmark.inf     EDK II module definition
UefiBenchmark.dsc     EDK II platform descriptor
Makefile              MinGW cross-compiler build + QEMU test target
```

## Design Notes

- **No STL** — `Vector<T>` is POD-only, backed by UEFI `AllocatePool`
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
