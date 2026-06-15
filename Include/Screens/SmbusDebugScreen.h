#pragma once
#include "Screens/Screen.h"

// Static diagnostic dump of the SMBus controller discovery / register snapshot.
class SmbusDebugScreen : public Screen {
public:
    const char* Title()  const override { return "SMBus Debug Info"; }
    const char* Footer() const override { return "[Any key] Back"; }

    void      Draw(Tui& tui, int top, int bottom) override;
    NavResult HandleKey(Tui& tui, EFI_INPUT_KEY key) override;
};
