#include "RunConfig.h"

namespace RunConfig {

// 0 means "unset" → the getter reports the default. Zero-initialised storage, so
// no runtime constructor is required (matches the freestanding model).
static UINT32 sTestMinutes   = 0;
static UINT32 sStressMinutes = 0;

static UINT32 Clamp(UINT32 m) {
    if (m < kMinMinutes) m = kMinMinutes;
    if (m > kMaxMinutes) m = kMaxMinutes;
    return m;
}

UINT32 GetTestMinutes()        { return sTestMinutes ? sTestMinutes : kTestDefaultMinutes; }
void   SetTestMinutes(UINT32 m){ sTestMinutes = Clamp(m); }
UINT64 GetTestBudgetUs()       { return (UINT64)GetTestMinutes() * 60ULL * US_PER_SECOND; }

UINT32 GetStressMinutes()        { return sStressMinutes ? sStressMinutes : kStressDefaultMinutes; }
void   SetStressMinutes(UINT32 m){ sStressMinutes = Clamp(m); }
UINT64 GetStressBudgetUs()       { return (UINT64)GetStressMinutes() * 60ULL * US_PER_SECOND; }

void   Reset()                   { sTestMinutes = 0; sStressMinutes = 0; }

}  // namespace RunConfig
