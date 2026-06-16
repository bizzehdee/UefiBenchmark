// Tests for CoreSelection preset logic: SelectAll, SelectPhysicalCoresOnly,
// SelectOnePerPackage, GetSelectedIndices, and SetIncludeBsp.
// Uses InjectRoster() to supply a synthetic AP list — bypasses Init() / MP Services.

#include "doctest.h"
#include "Freestanding.h"
#include "CoreSelection.h"

using namespace CoreSelection;

// ── Roster helpers ───────────────────────────────────────────────────────────

static ApInfo ap(UINTN idx, UINT32 pkg, UINT32 core, UINT32 thread, bool avail) {
    ApInfo a = {};
    a.ProcIndex = idx;
    a.Package   = pkg;
    a.Core      = core;
    a.Thread    = thread;
    a.Available = avail;
    a.Selected  = avail;
    return a;
}

// ── SelectAll ────────────────────────────────────────────────────────────────

TEST_CASE("CoreSelection::SelectAll: selects every available AP") {
    ApInfo roster[4] = {
        ap(1, 0, 0, 0, true),
        ap(2, 0, 0, 1, true),
        ap(3, 0, 1, 0, false),  // unavailable
        ap(4, 0, 1, 1, true),
    };
    InjectRoster(roster, 4);
    SelectAll();
    // Available APs are selected; unavailable is not.
    CHECK(GetAll()[0].Selected == true);
    CHECK(GetAll()[1].Selected == true);
    CHECK(GetAll()[2].Selected == false);
    CHECK(GetAll()[3].Selected == true);
    CHECK(SelectedCount() == 3);
}

TEST_CASE("CoreSelection::SelectAll: unavailable AP never selected") {
    ApInfo roster[2] = {
        ap(1, 0, 0, 0, false),
        ap(2, 0, 1, 0, true),
    };
    InjectRoster(roster, 2);
    SelectAll();
    CHECK(SelectedCount() == 1);
    CHECK(GetAll()[0].Selected == false);
}

// ── SelectPhysicalCoresOnly ──────────────────────────────────────────────────

TEST_CASE("CoreSelection::SelectPhysicalCoresOnly: keeps Thread==0 per core") {
    // 4-thread roster: 2 cores × 2 threads on pkg0.
    ApInfo roster[4] = {
        ap(1, 0, 0, 0, true),   // core0 t0 → keep
        ap(2, 0, 0, 1, true),   // core0 t1 → deselect (sibling t0 exists)
        ap(3, 0, 1, 0, true),   // core1 t0 → keep
        ap(4, 0, 1, 1, true),   // core1 t1 → deselect
    };
    InjectRoster(roster, 4);
    SelectPhysicalCoresOnly();
    CHECK(GetAll()[0].Selected == true);
    CHECK(GetAll()[1].Selected == false);
    CHECK(GetAll()[2].Selected == true);
    CHECK(GetAll()[3].Selected == false);
    CHECK(SelectedCount() == 2);
}

TEST_CASE("CoreSelection::SelectPhysicalCoresOnly: sole thread on a core is kept") {
    // core0 has only t1 (no t0 sibling) → keep it despite Thread != 0.
    ApInfo roster[2] = {
        ap(1, 0, 0, 1, true),   // sole thread; no t0 sibling → keep
        ap(2, 0, 1, 0, true),   // normal t0 → keep
    };
    InjectRoster(roster, 2);
    SelectPhysicalCoresOnly();
    CHECK(GetAll()[0].Selected == true);
    CHECK(GetAll()[1].Selected == true);
    CHECK(SelectedCount() == 2);
}

TEST_CASE("CoreSelection::SelectPhysicalCoresOnly: unavailable AP never selected") {
    ApInfo roster[4] = {
        ap(1, 0, 0, 0, false),  // unavailable t0
        ap(2, 0, 0, 1, true),   // t1 available; t0 sibling is unavailable → sole available
        ap(3, 0, 1, 0, true),
        ap(4, 0, 1, 1, true),
    };
    InjectRoster(roster, 4);
    SelectPhysicalCoresOnly();
    CHECK(GetAll()[0].Selected == false);  // unavailable
    // AP 2 (t1) has an unavailable t0 sibling, so no available t0 exists → keep t1
    CHECK(GetAll()[1].Selected == true);
    CHECK(GetAll()[2].Selected == true);
    CHECK(GetAll()[3].Selected == false);
}

// ── SelectOnePerPackage ──────────────────────────────────────────────────────

TEST_CASE("CoreSelection::SelectOnePerPackage: one selected per socket") {
    ApInfo roster[5] = {
        ap(1, 0, 0, 0, true),  // pkg0 first → keep
        ap(2, 0, 1, 0, true),  // pkg0 second → drop
        ap(3, 0, 2, 0, true),  // pkg0 third → drop
        ap(4, 1, 0, 0, true),  // pkg1 first → keep
        ap(5, 1, 1, 0, true),  // pkg1 second → drop
    };
    InjectRoster(roster, 5);
    SelectOnePerPackage();
    CHECK(GetAll()[0].Selected == true);
    CHECK(GetAll()[1].Selected == false);
    CHECK(GetAll()[2].Selected == false);
    CHECK(GetAll()[3].Selected == true);
    CHECK(GetAll()[4].Selected == false);
    CHECK(SelectedCount() == 2);
}

TEST_CASE("CoreSelection::SelectOnePerPackage: unavailable AP never selected") {
    ApInfo roster[3] = {
        ap(1, 0, 0, 0, false),  // pkg0 first but unavailable
        ap(2, 0, 1, 0, true),   // pkg0 second, available → selected (first available)
        ap(3, 1, 0, 0, true),   // pkg1 → selected
    };
    InjectRoster(roster, 3);
    SelectOnePerPackage();
    CHECK(GetAll()[0].Selected == false);
    CHECK(GetAll()[1].Selected == true);
    CHECK(GetAll()[2].Selected == true);
    CHECK(SelectedCount() == 2);
}

// ── GetSelectedIndices ───────────────────────────────────────────────────────

TEST_CASE("CoreSelection::GetSelectedIndices: returns ProcIndex of selected+available APs") {
    ApInfo roster[3] = {
        ap(10, 0, 0, 0, true),
        ap(20, 0, 1, 0, true),
        ap(30, 0, 2, 0, false),  // unavailable
    };
    InjectRoster(roster, 3);
    SelectAll();

    UINTN out[4] = {0xDEAD, 0xDEAD, 0xDEAD, 0xDEAD};
    UINT32 n = GetSelectedIndices(out, 4);
    CHECK(n == 2);
    CHECK(out[0] == 10);
    CHECK(out[1] == 20);
    CHECK(out[2] == (UINTN)0xDEAD);  // not written past count
}

TEST_CASE("CoreSelection::GetSelectedIndices: respects cap (truncation)") {
    ApInfo roster[3] = {
        ap(1, 0, 0, 0, true),
        ap(2, 0, 1, 0, true),
        ap(3, 0, 2, 0, true),
    };
    InjectRoster(roster, 3);
    SelectAll();

    UINTN out[4] = {};
    UINT32 n = GetSelectedIndices(out, 2);
    CHECK(n == 2);  // capped, not 3
}

// ── SetIncludeBsp / GetIncludeBsp ────────────────────────────────────────────

TEST_CASE("CoreSelection::SetIncludeBsp/GetIncludeBsp round-trip") {
    InjectRoster(nullptr, 0);  // reset state
    SetIncludeBsp(true);
    CHECK(GetIncludeBsp() == true);
    SetIncludeBsp(false);
    CHECK(GetIncludeBsp() == false);
}

TEST_CASE("CoreSelection::InjectRoster resets IncludeBsp to false") {
    SetIncludeBsp(true);
    ApInfo dummy = ap(1, 0, 0, 0, true);
    InjectRoster(&dummy, 1);
    CHECK(GetIncludeBsp() == false);
}
