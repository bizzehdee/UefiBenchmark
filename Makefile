# ── UefiBenchmark Makefile ────────────────────────────────────
# Builds the UEFI benchmark tool as a PE32+ .efi application.
#
# Supported hosts:
#   - Linux with LLVM/LLD (native host toolchain)
#   - Windows with MSYS2/MinGW64
#
# Usage:
#   make              # auto-detects platform, builds UefiBenchmark.efi
#   make disk         # build + create bootable FAT32 disk image
#   make qemu         # build + boot in QEMU (requires OVMF + mtools)
#   make clean        # remove build artifacts
#   make help         # show all targets and detected settings
#
# Override toolchain:
#   make CXX=clang++ LD=lld-link OBJCOPY=llvm-objcopy
#
# For EDK II builds, use the .inf/.dsc files instead.
# ──────────────────────────────────────────────────────────────

# ── Platform detection ────────────────────────────────────────
# On Windows (MSYS2/MinGW64), $(OS) is "Windows_NT" and the shell-provided
# MinGW toolchain already targets PE/COFF natively.
# On Linux, use the host LLVM toolchain and target PE/COFF explicitly.
ifeq ($(OS),Windows_NT)
  PLATFORM := windows
  DEFAULT_CXX := g++
  DEFAULT_LD := ld
  DEFAULT_OBJCOPY := objcopy
  CXX_TARGET_FLAGS :=
  LINK_OUTPUT = $(BUILDDIR)/UefiBenchmark.dll
else
  PLATFORM := linux
  DEFAULT_CXX := clang++
  DEFAULT_LD := lld-link
  DEFAULT_OBJCOPY := llvm-objcopy
  CXX_TARGET_FLAGS := --target=x86_64-unknown-windows
  LINK_OUTPUT = $(TARGET)
endif

# ── Toolchain ─────────────────────────────────────────────────
ifneq ($(filter default undefined,$(origin CXX)),)
  CXX := $(DEFAULT_CXX)
endif
ifneq ($(filter default undefined,$(origin LD)),)
  LD := $(DEFAULT_LD)
endif
ifneq ($(filter default undefined,$(origin OBJCOPY)),)
  OBJCOPY := $(DEFAULT_OBJCOPY)
endif

# ── Directories ───────────────────────────────────────────────
SRCDIR   = Source
INCDIR   = Include
BUILDDIR = Build
BMDIR    = $(SRCDIR)/Benchmarks

# ── Output ────────────────────────────────────────────────────
TARGET   = $(BUILDDIR)/UefiBenchmark.efi

# ── Sources ───────────────────────────────────────────────────
SOURCES  = \
	$(SRCDIR)/Freestanding.cpp \
	$(SRCDIR)/Timer.cpp \
	$(SRCDIR)/SystemInfo.cpp \
	$(SRCDIR)/BitmapFont.cpp \
	$(SRCDIR)/Renderer.cpp \
	$(SRCDIR)/BenchmarkRegistry.cpp \
	$(SRCDIR)/BenchmarkRunner.cpp \
	$(SRCDIR)/Tui.cpp \
	$(BMDIR)/CpuBenchmark.cpp \
	$(BMDIR)/MemoryBenchmark.cpp \
	$(BMDIR)/PiBenchmark.cpp \
	$(SRCDIR)/Main.cpp

OBJECTS  = $(patsubst %.cpp,$(BUILDDIR)/%.o,$(notdir $(SOURCES)))
DEPENDS  = $(OBJECTS:.o=.d)

# ── Compiler flags ────────────────────────────────────────────
CXXFLAGS = -std=c++17 \
           $(CXX_TARGET_FLAGS) \
           -ffreestanding \
           -fno-exceptions \
           -fno-rtti \
           -fno-stack-protector \
           -fno-strict-aliasing \
           -fno-builtin \
           -mno-red-zone \
           -Wall -Wextra -Wpedantic \
           -I$(INCDIR) -I$(SRCDIR) \
           -MMD -MP \
           -DEFI_FUNCTION_WRAPPER

ifeq ($(PLATFORM),windows)
  LDFLAGS = --subsystem 10 \
            --entry EfiMain \
            -nostdlib
else
  LDFLAGS = /subsystem:efi_application \
            /entry:EfiMain \
            /nodefaultlib \
            /dll \
            /machine:x64
endif

# ── OVMF firmware auto-detection ─────────────────────────────
# Override with:  make qemu OVMF=/path/to/OVMF.fd
ifndef OVMF
  # Debian / Ubuntu
  ifneq ($(wildcard /usr/share/OVMF/OVMF.fd),)
    OVMF = /usr/share/OVMF/OVMF.fd
  # Fedora
  else ifneq ($(wildcard /usr/share/edk2/ovmf/OVMF_CODE.fd),)
    OVMF = /usr/share/edk2/ovmf/OVMF_CODE.fd
  # Arch Linux
  else ifneq ($(wildcard /usr/share/edk2-ovmf/x64/OVMF.fd),)
    OVMF = /usr/share/edk2-ovmf/x64/OVMF.fd
  # MSYS2 (pacman -S mingw-w64-x86_64-edk2-ovmf)
  else ifneq ($(wildcard /mingw64/share/edk2-ovmf/OVMF.fd),)
    OVMF = /mingw64/share/edk2-ovmf/OVMF.fd
  # Fallback: current directory
  else
    OVMF = OVMF.fd
  endif
endif

DISKIMG  = $(BUILDDIR)/disk.img
EFIBOOTIMG = $(BUILDDIR)/efiboot.img
ISOIMG   = $(BUILDDIR)/UefiBenchmark.iso
ISOROOT  = $(BUILDDIR)/iso-root

ISO_TOOL := $(shell if command -v xorriso >/dev/null 2>&1; then echo xorriso; \
                   elif command -v genisoimage >/dev/null 2>&1; then echo genisoimage; \
                   elif command -v mkisofs >/dev/null 2>&1; then echo mkisofs; fi)

# ── Rules ─────────────────────────────────────────────────────
.PHONY: all clean disk iso qemu help check-toolchain check-iso-tools

all: check-toolchain $(TARGET)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

# Compile .cpp → .o  (Source/)
$(BUILDDIR)/%.o: $(SRCDIR)/%.cpp | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# Compile .cpp → .o  (Source/Benchmarks/)
$(BUILDDIR)/%.o: $(BMDIR)/%.cpp | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# Validate the selected toolchain before building.
ifeq ($(PLATFORM),windows)
check-toolchain:
	@test "$(MSYSTEM)" = "MINGW64" || { echo "Error: Windows builds require the MSYS2 MINGW64 shell."; \
		echo "Open the MINGW64 shell, then install mingw-w64-x86_64-gcc and make."; \
		exit 1; }
	@command -v "$(CXX)" >/dev/null 2>&1 || { echo "Error: Compiler not found: $(CXX)"; exit 1; }
	@command -v "$(LD)" >/dev/null 2>&1 || { echo "Error: Linker not found: $(LD)"; exit 1; }
	@command -v "$(OBJCOPY)" >/dev/null 2>&1 || { echo "Error: Objcopy not found: $(OBJCOPY)"; exit 1; }
else
check-toolchain:
	@command -v "$(CXX)" >/dev/null 2>&1 || { echo "Error: Compiler not found: $(CXX)"; \
		echo "Install LLVM/Clang (clang++, lld-link, llvm-objcopy) or override the toolchain variables."; \
		exit 1; }
	@command -v "$(LD)" >/dev/null 2>&1 || { echo "Error: Linker not found: $(LD)"; \
		echo "Install LLD (lld-link) or override LD."; \
		exit 1; }
endif

# Link objects into a PE32+ EFI application.
ifeq ($(PLATFORM),windows)
$(TARGET): $(OBJECTS)
	$(LD) $(LDFLAGS) -o $(LINK_OUTPUT) $(OBJECTS)
	$(OBJCOPY) -j .text -j .sdata -j .data -j .rodata -j .bss -j .rdata \
		--target efi-app-x86_64 \
		$(LINK_OUTPUT) $(TARGET)
	@echo ""
	@echo "  Built: $(TARGET)"
	@echo "  Toolchain: $(CXX)"
	@echo ""
else
$(TARGET): $(OBJECTS)
	$(LD) $(LDFLAGS) /out:$(TARGET) $(OBJECTS)
	@echo ""
	@echo "  Built: $(TARGET)"
	@echo "  Toolchain: $(CXX)"
	@echo ""
endif

# Include auto-generated header dependency files (.d)
-include $(DEPENDS)

clean:
	rm -rf $(BUILDDIR)

# ── Bootable disk image ──────────────────────────────────────
# Creates a 64 MB FAT32 image with the .efi at the standard boot path.
# Requires: mtools (mmd, mcopy), dosfstools (mkfs.fat)
disk: check-toolchain $(TARGET)
	@echo "Creating FAT32 disk image..."
	dd if=/dev/zero of=$(DISKIMG) bs=1M count=64 status=none
	mkfs.fat -F 32 $(DISKIMG) >/dev/null
	mmd -i $(DISKIMG) ::/EFI ::/EFI/BOOT
	mcopy -i $(DISKIMG) $(TARGET) ::/EFI/BOOT/BOOTX64.EFI
	@echo "  Disk image: $(DISKIMG)"

check-iso-tools:
	@test -n "$(ISO_TOOL)" || { echo "Error: no ISO creation tool found."; \
		echo "Install xorriso, genisoimage, or mkisofs to use 'make iso'."; \
		exit 1; }

$(EFIBOOTIMG): check-toolchain $(TARGET) | $(BUILDDIR)
	@echo "Creating EFI boot image..."
	dd if=/dev/zero of=$(EFIBOOTIMG) bs=1M count=4 status=none
	mkfs.fat $(EFIBOOTIMG) >/dev/null
	mmd -i $(EFIBOOTIMG) ::/EFI ::/EFI/BOOT
	mcopy -i $(EFIBOOTIMG) $(TARGET) ::/EFI/BOOT/BOOTX64.EFI

# ── Bootable ISO image ───────────────────────────────────────
# Creates an El Torito UEFI boot ISO containing a FAT EFI boot image.
# Requires: mtools, dosfstools, and one of xorriso / genisoimage / mkisofs
iso: check-iso-tools $(ISOIMG)
	@echo "  ISO image: $(ISOIMG)"

$(ISOIMG): $(EFIBOOTIMG)
	@echo "Creating bootable ISO..."
	rm -rf $(ISOROOT)
	mkdir -p $(ISOROOT)
	cp $(EFIBOOTIMG) $(ISOROOT)/efiboot.img
ifeq ($(ISO_TOOL),xorriso)
	xorriso -as mkisofs \
		-R -J \
		-V UEFIBENCH \
		-eltorito-alt-boot \
		-e efiboot.img \
		-no-emul-boot \
		-o $(ISOIMG) \
		$(ISOROOT)
else
	$(ISO_TOOL) \
		-R -J \
		-V UEFIBENCH \
		-eltorito-alt-boot \
		-e efiboot.img \
		-no-emul-boot \
		-o $(ISOIMG) \
		$(ISOROOT)
endif

# ── QEMU test ────────────────────────────────────────────────
# Boots the disk image with OVMF UEFI firmware.
# Requires: qemu-system-x86_64, OVMF
qemu: disk
	@test -f "$(OVMF)" || { echo "Error: OVMF not found at $(OVMF)"; \
		echo "Install OVMF or override:  make qemu OVMF=/path/to/OVMF.fd"; \
		exit 1; }
	qemu-system-x86_64 \
		-bios $(OVMF) \
		-drive format=raw,file=$(DISKIMG) \
		-m 512M \
		-cpu qemu64 \
		-net none \
		-serial stdio

# ── Help ──────────────────────────────────────────────────────
help:
	@echo "UefiBenchmark build targets:"
	@echo ""
	@echo "  make              Build UefiBenchmark.efi"
	@echo "  make disk         Build + create bootable FAT32 disk image"
	@echo "  make iso          Build + create bootable UEFI ISO image"
	@echo "  make qemu         Build + boot in QEMU (requires OVMF + mtools)"
	@echo "  make clean        Remove build artifacts"
	@echo "  make help         Show this help"
	@echo ""
	@echo "Detected settings:"
	@echo "  Platform:  $(PLATFORM)"
	@echo "  Compiler:  $(CXX)"
	@echo "  Linker:    $(LD)"
	@echo "  Objcopy:   $(OBJCOPY)"
	@echo "  ISO tool:  $(if $(ISO_TOOL),$(ISO_TOOL),not found)"
	@echo "  OVMF:      $(OVMF)"
	@echo ""
	@echo "Override toolchain:"
	@echo "  make CXX=clang++ LD=lld-link OBJCOPY=llvm-objcopy"
	@echo "Override OVMF:"
	@echo "  make qemu OVMF=/path/to/OVMF.fd"
