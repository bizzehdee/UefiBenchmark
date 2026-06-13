#pragma once
// Min / Max / Average helpers for POD Vector<UINT64>.

#include "UefiTypes.h"
#include "Freestanding.h"

namespace Stats {

inline UINT64 GetMin(const Vector<UINT64>& v) {
    if (v.Empty()) return 0;
    UINT64 m = v[0];
    for (UINTN i = 1; i < v.Size(); ++i)
        if (v[i] < m) m = v[i];
    return m;
}

inline UINT64 GetMax(const Vector<UINT64>& v) {
    if (v.Empty()) return 0;
    UINT64 m = v[0];
    for (UINTN i = 1; i < v.Size(); ++i)
        if (v[i] > m) m = v[i];
    return m;
}

inline UINT64 GetAverage(const Vector<UINT64>& v) {
    if (v.Empty()) return 0;
    UINT64 sum = 0;
    for (UINTN i = 0; i < v.Size(); ++i)
        sum += v[i];
    return sum / v.Size();
}

inline UINT64 GetSum(const Vector<UINT64>& v) {
    UINT64 sum = 0;
    for (UINTN i = 0; i < v.Size(); ++i)
        sum += v[i];
    return sum;
}

} // namespace Stats
