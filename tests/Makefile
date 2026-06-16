# ── UefiBenchmark host unit-test build ────────────────────────────────────────
# Compiles freestanding production code for a native host process.
# The UefiShim replaces UEFI firmware dependencies (allocator, Timer, etc.)
# with host-compatible stubs so pure logic can be exercised without firmware.
#
# Usage:
#   make              # build + run all tests
#   make compile      # build only
#   make clean        # remove build artefacts
#   make fetch-deps   # download doctest.h (run once or when offline)
# ──────────────────────────────────────────────────────────────────────────────

CXX      ?= g++
CXXFLAGS  = -std=c++17 -Wall -Wextra -Wpedantic \
            -I../Include -I. -Ithird_party \
            -DUEFI_HOST_TEST \
            -g

BUILD_DIR  = build
THIRD_PARTY = third_party
DOCTEST_H   = $(THIRD_PARTY)/doctest.h
DOCTEST_URL = https://raw.githubusercontent.com/doctest/doctest/v2.4.11/doctest/doctest.h

# ── Sources ────────────────────────────────────────────────────────────────────
SHIM_SRCS := host/UefiShim.cpp

# Production sources that compile cleanly on the host (no gBS/GOP calls).
PROD_SRCS := ../Source/RunConfig.cpp \
             ../Source/BenchmarkRegistry.cpp \
             ../Source/BigBuffer.cpp \
             ../Source/CoreSelection.cpp \
             ../Source/ScrollViewport.cpp

UNIT_SRCS := $(wildcard unit/test_*.cpp)

ALL_SRCS := main.cpp $(SHIM_SRCS) $(PROD_SRCS) $(UNIT_SRCS)

RUNNER := $(BUILD_DIR)/run_tests

# ── Targets ───────────────────────────────────────────────────────────────────
.PHONY: all compile run clean fetch-deps

all: compile run

compile: $(DOCTEST_H) $(RUNNER)

run: $(RUNNER)
	@echo "──────────────────────────────────────────"
	@$(RUNNER) --duration=true
	@echo "──────────────────────────────────────────"

$(RUNNER): $(ALL_SRCS) $(DOCTEST_H) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $(ALL_SRCS)

$(BUILD_DIR):
	mkdir -p $@

$(DOCTEST_H):
	@mkdir -p $(THIRD_PARTY)
	@echo "Downloading doctest.h …"
	@curl -fsSL -o $@ $(DOCTEST_URL) || \
	  (echo "curl failed — try: wget -O $@ $(DOCTEST_URL)"; exit 1)
	@echo "doctest.h downloaded."

fetch-deps: $(DOCTEST_H)

clean:
	rm -rf $(BUILD_DIR)
