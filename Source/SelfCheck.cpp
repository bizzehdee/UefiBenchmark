// Startup self-validation. See SelfCheck.h for rationale.
//
// #2 — CRC32 over the in-memory .text section vs an expected value embedded by
//      tools/patch_selfcrc.py. We hash .text only: x64 EFI code is RIP-relative
//      and therefore relocation-free, so its bytes are identical whatever base
//      the firmware loads us at. (.rdata/.data hold relocated pointers — e.g.
//      vtables — so they differ between file and memory and are NOT hashed.)
// #4 — cheap sentinels: a .rodata constant, the marker blob, the PE headers,
//      and a plausible image size.

#include "SelfCheck.h"
#include "Freestanding.h"   // gBS, gImageHandle

// ── Marker blob (patched post-build) ──────────────────────────
// The patch tool locates this by scanning the file for the 16-byte Magic, then
// writes the expected .text CRC and sets Flags bit0. Lives in initialised data,
// so it is excluded from the .text hash, and the magic must appear EXACTLY ONCE
// in the binary — hence the per-byte literal comparison in Verify() rather than
// a second contiguous magic constant the scan could mistakenly patch.
struct SelfCheckBlob {
    UINT8  Magic[16];
    UINT32 ExpectedCrc;
    UINT32 Flags;       // bit0: provisioned (ExpectedCrc valid)
};

volatile SelfCheckBlob gSelfCheck = {
    { 'U','e','f','i','S','e','l','f','C','h','k','!', 0xA5, 0x5A, 0xC3, 0x3C },
    0u, 0u
};

// .rodata load sentinel (volatile so it is actually read, not folded away).
static const volatile UINT32 kRodataSentinel = 0xC0DEFACEu;

namespace {

// Standard CRC-32 (poly 0xEDB88320, refin/refout, init/xorout 0xFFFFFFFF) —
// matches Python's zlib.crc32 used by the patch tool.
UINT32 Crc32(const UINT8* data, UINTN len) {
    UINT32 crc = 0xFFFFFFFFu;
    for (UINTN i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b)
            crc = (crc >> 1) ^ (0xEDB88320u & (~(crc & 1u) + 1u));  // -(crc&1)
    }
    return ~crc;
}

inline UINT16 Rd16(const UINT8* p) { return (UINT16)(p[0] | (p[1] << 8)); }
inline UINT32 Rd32(const UINT8* p) {
    return (UINT32)p[0] | ((UINT32)p[1] << 8) | ((UINT32)p[2] << 16) | ((UINT32)p[3] << 24);
}

// EFI_LOADED_IMAGE_PROTOCOL (truncated to the fields we need).
struct LoadedImageProto {
    UINT32     Revision;
    EFI_HANDLE ParentHandle;
    VOID*      SystemTable;
    EFI_HANDLE DeviceHandle;
    VOID*      FilePath;
    VOID*      Reserved;
    UINT32     LoadOptionsSize;
    VOID*      LoadOptions;
    VOID*      ImageBase;
    UINT64     ImageSize;
    // ... ImageCodeType / ImageDataType / Unload omitted
};

using HandleProtocolFn = EFI_STATUS (EFIAPI*)(EFI_HANDLE, EFI_GUID*, VOID**);

}  // namespace

namespace SelfCheck {

bool Verify(const char** outReason) {
    auto fail = [&](const char* r) -> bool { if (outReason) *outReason = r; return false; };

    // #4 — .rodata sentinel: read-only data mapped correctly.
    if (kRodataSentinel != 0xC0DEFACEu) return fail("read-only data sentinel mismatch");

    // #4 — marker blob intact: initialised data mapped correctly. Compared via
    // individual literals (not a contiguous constant) so the 16-byte magic
    // exists only once in the binary, where the patch tool expects it.
    {
        const volatile UINT8* m = gSelfCheck.Magic;
        bool ok = m[0]=='U' && m[1]=='e' && m[2]=='f' && m[3]=='i' &&
                  m[4]=='S' && m[5]=='e' && m[6]=='l' && m[7]=='f' &&
                  m[8]=='C' && m[9]=='h' && m[10]=='k' && m[11]=='!' &&
                  m[12]==0xA5 && m[13]==0x5A && m[14]==0xC3 && m[15]==0x3C;
        if (!ok) return fail("self-check marker corrupt");
    }

    if (!gBS || !gImageHandle) return fail("boot services unavailable");

    // Locate our own loaded image to find the in-memory base + size.
    EFI_GUID liGuid = { 0x5B1B31A1, 0x9562, 0x11D2,
                        { 0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B } };
    LoadedImageProto* li = nullptr;
    auto HandleProtocol = reinterpret_cast<HandleProtocolFn>(gBS->HandleProtocol);
    if (HandleProtocol(gImageHandle, &liGuid, reinterpret_cast<VOID**>(&li)) != EFI_SUCCESS || !li)
        return fail("LoadedImage protocol unavailable");

    const UINT8* base    = reinterpret_cast<const UINT8*>(li->ImageBase);
    UINT64       imgSize = li->ImageSize;
    if (!base) return fail("null image base");
    if (imgSize < 0x1000 || imgSize > 0x4000000) return fail("implausible image size");

    // #4 — PE headers parse cleanly.
    if (Rd16(base) != 0x5A4D) return fail("bad DOS signature");            // 'MZ'
    UINT32 peOff = Rd32(base + 0x3C);
    if ((UINT64)peOff + 24 > imgSize) return fail("bad PE header offset");
    if (Rd32(base + peOff) != 0x00004550) return fail("bad PE signature");  // 'PE\0\0'

    const UINT8* coff  = base + peOff + 4;
    UINT16       nSec  = Rd16(coff + 2);
    UINT16       optSz = Rd16(coff + 16);
    const UINT8* sec   = coff + 20 + optSz;

    const UINT8* textPtr = nullptr;
    UINT32       textLen = 0;
    for (UINT16 i = 0; i < nSec; ++i) {
        const UINT8* sh = sec + (UINTN)i * 40;
        if (sh[0] == '.' && sh[1] == 't' && sh[2] == 'e' &&
            sh[3] == 'x' && sh[4] == 't' && sh[5] == 0) {
            UINT32 vSize = Rd32(sh + 8);
            UINT32 vAddr = Rd32(sh + 12);
            textPtr = base + vAddr;
            textLen = vSize;
            break;
        }
    }
    if (!textPtr || textLen == 0) return fail(".text section not found");
    if ((UINT64)(textPtr - base) + textLen > imgSize) return fail(".text out of bounds");

    // #2 — CRC check, only when the binary was provisioned by the patch tool.
    if (gSelfCheck.Flags & 1u) {
        UINT32 crc = Crc32(textPtr, textLen);
        if (crc != gSelfCheck.ExpectedCrc) return fail(".text CRC mismatch - binary corrupt");
    }
    // Unprovisioned (e.g. dev build without the post-build step): sentinels only.

    return true;
}

bool SecureBootSigned() {
    if (!gST || !gST->RuntimeServices) return false;

    using GetVariableFn = EFI_STATUS (EFIAPI*)(CHAR16*, EFI_GUID*, UINT32*, UINTN*, VOID*);
    auto GetVariable = reinterpret_cast<GetVariableFn>(gST->RuntimeServices->GetVariable);
    if (!GetVariable) return false;

    // EFI_GLOBAL_VARIABLE namespace.
    EFI_GUID gv = { 0x8BE4DF61, 0x93CA, 0x11D2,
                    { 0xAA, 0x0D, 0x00, 0xE0, 0x98, 0x03, 0x2B, 0x8C } };
    static CHAR16 nSecureBoot[] = { 'S','e','c','u','r','e','B','o','o','t', 0 };
    static CHAR16 nSetupMode[]  = { 'S','e','t','u','p','M','o','d','e', 0 };

    UINT8  sb = 0, setup = 0;
    UINT32 attr = 0;
    UINTN  sz;

    sz = sizeof(sb);
    if (GetVariable(nSecureBoot, &gv, &attr, &sz, &sb) != EFI_SUCCESS)
        return false;  // variable absent → Secure Boot not in force

    // SetupMode may be missing on some firmware; absent ⇒ treat as not-setup.
    sz = sizeof(setup);
    if (GetVariable(nSetupMode, &gv, &attr, &sz, &setup) != EFI_SUCCESS)
        setup = 0;

    return sb == 1 && setup == 0;
}

}  // namespace SelfCheck
