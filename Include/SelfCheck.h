#pragma once
// Startup self-validation: cheap load-sanity sentinels (#4) plus a CRC32 of the
// in-memory .text section (#2) compared against an expected value embedded at
// build time by tools/patch_selfcrc.py.
//
// This guards against ACCIDENTAL corruption (bad media, truncated copy, flaky
// RAM) — not tampering (a corrupted binary can lie about its own hash; use
// Secure Boot signing for that). If the binary was not provisioned with an
// expected CRC, the CRC step is skipped and only the sentinels run.

#include "UefiTypes.h"

namespace SelfCheck {

// Returns true if the image looks correctly loaded. On failure, *outReason
// (if non-null) is set to a short static description. Requires gBS and
// gImageHandle to be set first.
bool Verify(const char** outReason);

// True if Secure Boot is enabled and enforcing (PK enrolled). When true, the
// firmware verified this image's signature before launching it, so the running
// binary is signed and trusted. Reads the SecureBoot/SetupMode UEFI variables.
bool SecureBootSigned();

}  // namespace SelfCheck
