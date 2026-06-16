// Tests for Vector<T> — the move-aware freestanding container (Freestanding.h).

#include "doctest.h"
#include "Freestanding.h"

// ── Lifecycle counter for observing destructor calls ─────────────────────────
struct Counted {
    static int sAlive;
    int val;
    explicit Counted(int v = 0) : val(v) { ++sAlive; }
    Counted(const Counted& o)   : val(o.val) { ++sAlive; }
    Counted(Counted&& o) noexcept : val(o.val) { ++sAlive; o.val = -1; }
    ~Counted() { --sAlive; }
};
int Counted::sAlive = 0;

// Move-only type to exercise the move-construct reallocation path.
struct MoveOnly {
    int val;
    explicit MoveOnly(int v) : val(v) {}
    MoveOnly(const MoveOnly&) = delete;
    MoveOnly& operator=(const MoveOnly&) = delete;
    MoveOnly(MoveOnly&& o) noexcept : val(o.val) { o.val = -1; }
    MoveOnly& operator=(MoveOnly&&) = default;
};

// ── Empty / initial state ─────────────────────────────────────────────────────

TEST_CASE("Vector: starts empty") {
    Vector<int> v;
    CHECK(v.Size()  == 0);
    CHECK(v.Empty() == true);
}

// ── PushBack ─────────────────────────────────────────────────────────────────

TEST_CASE("Vector: PushBack grows size by one each call") {
    Vector<int> v;
    v.PushBack(1);
    CHECK(v.Size() == 1);
    v.PushBack(2);
    CHECK(v.Size() == 2);
}

TEST_CASE("Vector: PushBack preserves element order across reallocs") {
    Vector<int> v;
    const int N = 64;   // forces several Grow() calls (initial cap = 8)
    for (int i = 0; i < N; ++i) v.PushBack(i);
    CHECK(v.Size() == static_cast<UINTN>(N));
    for (int i = 0; i < N; ++i) CHECK(v[i] == i);
}

TEST_CASE("Vector: PushBack with move-only type survives reallocation") {
    Vector<MoveOnly> v;
    const int N = 20;
    for (int i = 0; i < N; ++i) v.PushBack(MoveOnly(i));
    for (int i = 0; i < N; ++i) CHECK(v[i].val == i);
}

// ── Reserve ───────────────────────────────────────────────────────────────────

TEST_CASE("Vector: Reserve makes capacity >= requested without changing size") {
    Vector<int> v;
    v.PushBack(1);
    v.PushBack(2);
    v.Reserve(100);
    CHECK(v.Size() == 2);
    CHECK(v[0] == 1);
    CHECK(v[1] == 2);
}

TEST_CASE("Vector: Reserve smaller than current capacity is a no-op") {
    Vector<int> v;
    for (int i = 0; i < 10; ++i) v.PushBack(i);
    v.Reserve(2);   // already has capacity >= 10
    CHECK(v.Size() == 10);
    for (int i = 0; i < 10; ++i) CHECK(v[i] == i);
}

// ── Clear ─────────────────────────────────────────────────────────────────────

TEST_CASE("Vector: Clear resets size to 0 and runs destructors") {
    Counted::sAlive = 0;
    {
        Vector<Counted> v;
        v.PushBack(Counted(1));
        v.PushBack(Counted(2));
        CHECK(Counted::sAlive == 2);
        v.Clear();
        CHECK(v.Size()  == 0);
        CHECK(v.Empty() == true);
        CHECK(Counted::sAlive == 0);
    }
}

TEST_CASE("Vector: Clear leaves vector reusable") {
    Vector<int> v;
    v.PushBack(5);
    v.Clear();
    v.PushBack(7);
    CHECK(v.Size() == 1);
    CHECK(v[0]     == 7);
}

// ── Move semantics ────────────────────────────────────────────────────────────

TEST_CASE("Vector: move-construct transfers ownership; source becomes empty") {
    Vector<int> a;
    a.PushBack(10);
    a.PushBack(20);
    Vector<int> b(static_cast<Vector<int>&&>(a));
    CHECK(a.Size() == 0);
    CHECK(a.Empty());
    CHECK(b.Size() == 2);
    CHECK(b[0] == 10);
    CHECK(b[1] == 20);
}

TEST_CASE("Vector: move-assign transfers ownership; source becomes empty") {
    Vector<int> a;
    a.PushBack(99);
    Vector<int> b;
    b = static_cast<Vector<int>&&>(a);
    CHECK(a.Empty());
    CHECK(b.Size() == 1);
    CHECK(b[0] == 99);
}

TEST_CASE("Vector: self-move-assign is safe") {
    Vector<int> v;
    v.PushBack(42);
    // UB in the standard but the implementation explicitly guards (this != &other).
    Vector<int>& ref = v;
    v = static_cast<Vector<int>&&>(ref);
    CHECK(v.Size() == 1);
    CHECK(v[0] == 42);
}

// ── Destructor ────────────────────────────────────────────────────────────────

TEST_CASE("Vector: destructor destroys every live element exactly once") {
    Counted::sAlive = 0;
    {
        Vector<Counted> v;
        for (int i = 0; i < 12; ++i) v.PushBack(Counted(i));
        CHECK(Counted::sAlive == 12);
    }
    CHECK(Counted::sAlive == 0);   // no leak, no double-destroy
}
