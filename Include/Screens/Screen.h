#pragma once
// Base interface for an individual TUI screen.
//
// Tui is the controller: it owns the header, footer, navigation stack, and the
// shared model. Each Screen owns only its body (the region between header and
// footer) plus its own input handling, and returns a NavResult telling the
// controller where to go next. Screens never call each other directly.

#include "UefiTypes.h"

class Tui;  // controller (full definition in Tui.h)

// Identifies every screen the controller can navigate to.
enum class ScreenId {
    MainMenu,
    BenchmarkSelection,
    RunCountPicker,
    Results,
    CategoryResults,
    SystemInfo,
    AiSuitability,
    SmbusDebug,
    ResolutionPicker,
    ThemePicker,
    CorePicker,
};

// What the controller should do after a key is handled.
enum class NavOp { Stay, Back, Push, Replace, Exit };

struct NavResult {
    NavOp    op     = NavOp::Stay;
    ScreenId target = ScreenId::MainMenu;  // valid for Push/Replace
};

// Small constructors for readable returns from HandleKey().
inline NavResult NavStay()              { return { NavOp::Stay,    ScreenId::MainMenu }; }
inline NavResult NavBack()              { return { NavOp::Back,    ScreenId::MainMenu }; }
inline NavResult NavExit()              { return { NavOp::Exit,    ScreenId::MainMenu }; }
inline NavResult NavPush(ScreenId s)    { return { NavOp::Push,    s }; }
inline NavResult NavReplace(ScreenId s) { return { NavOp::Replace, s }; }

class Screen {
public:
    virtual ~Screen() {}

    // Header title and footer hint — the controller draws the chrome.
    virtual const char* Title()  const = 0;
    virtual const char* Footer() const = 0;

    // Called once each time the screen becomes active (fresh push or returned-to
    // via Back). Use to reset transient cursor/scroll state and to build any
    // content that does not change while the screen is open.
    virtual void OnEnter(Tui& /*tui*/) {}

    // Draw the body. Rows [top, bottom) are available; the header occupies the
    // rows above `top` and the footer the rows from `bottom` down.
    virtual void Draw(Tui& tui, int top, int bottom) = 0;

    // Handle a single key. Return NavStay() to remain (e.g. cursor moved), or a
    // navigation action to change screens.
    virtual NavResult HandleKey(Tui& tui, EFI_INPUT_KEY key) = 0;
};
