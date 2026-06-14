#pragma once
// Freestanding C++ support: memory primitives, minimal Vector<T>, string helpers.
// All dynamic allocation routes through UEFI Boot Services AllocatePool/FreePool.
// Vector<T> supports non-copyable move-aware element types.

#include "UefiTypes.h"

// ── Memory primitives ────────────────────────────────────────
extern "C" {
void* memset(void* dest, int val, UINTN count);
void* memcpy(void* dest, const void* src, UINTN count);
void* optimized_memcpy(void *dest, const void *src, UINTN count);
void* memmove(void* dest, const void* src, UINTN count);
int   memcmp(const void* a, const void* b, UINTN count);
}

// ── Operator new / delete (UEFI pool-backed) ─────────────────
void* operator new(UINTN size);
void* operator new[](UINTN size);
void  operator delete(void* ptr) noexcept;
void  operator delete[](void* ptr) noexcept;
void  operator delete(void* ptr, UINTN) noexcept;
void  operator delete[](void* ptr, UINTN) noexcept;

// Placement new
inline void* operator new(UINTN, void* ptr) noexcept { return ptr; }
inline void  operator delete(void*, void*)  noexcept {}

// ── String utilities ─────────────────────────────────────────
UINTN StrLen(const char* s);
int   StrCmp(const char* a, const char* b);
void  StrCopy(char* dest, const char* src, UINTN maxLen);

// Integer to decimal ASCII. Returns pointer into a static buffer.
const char* IntToStr(INT64 value);
const char* UintToStr(UINT64 value);
// Integer to zero-padded hex ASCII (digits = number of hex digits to show).
const char* HexToStr(UINT64 value, int digits);

// Print a narrow string via ConOut (converts to CHAR16 internally)
void ConPrint(const char* str);
void ConPrintLine(const char* str);

// ── Minimal move-aware Vector<T> ─────────────────────────────
// Requirements on T:
//   - T must be constructible in place with placement new.
//   - T should be movable for reallocation.
//   - alignof(T) must be <= 8 (UEFI AllocatePool alignment guarantee).
template<typename T>
class Vector {
public:
    Vector() : mData(nullptr), mSize(0), mCapacity(0) {}

    ~Vector() {
        DestroyRange(0, mSize);
        if (mData) operator delete(static_cast<void*>(mData));
    }

    // No copy (freestanding simplicity)
    Vector(const Vector&) = delete;
    Vector& operator=(const Vector&) = delete;

    // Move support
    Vector(Vector&& other) noexcept
        : mData(other.mData), mSize(other.mSize), mCapacity(other.mCapacity) {
        other.mData = nullptr;
        other.mSize = 0;
        other.mCapacity = 0;
    }

    Vector& operator=(Vector&& other) noexcept {
        if (this != &other) {
            DestroyRange(0, mSize);
            if (mData) operator delete(static_cast<void*>(mData));
            mData = other.mData;
            mSize = other.mSize;
            mCapacity = other.mCapacity;
            other.mData = nullptr;
            other.mSize = 0;
            other.mCapacity = 0;
        }
        return *this;
    }

    void PushBack(const T& value) {
        if (mSize >= mCapacity) Grow();
        new (mData + mSize) T(value);
        ++mSize;
    }

    void PushBack(T&& value) {
        if (mSize >= mCapacity) Grow();
        new (mData + mSize) T(static_cast<T&&>(value));
        ++mSize;
    }

    void Clear() {
        DestroyRange(0, mSize);
        mSize = 0;
    }

    UINTN Size() const { return mSize; }
    bool  Empty() const { return mSize == 0; }

    T&       operator[](UINTN i)       { return mData[i]; }
    const T& operator[](UINTN i) const { return mData[i]; }

    T*       Data()       { return mData; }
    const T* Data() const { return mData; }

    T*       Begin()       { return mData; }
    const T* Begin() const { return mData; }
    T*       End()         { return mData + mSize; }
    const T* End()   const { return mData + mSize; }

    void Reserve(UINTN cap) {
        if (cap <= mCapacity) return;
        T* newData = static_cast<T*>(operator new(cap * sizeof(T)));
        if (mData) {
            for (UINTN i = 0; i < mSize; ++i) {
                new (newData + i) T(static_cast<T&&>(mData[i]));
            }
            DestroyRange(0, mSize);
            operator delete(static_cast<void*>(mData));
        }
        mData = newData;
        mCapacity = cap;
    }

private:
    void Grow() {
        UINTN newCap = mCapacity == 0 ? 8 : mCapacity * 2;
        Reserve(newCap);
    }

    void DestroyRange(UINTN begin, UINTN end) {
        for (UINTN i = begin; i < end; ++i) {
            mData[i].~T();
        }
    }

    T*    mData;
    UINTN mSize;
    UINTN mCapacity;
};
