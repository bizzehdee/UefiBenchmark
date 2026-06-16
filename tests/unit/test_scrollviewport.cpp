// Tests for ScrollViewport scroll state, key handling, and Render() output.
// Uses FakeRenderer from UefiShim to record DrawText/DrawTextBg calls.

#include "doctest.h"
#include "Freestanding.h"
#include "ScrollViewport.h"
#include "host/UefiShim.h"

static constexpr Color kFg  = Color(210, 210, 210);
static constexpr Color kBg  = Color(0, 70, 90);

// Key helpers.
static EFI_INPUT_KEY scanKey(UINT16 code) {
    EFI_INPUT_KEY k = {};
    k.ScanCode = code;
    return k;
}

// ── Initial state ────────────────────────────────────────────────────────────

TEST_CASE("ScrollViewport: fresh viewport has TotalLines==0 and ScrollPos==0") {
    ScrollViewport vp;
    CHECK(vp.TotalLines() == 0);
    CHECK(vp.ScrollPos()  == 0);
}

// ── Clear / ClearContent ─────────────────────────────────────────────────────

TEST_CASE("ScrollViewport::Clear: resets both line count and scroll position") {
    ScrollViewport vp;
    vp.AddLine("hello", kFg);
    // Drive scroll down first so ScrollPos > 0
    for (int i = 0; i < 20; ++i) vp.AddLine("x", kFg);
    vp.HandleKey(scanKey(SCAN_END), 5);
    CHECK(vp.ScrollPos() > 0);
    vp.Clear();
    CHECK(vp.TotalLines() == 0);
    CHECK(vp.ScrollPos()  == 0);
}

TEST_CASE("ScrollViewport::ClearContent: resets line count but preserves scroll") {
    ScrollViewport vp;
    for (int i = 0; i < 20; ++i) vp.AddLine("x", kFg);
    vp.HandleKey(scanKey(SCAN_END), 5);
    int savedScroll = vp.ScrollPos();
    CHECK(savedScroll > 0);

    vp.ClearContent();
    CHECK(vp.TotalLines() == 0);
    CHECK(vp.ScrollPos()  == savedScroll);  // preserved
}

// ── AddLine ──────────────────────────────────────────────────────────────────

TEST_CASE("ScrollViewport::AddLine(text, fg): increments TotalLines") {
    ScrollViewport vp;
    vp.AddLine("first", kFg);
    CHECK(vp.TotalLines() == 1);
    vp.AddLine("second", kFg);
    CHECK(vp.TotalLines() == 2);
}

TEST_CASE("ScrollViewport::AddLine (blank overload): increments TotalLines") {
    ScrollViewport vp;
    vp.AddLine();
    CHECK(vp.TotalLines() == 1);
}

TEST_CASE("ScrollViewport::AddLine: past MAX_LINES cap is silently dropped") {
    ScrollViewport vp;
    for (int i = 0; i < ScrollViewport::MAX_LINES + 5; ++i)
        vp.AddLine("x", kFg);
    CHECK(vp.TotalLines() == ScrollViewport::MAX_LINES);
}

// ── HandleKey return value ───────────────────────────────────────────────────

TEST_CASE("ScrollViewport::HandleKey: scroll keys return true") {
    ScrollViewport vp;
    for (int i = 0; i < 30; ++i) vp.AddLine("x", kFg);
    CHECK(vp.HandleKey(scanKey(SCAN_UP),        10) == true);
    CHECK(vp.HandleKey(scanKey(SCAN_DOWN),      10) == true);
    CHECK(vp.HandleKey(scanKey(SCAN_PAGE_UP),   10) == true);
    CHECK(vp.HandleKey(scanKey(SCAN_PAGE_DOWN), 10) == true);
    CHECK(vp.HandleKey(scanKey(SCAN_HOME),      10) == true);
    CHECK(vp.HandleKey(scanKey(SCAN_END),       10) == true);
}

TEST_CASE("ScrollViewport::HandleKey: non-scroll keys return false") {
    ScrollViewport vp;
    EFI_INPUT_KEY k = {};
    k.ScanCode     = SCAN_NULL;
    k.UnicodeChar  = 'a';
    CHECK(vp.HandleKey(k, 10) == false);

    k.ScanCode    = SCAN_ESC;
    k.UnicodeChar = 0;
    CHECK(vp.HandleKey(k, 10) == false);
}

// ── Up / Down clamping ───────────────────────────────────────────────────────

TEST_CASE("ScrollViewport: Up at top stays at ScrollPos==0") {
    ScrollViewport vp;
    vp.AddLine("x", kFg);
    vp.HandleKey(scanKey(SCAN_UP), 5);
    CHECK(vp.ScrollPos() == 0);
}

TEST_CASE("ScrollViewport: Down at bottom stays at last page") {
    ScrollViewport vp;
    for (int i = 0; i < 10; ++i) vp.AddLine("x", kFg);
    const int viewRows = 5;
    // Drive to bottom
    for (int i = 0; i < 20; ++i) vp.HandleKey(scanKey(SCAN_DOWN), viewRows);
    int maxScroll = vp.TotalLines() - viewRows;
    CHECK(vp.ScrollPos() == maxScroll);
    // One more Down should not increase it.
    vp.HandleKey(scanKey(SCAN_DOWN), viewRows);
    CHECK(vp.ScrollPos() == maxScroll);
}

TEST_CASE("ScrollViewport: Down increments scroll when not at bottom") {
    ScrollViewport vp;
    for (int i = 0; i < 20; ++i) vp.AddLine("x", kFg);
    CHECK(vp.ScrollPos() == 0);
    vp.HandleKey(scanKey(SCAN_DOWN), 5);
    CHECK(vp.ScrollPos() == 1);
}

TEST_CASE("ScrollViewport: Up decrements scroll when not at top") {
    ScrollViewport vp;
    for (int i = 0; i < 20; ++i) vp.AddLine("x", kFg);
    vp.HandleKey(scanKey(SCAN_DOWN), 5);
    vp.HandleKey(scanKey(SCAN_DOWN), 5);
    CHECK(vp.ScrollPos() == 2);
    vp.HandleKey(scanKey(SCAN_UP), 5);
    CHECK(vp.ScrollPos() == 1);
}

// ── PageUp / PageDown ────────────────────────────────────────────────────────

TEST_CASE("ScrollViewport: PageDown moves by viewRows") {
    ScrollViewport vp;
    for (int i = 0; i < 30; ++i) vp.AddLine("x", kFg);
    const int vr = 8;
    vp.HandleKey(scanKey(SCAN_PAGE_DOWN), vr);
    CHECK(vp.ScrollPos() == vr);
}

TEST_CASE("ScrollViewport: PageUp moves by viewRows") {
    ScrollViewport vp;
    for (int i = 0; i < 30; ++i) vp.AddLine("x", kFg);
    const int vr = 8;
    vp.HandleKey(scanKey(SCAN_PAGE_DOWN), vr);
    vp.HandleKey(scanKey(SCAN_PAGE_DOWN), vr);
    vp.HandleKey(scanKey(SCAN_PAGE_UP), vr);
    CHECK(vp.ScrollPos() == vr);
}

TEST_CASE("ScrollViewport: PageUp at top clamps to 0") {
    ScrollViewport vp;
    for (int i = 0; i < 30; ++i) vp.AddLine("x", kFg);
    vp.HandleKey(scanKey(SCAN_PAGE_UP), 8);
    CHECK(vp.ScrollPos() == 0);
}

// ── Home / End ───────────────────────────────────────────────────────────────

TEST_CASE("ScrollViewport: Home jumps to ScrollPos==0") {
    ScrollViewport vp;
    for (int i = 0; i < 20; ++i) vp.AddLine("x", kFg);
    vp.HandleKey(scanKey(SCAN_END), 5);
    CHECK(vp.ScrollPos() > 0);
    vp.HandleKey(scanKey(SCAN_HOME), 5);
    CHECK(vp.ScrollPos() == 0);
}

TEST_CASE("ScrollViewport: End jumps to last full page") {
    ScrollViewport vp;
    for (int i = 0; i < 20; ++i) vp.AddLine("x", kFg);
    const int vr = 5;
    vp.HandleKey(scanKey(SCAN_END), vr);
    CHECK(vp.ScrollPos() == vp.TotalLines() - vr);
}

// ── ScrollPos invariant after any key ────────────────────────────────────────

TEST_CASE("ScrollViewport: ScrollPos always in [0, TotalLines-viewRows] after any key") {
    ScrollViewport vp;
    for (int i = 0; i < 15; ++i) vp.AddLine("x", kFg);
    const int vr = 5;
    int maxScroll = vp.TotalLines() - vr;

    UINT16 keys[] = {SCAN_UP, SCAN_DOWN, SCAN_PAGE_UP, SCAN_PAGE_DOWN,
                     SCAN_HOME, SCAN_END};
    for (int iter = 0; iter < 40; ++iter) {
        vp.HandleKey(scanKey(keys[iter % 6]), vr);
        CHECK(vp.ScrollPos() >= 0);
        CHECK(vp.ScrollPos() <= maxScroll);
    }
}

// ── ScrollToLine ─────────────────────────────────────────────────────────────

TEST_CASE("ScrollViewport::ScrollToLine: scrolls up when line is above visible window") {
    ScrollViewport vp;
    for (int i = 0; i < 20; ++i) vp.AddLine("x", kFg);
    const int vr = 5;
    // Navigate to bottom first.
    vp.HandleKey(scanKey(SCAN_END), vr);
    int bottom = vp.ScrollPos();
    CHECK(bottom > 0);
    // Scroll to line 0 (above visible).
    vp.ScrollToLine(0, vr);
    CHECK(vp.ScrollPos() == 0);
}

TEST_CASE("ScrollViewport::ScrollToLine: scrolls down when line is below visible window") {
    ScrollViewport vp;
    for (int i = 0; i < 20; ++i) vp.AddLine("x", kFg);
    const int vr = 5;
    // Start at top (scroll==0, visible [0,4]).
    vp.ScrollToLine(10, vr);
    // Line 10 should now be visible: scroll in [6, 10].
    CHECK(vp.ScrollPos() <= 10);
    CHECK(vp.ScrollPos() + vr > 10);
}

TEST_CASE("ScrollViewport::ScrollToLine: no change when line already visible") {
    ScrollViewport vp;
    for (int i = 0; i < 20; ++i) vp.AddLine("x", kFg);
    const int vr = 5;
    // Scroll to position 3 so visible window is [3, 7].
    for (int i = 0; i < 3; ++i) vp.HandleKey(scanKey(SCAN_DOWN), vr);
    CHECK(vp.ScrollPos() == 3);
    // Line 5 is visible → ScrollToLine should not change position.
    vp.ScrollToLine(5, vr);
    CHECK(vp.ScrollPos() == 3);
}

// ── Render: draw call content ─────────────────────────────────────────────────

TEST_CASE("ScrollViewport::Render: draws the visible lines in order") {
    FakeRenderer::Reset();
    FakeRenderer::SetSize(80, 25);

    ScrollViewport vp;
    vp.AddLine("line0", kFg);
    vp.AddLine("line1", kFg);
    vp.AddLine("line2", kFg);

    vp.Render(0, 3);

    // Should have exactly 3 draw calls (one per visible row).
    REQUIRE(FakeRenderer::CallCount() >= 3);
    const FakeRenderer::DrawCall* calls = FakeRenderer::Calls();
    CHECK(StrCmp(calls[0].text, "line0") == 0);
    CHECK(StrCmp(calls[1].text, "line1") == 0);
    CHECK(StrCmp(calls[2].text, "line2") == 0);
}

TEST_CASE("ScrollViewport::Render: respects scroll offset") {
    FakeRenderer::Reset();
    FakeRenderer::SetSize(80, 25);

    ScrollViewport vp;
    vp.AddLine("L0", kFg);
    vp.AddLine("L1", kFg);
    vp.AddLine("L2", kFg);
    vp.AddLine("L3", kFg);
    // Scroll to line 2 so visible window starts there.
    vp.HandleKey(scanKey(SCAN_DOWN), 2);
    vp.HandleKey(scanKey(SCAN_DOWN), 2);
    CHECK(vp.ScrollPos() == 2);

    vp.Render(0, 2);

    const FakeRenderer::DrawCall* calls = FakeRenderer::Calls();
    CHECK(StrCmp(calls[0].text, "L2") == 0);
    CHECK(StrCmp(calls[1].text, "L3") == 0);
}

TEST_CASE("ScrollViewport::Render: hasBg==false for lines added with AddLine(text,fg)") {
    FakeRenderer::Reset();
    FakeRenderer::SetSize(80, 25);

    ScrollViewport vp;
    vp.AddLine("plain", kFg);
    vp.Render(0, 1);

    REQUIRE(FakeRenderer::CallCount() >= 1);
    CHECK(FakeRenderer::Calls()[0].hasBg == false);
}

TEST_CASE("ScrollViewport::Render: hasBg==true for lines added with AddLine(text,fg,bg)") {
    FakeRenderer::Reset();
    FakeRenderer::SetSize(80, 25);

    ScrollViewport vp;
    vp.AddLine("highlighted", kFg, kBg);
    vp.Render(0, 1);

    REQUIRE(FakeRenderer::CallCount() >= 1);
    CHECK(FakeRenderer::Calls()[0].hasBg == true);
}

TEST_CASE("ScrollViewport::Render: clears trailing rows when content < viewRows") {
    FakeRenderer::Reset();
    FakeRenderer::SetSize(80, 25);

    ScrollViewport vp;
    vp.AddLine("only", kFg);
    vp.Render(0, 5);  // viewRows=5 but only 1 line

    // 5 draw calls total: 1 content + 4 cleared trailing rows.
    CHECK(FakeRenderer::CallCount() == 5);
    // The 4 trailing calls draw empty strings (DrawText, not DrawTextBg).
    for (UINT32 i = 1; i < 5; ++i) {
        const FakeRenderer::DrawCall& c = FakeRenderer::Calls()[i];
        CHECK(c.hasBg  == false);
        CHECK(c.text[0] == '\0');
    }
}
