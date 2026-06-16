# ── RootBench Makefile ────────────────────────────────────────
# Builds the RootBench UEFI benchmark tool as a PE32+ .efi application.
#
# Supported hosts:
#   - Linux with LLVM/LLD (native host toolchain)
#   - Windows with MSYS2/MinGW64
#
# Usage:
#   make              # auto-detects platform, builds RootBench.efi
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
  LINK_OUTPUT = $(BUILDDIR)/RootBench.dll
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

# ── Secure Boot signing ───────────────────────────────────────
# Signing is ON by default. The .efi is signed with sbsign during the build
# (zero interaction). On first use, a self-signed key/cert pair is generated
# automatically in $(KEYDIR) via openssl in batch mode — no prompts.
#
#   make                  # build + sign RootBench.efi (signs in place)
#   make disk             # build + sign + bootable disk with the signed .efi
#   make SIGN=0           # opt out: build an unsigned .efi
#
# Bring your own keys instead of the auto-generated pair:
#   make SB_KEY=/path/db.key SB_CERT=/path/db.crt
#
# NOTE: signing alone does not make Secure Boot accept the binary — the
# certificate ($(SB_CERT)) must also be enrolled into the firmware's `db`
# (or as a MOK via mokutil). See the `enroll-info` target.
SIGN     ?= 1
SBSIGN   ?= sbsign
OPENSSL  ?= openssl

# ── Self-check provisioning ───────────────────────────────────
# After linking, embed a CRC32 of .text into the binary so the app can verify
# at startup that it loaded intact (see Source/SelfCheck.cpp). Runs before
# signing. Skipped gracefully if python3 is unavailable (app then falls back to
# sentinel-only checks). Disable with SELFCRC=0.
SELFCRC  ?= 1
PYTHON   ?= python3
SELFCRC_TOOL := tools/patch_selfcrc.py
MOKUTIL  ?= mokutil
KEYDIR   ?= keys
SB_DER   ?= $(KEYDIR)/db.der
SB_CN    ?= RootBench Secure Boot

# mokutil needs root; use sudo automatically when not already root.
SUDO := $(shell [ "$$(id -u)" = 0 ] 2>/dev/null || echo sudo)

# ── Reuse an already-enrolled Machine Owner Key (MOK) ────────
# To avoid generating + enrolling a second key just for RootBench, auto-detect a
# MOK that is already enrolled on this machine and sign with it. Debian/Ubuntu
# (shim/DKMS) and Fedora (akmods) standard locations are checked, in that order.
# An explicit SB_KEY=... always wins over detection; USE_MOK=0 disables it.
# sbsign needs a PEM certificate, so the MOK's DER cert is converted once into
# $(KEYDIR)/mok.pem. The MOK private key is normally root-only, so the signing
# step escalates via sudo when the current user cannot read it.
USE_MOK ?= 1

MOK_DEB_KEY  := /var/lib/shim-signed/mok/MOK.priv
MOK_DEB_CERT := /var/lib/shim-signed/mok/MOK.der
MOK_FED_KEY  := /etc/pki/akmods/private/private_key.priv
MOK_FED_CERT := /etc/pki/akmods/certs/public_key.der

# Detect only when the user has not explicitly chosen a signing key.
ifeq ($(filter command line environment,$(origin SB_KEY)),)
ifeq ($(USE_MOK),1)
  # Detect off the certificate: it is the artifact we must convert, and it is
  # usually more readable than the private key (whose directory is often
  # root-only). The key is read at sign time, escalating via sudo if needed.
  ifneq ($(wildcard $(MOK_DEB_CERT)),)
    MOK_KEY      := $(MOK_DEB_KEY)
    MOK_CERT_DER := $(MOK_DEB_CERT)
    MOK_SOURCE   := Debian/Ubuntu shim MOK
  else ifneq ($(wildcard $(MOK_FED_CERT)),)
    MOK_KEY      := $(MOK_FED_KEY)
    MOK_CERT_DER := $(MOK_FED_CERT)
    MOK_SOURCE   := Fedora akmods MOK
  endif
endif
endif

ifdef MOK_KEY
  # Sign with the already-enrolled MOK; convert its DER cert to PEM for sbsign.
  SB_KEY    ?= $(MOK_KEY)
  SB_CERT   ?= $(KEYDIR)/mok.pem
  # Escalate signing only if the (usually root-only) private key isn't readable.
  SIGN_SUDO := $(shell test -r $(MOK_KEY) 2>/dev/null || echo $(SUDO))
  # The private key lives in a root-only dir we may not be able to stat, so it
  # must NOT be a make prerequisite — sbsign reads it directly (under sudo).
  SIGN_KEY_DEP := $(SB_CERT)
else
  # No enrolled MOK found: fall back to a self-signed pair generated in $(KEYDIR).
  SB_KEY    ?= $(KEYDIR)/db.key
  SB_CERT   ?= $(KEYDIR)/db.crt
  SIGN_SUDO :=
  SIGN_KEY_DEP := $(SB_KEY) $(SB_CERT)
endif

# 'make install' (Linux): vendor directory and filename on the EFI System
# Partition. The default lands at <ESP>/EFI/RootBench/RootBenchX64.efi.
INSTALL_SUBDIR ?= EFI/RootBench
INSTALL_NAME   ?= RootBenchX64.efi

ifeq ($(SIGN),1)
  SIGN_DEP := sign
  # Pre-flight tool check, run before the compile so a missing signing tool is
  # reported up front instead of after a full build + key generation.
  SIGN_PRECHECK := check-sign-tools
else
  SIGN_DEP :=
  SIGN_PRECHECK :=
endif

# ── Directories ───────────────────────────────────────────────
SRCDIR   = Source
INCDIR   = Include
BUILDDIR = Build
BMDIR    = $(SRCDIR)/Benchmarks
SCNDIR   = $(SRCDIR)/Screens

# ── Output ────────────────────────────────────────────────────
TARGET   = $(BUILDDIR)/RootBench.efi

# ── Sources ───────────────────────────────────────────────────
SOURCES  = \
	$(SRCDIR)/Freestanding.cpp \
	$(SRCDIR)/Timer.cpp \
	$(SRCDIR)/SystemInfo.cpp \
	$(SRCDIR)/BitmapFont.cpp \
	$(SRCDIR)/Renderer.cpp \
	$(SRCDIR)/BenchmarkRegistry.cpp \
	$(SRCDIR)/BenchmarkRunner.cpp \
	$(SRCDIR)/CoreSelection.cpp \
	$(SRCDIR)/ScrollViewport.cpp \
	$(SRCDIR)/SelfCheck.cpp \
	$(SRCDIR)/RunConfig.cpp \
	$(SRCDIR)/Tui.cpp \
	$(SCNDIR)/UiHelpers.cpp \
	$(SCNDIR)/MainMenuScreen.cpp \
	$(SCNDIR)/BenchmarkSelectionScreen.cpp \
	$(SCNDIR)/RunCountPickerScreen.cpp \
	$(SCNDIR)/ResultsScreen.cpp \
	$(SCNDIR)/CategoryResultsScreen.cpp \
	$(SCNDIR)/SystemInfoScreen.cpp \
	$(SCNDIR)/AiSuitabilityScreen.cpp \
	$(SCNDIR)/SmbusDebugScreen.cpp \
	$(SCNDIR)/ResolutionPickerScreen.cpp \
	$(SCNDIR)/ThemePickerScreen.cpp \
	$(SCNDIR)/CorePickerScreen.cpp \
	$(SRCDIR)/CpuFeatures.cpp \
	$(SRCDIR)/BigBuffer.cpp \
	$(SRCDIR)/MachineCheck.cpp \
	$(SRCDIR)/VideoEngine.cpp \
	$(BMDIR)/IntThroughputBenchmark.cpp \
	$(BMDIR)/IntLatencyBenchmark.cpp \
	$(BMDIR)/FpScalarBenchmark.cpp \
	$(BMDIR)/FpVectorBenchmark.cpp \
	$(BMDIR)/BranchBenchmark.cpp \
	$(BMDIR)/AesBenchmark.cpp \
	$(BMDIR)/HashBenchmark.cpp \
	$(BMDIR)/MandelbrotBenchmark.cpp \
	$(BMDIR)/MemBandwidthBenchmark.cpp \
	$(BMDIR)/MemLatencyBenchmark.cpp \
	$(BMDIR)/MemIntegrityBenchmark.cpp \
	$(BMDIR)/L3CacheCliffBenchmark.cpp \
	$(BMDIR)/AiInt8Benchmark.cpp \
	$(BMDIR)/AiInt4Benchmark.cpp \
	$(BMDIR)/AiMemBenchmark.cpp \
	$(BMDIR)/AiCacheBenchmark.cpp \
	$(BMDIR)/StressMemClockBenchmark.cpp \
	$(BMDIR)/StressMemLatencyBenchmark.cpp \
	$(BMDIR)/StressCpuPowerBenchmark.cpp \
	$(BMDIR)/StressCpuVerifyBenchmark.cpp \
	$(SRCDIR)/Main.cpp

OBJECTS  = $(patsubst %.cpp,$(BUILDDIR)/%.o,$(notdir $(SOURCES)))
DEPENDS  = $(OBJECTS:.o=.d)

# ── Compiler flags ────────────────────────────────────────────
CXXFLAGS = -std=c++17 \
           -O2 \
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

# ── Render mode ───────────────────────────────────────────────
# VIDEO_BLT=1 (default): flush dirty rows via GOP->Blt() (Boot Services).
#   Shadow buffer is kept in EFI_GRAPHICS_OUTPUT_BLT_PIXEL (BGRA) order;
#   the firmware converts to the hardware pixel format. Fast on most UEFI
#   implementations. BSP-only — do not call Present() from APs in this mode.
# VIDEO_BLT=0: write directly to FrameBufferBase using AVX2 non-temporal
#   streaming stores (falls back to memcpy). Safe to call from APs.
#   Shadow buffer is in the native hardware pixel format (BGR or RGB).
VIDEO_BLT ?= 1
ifeq ($(VIDEO_BLT),1)
  CXXFLAGS += -DVIDEO_BLT=1
endif

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
ISOIMG   = $(BUILDDIR)/RootBench.iso
ISOROOT  = $(BUILDDIR)/iso-root

ISO_TOOL := $(shell if command -v xorriso >/dev/null 2>&1; then echo xorriso; \
                   elif command -v genisoimage >/dev/null 2>&1; then echo genisoimage; \
                   elif command -v mkisofs >/dev/null 2>&1; then echo mkisofs; fi)

# ── Rules ─────────────────────────────────────────────────────
.PHONY: all clean disk iso qemu qemusingle install help check-toolchain check-iso-tools check-sign-tools sign keys enroll enroll-info enroll-status

all: check-toolchain $(SIGN_PRECHECK) $(TARGET) $(SIGN_DEP)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

# Compile .cpp → .o  (Source/)
$(BUILDDIR)/%.o: $(SRCDIR)/%.cpp | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# Compile .cpp → .o  (Source/Benchmarks/)
$(BUILDDIR)/%.o: $(BMDIR)/%.cpp | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# Compile .cpp → .o  (Source/Screens/)
$(BUILDDIR)/%.o: $(SCNDIR)/%.cpp | $(BUILDDIR)
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
	@if [ "$(SELFCRC)" = "1" ]; then \
		command -v $(PYTHON) >/dev/null 2>&1 \
		&& $(PYTHON) $(SELFCRC_TOOL) $(TARGET) \
		|| echo "  [selfcheck] $(PYTHON) not found - CRC not embedded (sentinel checks only)"; \
	fi
	@echo ""
	@echo "  Built: $(TARGET)"
	@echo "  Toolchain: $(CXX)"
	@echo ""
else
$(TARGET): $(OBJECTS)
	$(LD) $(LDFLAGS) /out:$(TARGET) $(OBJECTS)
	@if [ "$(SELFCRC)" = "1" ]; then \
		command -v $(PYTHON) >/dev/null 2>&1 \
		&& $(PYTHON) $(SELFCRC_TOOL) $(TARGET) \
		|| echo "  [selfcheck] $(PYTHON) not found - CRC not embedded (sentinel checks only)"; \
	fi
	@echo ""
	@echo "  Built: $(TARGET)"
	@echo "  Toolchain: $(CXX)"
	@echo ""
endif

# ── Secure Boot signing rules ─────────────────────────────────
$(KEYDIR):
	mkdir -p $(KEYDIR)

ifdef MOK_KEY
# Reusing an enrolled MOK: its private key already exists; we only need to
# convert the MOK's DER certificate to the PEM form sbsign expects.
$(SB_CERT): $(MOK_CERT_DER) | $(KEYDIR)
	@command -v $(OPENSSL) >/dev/null 2>&1 || { echo "Error: openssl not found (needed to convert the MOK cert)."; exit 1; }
	$(OPENSSL) x509 -inform DER -in $(MOK_CERT_DER) -outform PEM -out $(SB_CERT)
	@echo "  Reusing enrolled MOK ($(MOK_SOURCE)): $(MOK_KEY)"
	@echo "  Converted MOK cert -> $(SB_CERT)"
else
# Generate a self-signed Secure Boot key/cert once, fully non-interactively.
# -batch + -subj suppress all prompts; -nodes leaves the key unencrypted so
# sbsign needs no passphrase.
$(SB_KEY): | $(KEYDIR)
	@command -v $(OPENSSL) >/dev/null 2>&1 || { echo "Error: openssl not found (needed to generate signing keys)."; exit 1; }
	$(OPENSSL) req -new -x509 -newkey rsa:2048 -nodes -batch \
		-subj "/CN=$(SB_CN)/" -days 3650 \
		-keyout $(SB_KEY) -out $(SB_CERT)
	@echo "  Generated signing key:  $(SB_KEY)"
	@echo "  Generated certificate:  $(SB_CERT)"

# The cert is produced alongside the key; this keeps it as a known prereq.
$(SB_CERT): $(SB_KEY)
endif

keys: $(SIGN_KEY_DEP)

# Sign the built .efi in place with sbsign. Depends on $(TARGET) and the keys,
# so the binary is always freshly built and the keys exist before signing.
sign: $(TARGET) $(SIGN_KEY_DEP)
	@command -v $(SBSIGN) >/dev/null 2>&1 || { echo "Error: sbsign not found."; \
		echo "Install sbsigntools (Debian/Ubuntu: apt install sbsigntool;"; \
		echo "Fedora: dnf install sbsigntools; Arch: pacman -S sbsigntools)."; \
		exit 1; }
ifdef MOK_KEY
	@echo "  Signing with already-enrolled MOK ($(MOK_SOURCE)): $(MOK_KEY)"
else
	@echo "  No enrolled MOK detected; signing with self-signed key: $(SB_KEY)"
	@echo "  (enroll it with 'make enroll', or reuse a MOK via SB_KEY=.../SB_CERT=...)"
endif
	$(SIGN_SUDO) $(SBSIGN) --key $(SB_KEY) --cert $(SB_CERT) --output $(TARGET) $(TARGET)
	@echo ""
	@echo "  Signed: $(TARGET)"
	@echo "  Cert:   $(SB_CERT)$(if $(MOK_KEY), (MOK already enrolled), (must be enrolled in firmware db — see 'make enroll-info'))"
	@echo ""

# mokutil consumes a DER-encoded certificate; our cert is PEM, so convert it.
$(SB_DER): $(SB_CERT)
	@command -v $(OPENSSL) >/dev/null 2>&1 || { echo "Error: openssl not found (needed to convert cert to DER)."; exit 1; }
	$(OPENSSL) x509 -in $(SB_CERT) -outform DER -out $(SB_DER)
	@echo "  DER certificate:  $(SB_DER)"

# Stage the cert for Secure Boot enrollment via the Machine Owner Key (MOK).
# This queues the import; MokManager finishes it on the NEXT reboot, where
# physical-presence confirmation is required by design (unavoidable).
# --root-pw lets MokManager accept the machine's root password instead of a
# fresh one-time password, so this invocation needs no interactive prompt.
enroll: $(SB_DER)
	@command -v $(MOKUTIL) >/dev/null 2>&1 || { echo "Error: mokutil not found (install the 'mokutil' package)."; exit 1; }
	$(SUDO) $(MOKUTIL) --import $(SB_DER) --root-pw
	@echo ""
	@echo "  Queued $(SB_DER) for MOK enrollment."
	@echo "  Reboot now; in MokManager choose 'Enroll MOK' and enter the root password."
	@echo ""

# Show pending/enrolled MOK state.
enroll-status:
	@command -v $(MOKUTIL) >/dev/null 2>&1 || { echo "Error: mokutil not found (install the 'mokutil' package)."; exit 1; }
	@$(MOKUTIL) --list-new 2>/dev/null || true
	@$(MOKUTIL) --list-enrolled 2>/dev/null || true

enroll-info:
	@echo "To boot the signed .efi under Secure Boot, enroll $(SB_CERT):"
	@echo ""
	@echo "  On real hardware (MOK):  make enroll    # then reboot into MokManager"
	@echo "  Check status:            make enroll-status"
	@echo ""
	@echo "  In QEMU/OVMF: use a Secure-Boot-enabled OVMF_VARS and enroll the"
	@echo "  cert into 'db' via the firmware setup UI or virt-fw-vars."
	@echo ""

# ── Per-file ISA handling ─────────────────────────────────────
# No global per-file -m<isa> flags. Every SIMD kernel carries its own
# __attribute__((target(...))), enabling the ISA for that function only. This
# keeps all other code — and -O2 auto-vectorization — at the SSE2 baseline, so
# AVX/AES instructions can never be emitted before EnableAvxState() is called.
# (Global -mavx2 would let -O2 auto-vectorize ordinary loops into AVX, faulting
# on cores where the OS/firmware has not yet enabled AVX state.)

# Include auto-generated header dependency files (.d)
-include $(DEPENDS)

clean:
	rm -rf $(BUILDDIR)

# ── Bootable disk image ──────────────────────────────────────
# Creates a 64 MB FAT32 image with the .efi at the standard boot path.
# Requires: mtools (mmd, mcopy), dosfstools (mkfs.fat)
disk: check-toolchain $(SIGN_PRECHECK) $(TARGET) $(SIGN_DEP)
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

# Pre-flight check for Secure Boot signing (SIGN=1). Reports every missing tool
# at once with install hints and the opt-out, before the build does any work.
check-sign-tools:
	@missing=; \
	command -v "$(SBSIGN)"  >/dev/null 2>&1 || missing="$$missing $(SBSIGN)"; \
	command -v "$(OPENSSL)" >/dev/null 2>&1 || missing="$$missing $(OPENSSL)"; \
	if [ -n "$$missing" ]; then \
		echo "Error: Secure Boot signing is enabled (SIGN=1) but required tool(s) not found:$$missing"; \
		echo "  Fedora:         sudo dnf install sbsigntools openssl"; \
		echo "  Debian/Ubuntu:  sudo apt install sbsigntool openssl"; \
		echo "  Arch:           sudo pacman -S sbsigntools openssl"; \
		echo "Or build without signing:  make SIGN=0"; \
		exit 1; \
	fi

$(EFIBOOTIMG): check-toolchain $(SIGN_PRECHECK) $(TARGET) $(SIGN_DEP) | $(BUILDDIR)
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
		-V ROOTBENCH \
		-eltorito-alt-boot \
		-e efiboot.img \
		-no-emul-boot \
		-o $(ISOIMG) \
		$(ISOROOT)
else
	$(ISO_TOOL) \
		-R -J \
		-V ROOTBENCH \
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
		-smp 4 \
		-cpu qemu64 \
		-net none \
		-serial stdio

qemusingle: disk
	@test -f "$(OVMF)" || { echo "Error: OVMF not found at $(OVMF)"; \
		echo "Install OVMF or override:  make qemusingle OVMF=/path/to/OVMF.fd"; \
		exit 1; }
	qemu-system-x86_64 \
		-bios $(OVMF) \
		-drive format=raw,file=$(DISKIMG) \
		-m 512M \
		-cpu qemu64 \
		-net none \
		-serial stdio

# ── Install to the EFI System Partition (Linux) ──────────────
# Builds (and signs, unless SIGN=0) the .efi, copies it to the ESP under
# $(INSTALL_SUBDIR), registers a GRUB chainloader entry, and regenerates the
# GRUB config. The privileged steps need root.
#
# Two supported ways to run it:
#   sudo make install   # everything runs as root (so a root-only MOK is also
#                        # detected and used); artifacts are chowned back after.
#   make install        # build/sign as you; only the ESP/GRUB step escalates
#                        # via sudo. Note: a root-only MOK won't be visible here.
install: check-toolchain $(SIGN_PRECHECK) $(TARGET) $(SIGN_DEP)
ifeq ($(PLATFORM),windows)
	@echo "Error: 'make install' is Linux-only."; exit 1
else
	$(SUDO) bash tools/install.sh "$(TARGET)" "$(INSTALL_SUBDIR)" "$(INSTALL_NAME)"
	@# Under 'sudo make install' the build above ran as root and left root-owned
	@# artifacts; hand them back so a later plain 'make' isn't blocked.
	@if [ -n "$$SUDO_USER" ]; then \
		chown -R "$$SUDO_USER" $(BUILDDIR) $(KEYDIR) 2>/dev/null || true; \
		echo "  Restored ownership of $(BUILDDIR)/ and $(KEYDIR)/ to $$SUDO_USER."; \
	fi
endif

# ── Help ──────────────────────────────────────────────────────
help:
	@echo "RootBench build targets:"
	@echo ""
	@echo "  make              Build RootBench.efi"
	@echo "  make disk         Build + create bootable FAT32 disk image"
	@echo "  make iso          Build + create bootable UEFI ISO image"
	@echo "  make qemu         Build + boot in QEMU with 4 cores (requires OVMF + mtools)"
	@echo "  make qemusingle   Build + boot in QEMU with 1 core"
	@echo "  sudo make install Install to the EFI partition + add a GRUB entry (Linux)"
	@echo "  make SIGN=0       Opt out of Secure Boot signing (signing is on by default)"
	@echo "  make sign         Sign an already-built .efi in place"
	@echo "  make keys         Generate the self-signed key/cert pair only"
	@echo "  make enroll       Import the cert as a MOK (reboot to confirm in MokManager)"
	@echo "  make enroll-status  Show pending/enrolled MOK certificates"
	@echo "  make enroll-info  How Secure Boot enrollment works"
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
	@echo "  Sign:      $(if $(filter 1,$(SIGN)),enabled$(if $(MOK_KEY), (reusing enrolled $(MOK_SOURCE)), (self-signed: $(SB_CERT))),disabled)"
	@echo ""
	@echo "Override toolchain:"
	@echo "  make CXX=clang++ LD=lld-link OBJCOPY=llvm-objcopy"
	@echo "Override OVMF:"
	@echo "  make qemu OVMF=/path/to/OVMF.fd"
