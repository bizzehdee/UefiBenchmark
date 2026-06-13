// Freestanding C++ runtime support: memory primitives and operator new/delete.

#include "Freestanding.h"

// ── Globals (set in EfiMain before any allocation) ───────────
EFI_SYSTEM_TABLE*  gST = nullptr;
EFI_BOOT_SERVICES* gBS = nullptr;
EFI_HANDLE         gImageHandle = nullptr;

extern "C" int _fltused = 0;
extern "C" int _purecall() {
    ConPrintLine("Fatal error: pure virtual function call.");
    for (;;) {}
}

// ── Memory primitives ────────────────────────────────────────
extern "C" {

void* memset(void* dest, int val, UINTN count) {
    UINT8* d = static_cast<UINT8*>(dest);
    UINT8 v = static_cast<UINT8>(val);
    for (UINTN i = 0; i < count; ++i)
        d[i] = v;
    return dest;
}

void* memcpy(void* dest, const void* src, UINTN count) {
    auto d = static_cast<UINT8*>(dest);
    auto s = static_cast<const UINT8*>(src);
    for (UINTN i = 0; i < count; ++i)
        d[i] = s[i];
    return dest;
}

void* memmove(void* dest, const void* src, UINTN count) {
    auto d = static_cast<UINT8*>(dest);
    auto s = static_cast<const UINT8*>(src);
    if (d < s) {
        for (UINTN i = 0; i < count; ++i)
            d[i] = s[i];
    } else if (d > s) {
        for (UINTN i = count; i > 0; --i)
            d[i - 1] = s[i - 1];
    }
    return dest;
}

int memcmp(const void* a, const void* b, UINTN count) {
    auto pa = static_cast<const UINT8*>(a);
    auto pb = static_cast<const UINT8*>(b);
    for (UINTN i = 0; i < count; ++i) {
        if (pa[i] != pb[i])
            return pa[i] < pb[i] ? -1 : 1;
    }
    return 0;
}

} // extern "C"

// ── Operator new / delete ────────────────────────────────────
void* operator new(UINTN size) {
    void* ptr = nullptr;
    if (gBS) {
        gBS->AllocatePool(EfiLoaderData, size, &ptr);
    }
    return ptr;
}

void* operator new[](UINTN size) {
    return operator new(size);
}

void operator delete(void* ptr) noexcept {
    if (ptr && gBS) {
        gBS->FreePool(ptr);
    }
}

void operator delete[](void* ptr) noexcept {
    operator delete(ptr);
}

void operator delete(void* ptr, UINTN) noexcept {
    operator delete(ptr);
}

void operator delete[](void* ptr, UINTN) noexcept {
    operator delete(ptr);
}

// ── String utilities ─────────────────────────────────────────
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
    bool neg = value < 0;
    UINT64 v = neg ? static_cast<UINT64>(-value) : static_cast<UINT64>(value);
    int pos = 23;
    sIntBuf[pos] = '\0';
    while (v > 0) {
        sIntBuf[--pos] = '0' + static_cast<char>(v % 10);
        v /= 10;
    }
    if (neg) sIntBuf[--pos] = '-';
    // Shift to start
    int len = 23 - pos;
    for (int i = 0; i <= len; ++i)
        sIntBuf[i] = sIntBuf[pos + i];
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
    for (int i = 0; i <= len; ++i)
        sIntBuf[i] = sIntBuf[pos + i];
    return sIntBuf;
}

// ── ConOut printing ──────────────────────────────────────────
void ConPrint(const char* str) {
    if (!gST || !gST->ConOut || !str) return;
    // Convert narrow to CHAR16 in small chunks
    CHAR16 buf[128];
    while (*str) {
        int i = 0;
        while (*str && i < 126) {
            buf[i++] = static_cast<CHAR16>(*str++);
        }
        buf[i] = 0;
        gST->ConOut->OutputString(gST->ConOut, buf);
    }
}

void ConPrintLine(const char* str) {
    ConPrint(str);
    CHAR16 nl[] = { '\r', '\n', 0 };
    if (gST && gST->ConOut)
        gST->ConOut->OutputString(gST->ConOut, nl);
}
