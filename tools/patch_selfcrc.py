#!/usr/bin/env python3
# Post-build self-check provisioning for UefiBenchmark.
#
# Computes CRC-32 over the in-memory image of the PE .text section and writes it
# into the SelfCheckBlob (located by its 16-byte magic) so the app can verify at
# startup that .text loaded intact. See Source/SelfCheck.cpp.
#
# .text is hashed because x64 EFI code is RIP-relative and thus relocation-free,
# so its bytes are identical whatever base the firmware loads at. If any base
# relocation lands inside .text (it shouldn't), the binary is left UNprovisioned
# (Flags bit0 = 0) so the runtime falls back to sentinel-only checks instead of
# failing on a spurious CRC mismatch.
#
# Usage: patch_selfcrc.py <path-to.efi>
# Idempotent: re-running recomputes and rewrites the same value.

import struct
import sys
import zlib

# Must match Source/SelfCheck.cpp gSelfCheck.Magic exactly.
MAGIC = bytes([ord(c) for c in "UefiSelfChk!"]) + bytes([0xA5, 0x5A, 0xC3, 0x3C])


def fail(msg):
    sys.stderr.write("  [selfcheck] ERROR: %s\n" % msg)
    sys.exit(1)


def rva_to_off(sections, rva):
    for s in sections:
        size = max(s["vsize"], s["rawsz"])
        if s["vaddr"] <= rva < s["vaddr"] + size:
            return s["rawptr"] + (rva - s["vaddr"])
    return None


def main():
    if len(sys.argv) != 2:
        fail("usage: patch_selfcrc.py <path-to.efi>")
    path = sys.argv[1]

    with open(path, "rb") as f:
        data = bytearray(f.read())

    # ── Parse PE headers ──────────────────────────────────────
    if data[0:2] != b"MZ":
        fail("not a PE/MZ image: %s" % path)
    pe_off = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe_off:pe_off + 4] != b"PE\0\0":
        fail("bad PE signature")
    coff = pe_off + 4
    n_sec = struct.unpack_from("<H", data, coff + 2)[0]
    opt_sz = struct.unpack_from("<H", data, coff + 16)[0]
    opt = coff + 20
    magic = struct.unpack_from("<H", data, opt)[0]
    if magic != 0x20B:
        fail("expected PE32+ (0x20B), got 0x%X" % magic)

    # DataDirectory[5] = Base Relocation Table (offset 152 within optional hdr).
    reloc_rva = struct.unpack_from("<I", data, opt + 152)[0]
    reloc_size = struct.unpack_from("<I", data, opt + 156)[0]

    sec_tbl = opt + opt_sz
    sections = []
    text = None
    for i in range(n_sec):
        sh = sec_tbl + i * 40
        name = bytes(data[sh:sh + 8]).rstrip(b"\0")
        s = {
            "name": name,
            "vsize": struct.unpack_from("<I", data, sh + 8)[0],
            "vaddr": struct.unpack_from("<I", data, sh + 12)[0],
            "rawsz": struct.unpack_from("<I", data, sh + 16)[0],
            "rawptr": struct.unpack_from("<I", data, sh + 20)[0],
        }
        sections.append(s)
        if name == b".text":
            text = s
    if text is None:
        fail(".text section not found")

    # ── In-memory bytes of .text (raw data, zero-padded to VirtualSize) ──
    vsize = text["vsize"]
    n_raw = min(vsize, text["rawsz"])
    start = text["rawptr"]
    text_bytes = bytes(data[start:start + n_raw]) + b"\0" * (vsize - n_raw)
    crc = zlib.crc32(text_bytes) & 0xFFFFFFFF

    # ── Check for relocations inside .text ────────────────────
    text_lo = text["vaddr"]
    text_hi = text["vaddr"] + vsize
    text_has_reloc = False
    if reloc_size > 0:
        off = rva_to_off(sections, reloc_rva)
        if off is not None:
            end = off + reloc_size
            pos = off
            while pos + 8 <= end:
                page_rva, block_sz = struct.unpack_from("<II", data, pos)
                if block_sz < 8:
                    break
                n_entries = (block_sz - 8) // 2
                for e in range(n_entries):
                    entry = struct.unpack_from("<H", data, pos + 8 + e * 2)[0]
                    typ = entry >> 12
                    if typ == 0:  # IMAGE_REL_BASED_ABSOLUTE = padding
                        continue
                    r = page_rva + (entry & 0xFFF)
                    if text_lo <= r < text_hi:
                        text_has_reloc = True
                        break
                if text_has_reloc:
                    break
                pos += block_sz

    # ── Locate the marker blob and patch it ───────────────────
    idx = data.find(MAGIC)
    if idx < 0:
        fail("self-check marker not found (is Source/SelfCheck.cpp linked in?)")
    if data.find(MAGIC, idx + 1) >= 0:
        fail("self-check marker found more than once (magic is not unique)")

    flags = 0 if text_has_reloc else 1
    struct.pack_into("<I", data, idx + 16, crc)     # ExpectedCrc
    struct.pack_into("<I", data, idx + 20, flags)   # Flags

    with open(path, "wb") as f:
        f.write(data)

    if text_has_reloc:
        sys.stderr.write(
            "  [selfcheck] WARNING: relocations inside .text — left UNprovisioned "
            "(runtime uses sentinel checks only)\n")
    else:
        print("  [selfcheck] .text CRC32 = 0x%08X (%u bytes) embedded" % (crc, vsize))


if __name__ == "__main__":
    main()
