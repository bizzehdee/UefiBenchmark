// Host-build UEFI shim.
// Provides everything the test binaries need that cannot come from the
// production freestanding TUs (which define conflicting host symbols such as
// atexit and UEFI-backed operator new).
//
// Provides:
//  - UEFI global stubs  (gST / gBS / gImageHandle = null)
//  - operator new/delete backed by malloc
//  - optimized_memcpy  (Freestanding.h declares it; libc doesn't provide it)
//  - String helpers: StrLen, StrCmp, StrCopy, IntToStr, UintToStr, HexToStr
//    (verbatim from Source/Freestanding.cpp — kept in sync by inspection)
//  - ConPrint / ConPrintLine        (no-ops)
//  - Timer::*                       (FakeTimer — test-controlled)
//  - MachineCheck::*                (stubs — no MSR access)
//
// memset / memcpy / memmove / memcmp are provided by the host libc at link time.
// With UEFI_HOST_TEST defined, UINTN == unsigned long == size_t, so the
// extern "C" declarations in Freestanding.h are compatible with libc's signatures.

// Include UefiTypes.h first so that its NULL/bool defines win over any later
// system headers. We declare malloc/free manually to avoid pulling in <stdlib.h>
// (which would redefine NULL and potentially pull in <string.h>).
#include "UefiTypes.h"
#include "MachineCheck.h"
#include "host/UefiShim.h"

// malloc/free — declared with UINTN which equals size_t in host builds.
extern "C" { void* malloc(UINTN sz); void free(void* p); }

// ── UEFI globals ─────────────────────────────────────────────────────────────
extern "C" {
    EFI_SYSTEM_TABLE*  gST          = nullptr;
    EFI_BOOT_SERVICES* gBS          = nullptr;
    EFI_HANDLE         gImageHandle = nullptr;
}

// ── Operator new / delete → malloc / free ────────────────────────────────────
// UINTN == size_t in host builds, so these satisfy the C++ new/delete ABI.
void* operator new  (UINTN sz)              { return malloc(sz ? sz : 1); }
void* operator new[](UINTN sz)              { return malloc(sz ? sz : 1); }
void  operator delete  (void* p) noexcept   { free(p); }
void  operator delete[](void* p) noexcept   { free(p); }
void  operator delete  (void* p, UINTN) noexcept { free(p); }
void  operator delete[](void* p, UINTN) noexcept { free(p); }

// ── optimized_memcpy — not in libc; delegates to libc memcpy on the host ─────
extern "C" {
void* optimized_memcpy(void* dest, const void* src, UINTN count) {
    // libc memcpy is declared extern "C" in Freestanding.h; forward to it.
    extern void* memcpy(void*, const void*, UINTN);
    return memcpy(dest, src, count);
}
} // extern "C"

// ── String helpers (verbatim from Source/Freestanding.cpp) ───────────────────
UINTN StrLen(const char* s) {
    if (!s) return 0;
    UINTN len = 0;
    while (s[len]) ++len;
    return len;
}

int StrCmp(const char* a, const char* b) {
    if (!a || !b) return a == b ? 0 : (a ? 1 : -1);
    while (*a && *a == *b) { ++a; ++b; }
    return static_cast<int>(static_cast<UINT8>(*a)) -
           static_cast<int>(static_cast<UINT8>(*b));
}

void StrCopy(char* dest, const char* src, UINTN maxLen) {
    if (!dest || maxLen == 0) return;
    UINTN i = 0;
    if (src) {
        for (; i < maxLen - 1 && src[i]; ++i)
            dest[i] = src[i];
    }
    dest[i] = '\0';
}

static char sIntBuf[24];

const char* IntToStr(INT64 value) {
    if (value == 0) { sIntBuf[0] = '0'; sIntBuf[1] = '\0'; return sIntBuf; }
    bool   neg = value < 0;
    UINT64 v   = neg ? static_cast<UINT64>(-value) : static_cast<UINT64>(value);
    int pos = 23;
    sIntBuf[pos] = '\0';
    while (v > 0) {
        sIntBuf[--pos] = '0' + static_cast<char>(v % 10);
        v /= 10;
    }
    if (neg) sIntBuf[--pos] = '-';
    int len = 23 - pos;
    for (int i = 0; i <= len; ++i) sIntBuf[i] = sIntBuf[pos + i];
    return sIntBuf;
}

const char* UintToStr(UINT64 value) {
    if (value == 0) { sIntBuf[0] = '0'; sIntBuf[1] = '\0'; return sIntBuf; }
    int pos = 23;
    sIntBuf[pos] = '\0';
    while (value > 0) {
        sIntBuf[--pos] = '0' + static_cast<char>(value % 10);
        value /= 10;
    }
    int len = 23 - pos;
    for (int i = 0; i <= len; ++i) sIntBuf[i] = sIntBuf[pos + i];
    return sIntBuf;
}

const char* HexToStr(UINT64 value, int digits) {
    if (digits < 1)  digits = 1;
    if (digits > 16) digits = 16;
    static char buf[20];
    const char* hex = "0123456789ABCDEF";
    for (int i = digits - 1; i >= 0; --i) {
        buf[i] = hex[value & 0xF];
        value >>= 4;
    }
    buf[digits] = '\0';
    return buf;
}

// ── ConPrint / ConPrintLine — no-ops on host ─────────────────────────────────
void ConPrint(const char*) {}
void ConPrintLine(const char*) {}

// ── FakeTimer — controllable Timer:: implementation ──────────────────────────
namespace {
    bool   sCalibrated  = false;
    UINT64 sCyclesPerUs = 1;
    UINT64 sCurrentTSC  = 0;
    UINT64 sStepPerRead = 0;
}

namespace FakeTimer {

void Reset() {
    sCalibrated  = false;
    sCyclesPerUs = 1;
    sCurrentTSC  = 0;
    sStepPerRead = 0;
}

void SetCalibrated(bool on)       { sCalibrated  = on; }
void SetCyclesPerUs(UINT64 cpus)  { sCyclesPerUs = cpus; }
void SetTSC(UINT64 tsc)           { sCurrentTSC  = tsc; }
void SetStepPerRead(UINT64 step)  { sStepPerRead = step; }
UINT64 GetCurrentTSC()            { return sCurrentTSC; }

} // namespace FakeTimer

namespace Timer {

void   Calibrate()      {}
bool   IsCalibrated()   { return sCalibrated; }
UINT64 CyclesPerUs()    { return sCyclesPerUs; }

UINT64 ReadTSC() {
    sCurrentTSC += sStepPerRead;
    return sCurrentTSC;
}

void   Start()          {}
UINT64 ElapsedUs()      { return 0; }
UINT64 ElapsedMs()      { return 0; }
bool   HasInvariantTSC(){ return false; }

} // namespace Timer

// ── MachineCheck stubs — no MSR access on host ───────────────────────────────
namespace MachineCheck {

bool Available()              { return false; }
void BeginRun()               {}
void PollLocal()              {}
UINT32 CorrectedCount()       { return 0; }
UINT32 UncorrectedCount()     { return 0; }
bool LastEvent(McaEvent*)     { return false; }

} // namespace MachineCheck

// ── SystemInfo stubs — only the symbols referenced by BigBuffer / CoreSelection
// (Allocate() and Init() call these; both are never invoked in host tests)
#include "SystemInfo.h"
namespace SystemInfo {
UINT64 GetTotalMemoryBytes()               { return 0; }
EFI_MP_SERVICES_PROTOCOL* GetMpServices() { return nullptr; }
} // namespace SystemInfo

// ── FakeRenderer ─────────────────────────────────────────────────────────────
namespace {
    UINT32 sRendCols  = 80;
    UINT32 sRendRows  = 25;
    FakeRenderer::DrawCall sCalls[FakeRenderer::MAX_CALLS];
    UINT32 sCallCount = 0;
}

namespace FakeRenderer {

void Reset() {
    sCallCount = 0;
    sRendCols  = 80;
    sRendRows  = 25;
}

void SetSize(UINT32 cols, UINT32 rows) { sRendCols = cols; sRendRows = rows; }

UINT32 CallCount()         { return sCallCount; }
const DrawCall* Calls()    { return sCalls; }

static void Record(int col, int row, const char* text, bool hasBg) {
    if (sCallCount >= MAX_CALLS) return;
    DrawCall& c = sCalls[sCallCount++];
    c.col   = col;
    c.row   = row;
    c.hasBg = hasBg;
    int i = 0;
    if (text) {
        while (text[i] && i < 127) { c.text[i] = text[i]; ++i; }
    }
    c.text[i] = '\0';
}

} // namespace FakeRenderer

// ── Renderer:: stubs — recording fake for host tests ─────────────────────────
#include "Renderer.h"
namespace Renderer {

bool   Init(UINT32, UINT32)           { return false; }
bool   IsGraphics()                   { return false; }
UINT32 ScreenWidth()                  { return sRendCols * 8; }
UINT32 ScreenHeight()                 { return sRendRows * 16; }
UINT32 Columns()                      { return sRendCols; }
UINT32 Rows()                         { return sRendRows; }
UINT32 FontScale()                    { return 1; }
UINT32 ListModes(ModeDesc*, UINT32)   { return 0; }
bool   SetModeByIndex(UINT32)         { return false; }
UINT32 CurrentModeIndex()             { return 0; }
void   Clear()                        {}
void   Clear(Color)                   {}
void   FillRow(int, Color)            {}
void   Present()                      {}
void   TextPrint(const char*)         {}
void   TextClear()                    {}
EFI_INPUT_KEY WaitKey()               { return {}; }
bool   PollKey(EFI_INPUT_KEY*)        { return false; }
void   FlushInput()                   {}
const char* Pad(const char*, int)     { return ""; }

void DrawText(int col, int row, const char* text, Color) {
    FakeRenderer::Record(col, row, text, false);
}
void DrawTextBg(int col, int row, const char* text, Color, Color) {
    FakeRenderer::Record(col, row, text, true);
}

} // namespace Renderer
