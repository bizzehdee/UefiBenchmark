#include "CoreSelection.h"
#include "SystemInfo.h"

namespace CoreSelection {

static ApInfo sAps[MAX_APS];
static UINT32 sCount = 0;
static bool   sIncludeBsp = false; // by default, BSD runs the EFI application, so we don't include it in the selection

void Init() {
    sCount = 0;

    auto* mp = SystemInfo::GetMpServices();
    if (!mp) return;

    UINTN total = 0, enabled = 0;
    if (EFI_ERROR(mp->GetNumberOfProcessors(mp, &total, &enabled))) return;

    UINTN bsp = 0;
    mp->WhoAmI(mp, &bsp);

    for (UINTN i = 0; i < total && sCount < MAX_APS; ++i) {
        if (i == bsp) continue;

        ApInfo ap = {};
        ap.ProcIndex = i;

        EFI_PROCESSOR_INFORMATION info = {};
        if (!EFI_ERROR(mp->GetProcessorInfo(mp, i, &info))) {
            ap.Available = (info.StatusFlag & PROCESSOR_ENABLED_BIT) != 0;
            ap.Package   = info.Location.Package;
            ap.Core      = info.Location.Core;
            ap.Thread    = info.Location.Thread;
        } else {
            ap.Available = true; // assume available if we can't query
        }
        ap.Selected = ap.Available;

        sAps[sCount++] = ap;
    }
}

UINT32  Count()  { return sCount; }
ApInfo* GetAll() { return sAps; }

UINT32 SelectedCount() {
    UINT32 n = 0;
    for (UINT32 i = 0; i < sCount; ++i)
        if (sAps[i].Selected && sAps[i].Available) ++n;
    return n;
}

UINT32 GetSelectedIndices(UINTN* out, UINT32 cap) {
    UINT32 n = 0;
    for (UINT32 i = 0; i < sCount && n < cap; ++i)
        if (sAps[i].Selected && sAps[i].Available)
            out[n++] = sAps[i].ProcIndex;
    return n;
}

void SelectAll() {
    for (UINT32 i = 0; i < sCount; ++i)
        sAps[i].Selected = sAps[i].Available;
}

void SelectPhysicalCoresOnly() {
    for (UINT32 i = 0; i < sCount; ++i) {
        if (!sAps[i].Available) { sAps[i].Selected = false; continue; }

        if (sAps[i].Thread == 0) {
            sAps[i].Selected = true;
            continue;
        }

        // Deselect hyperthreads: only keep Thread==0 per package+core.
        // If no Thread==0 sibling exists, keep this one (sole thread reported).
        bool siblingExists = false;
        for (UINT32 j = 0; j < sCount; ++j) {
            if (j == i) continue;
            if (sAps[j].Available     &&
                sAps[j].Package == sAps[i].Package &&
                sAps[j].Core    == sAps[i].Core    &&
                sAps[j].Thread  == 0) {
                siblingExists = true;
                break;
            }
        }
        sAps[i].Selected = !siblingExists;
    }
}

void SetIncludeBsp(bool include) { sIncludeBsp = include; }
bool GetIncludeBsp()             { return sIncludeBsp; }

#ifdef UEFI_HOST_TEST
void InjectRoster(const ApInfo* aps, UINT32 count) {
    sCount = count < MAX_APS ? count : MAX_APS;
    for (UINT32 i = 0; i < sCount; ++i) sAps[i] = aps[i];
    sIncludeBsp = false;
}
#endif

void SelectOnePerPackage() {
    for (UINT32 i = 0; i < sCount; ++i) {
        if (!sAps[i].Available) { sAps[i].Selected = false; continue; }
        bool isFirst = true;
        for (UINT32 j = 0; j < i; ++j) {
            if (sAps[j].Available && sAps[j].Package == sAps[i].Package) {
                isFirst = false; break;
            }
        }
        sAps[i].Selected = isFirst;
    }
}

} // namespace CoreSelection
