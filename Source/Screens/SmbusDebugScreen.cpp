#include "Screens/SmbusDebugScreen.h"
#include "Screens/UiHelpers.h"
#include "Tui.h"
#include "Renderer.h"
#include "ColorTheme.h"
#include "SystemInfo.h"
#include "Freestanding.h"

void SmbusDebugScreen::Draw(Tui& /*tui*/, int top, int /*bottom*/) {
    const SystemInfo::SmbusDebugInfo& d = SystemInfo::GetSmbusDebug();

    int row = top + 1;
    const Color dim  = Theme::Current().TextDim;
    const Color text = Theme::Current().Text;
    const Color hi   = Theme::Current().Accent;
    const Color warn = Theme::Current().Warning;

    auto Row = [&](const char* label, const char* val, Color c = Color{}) {
        if (c.R == 0 && c.G == 0 && c.B == 0) c = text;
        Renderer::DrawText(2,  row, label, dim);
        Renderer::DrawText(30, row, val,   c);
        ++row;
    };

    // Controller discovery
    Renderer::DrawText(2, row++, "  [PCI scan — bus 0, class 0C/05]", hi);
    if (d.Dev == 0xFF) {
        Row("  Controller found:", "NO — no device with class 0C/05 on bus 0", warn);
    } else {
        Row("  Dev / Fn:",
            Ui::Concat3(UintToStr(d.Dev), " / ", UintToStr(d.Fn)));
        Row("  VID:DID:",
            Ui::Concat3(HexToStr(d.Vid, 4), ":", HexToStr(d.Did, 4)));
        const char* vidName = (d.Vid == 0x8086) ? "  (Intel)" :
                              (d.Vid == 0x1022) ? "  (AMD)"   : "  (unknown vendor)";
        Row("  Vendor:", vidName);
    }

    ++row;
    Renderer::DrawText(2, row++, "  [Register snapshot (before enable writes)]", hi);
    Row("  Reg[20h] Intel SMBA:",
        Ui::Concat3(HexToStr(d.Reg20, 8), "  I/O BAR? ",
                    (d.Reg20 & 1) ? "YES" : "NO (MMIO or zero)"),
        (d.Reg20 & 1) ? text : warn);
    Row("  Reg[40h] Intel HOSTC:",
        Ui::Concat3(HexToStr(d.Reg40, 8), "  HST_EN? ",
                    (d.Reg40 & 1) ? "YES" : "NO (controller disabled!)"),
        (d.Reg40 & 1) ? text : warn);
    Row("  Reg[90h] AMD SMBBASE:",
        Ui::Concat3(HexToStr(d.Reg90, 8), "  base=",
                    HexToStr(d.Reg90 & 0xFFF0, 4)));
    {
        UINT16 pmioBase = d.PmioSmba & 0xFFFE;
        Row("  PMIO[2Ch] AMD SMBA:",
            Ui::Concat3(HexToStr(d.PmioSmba, 4), "  base=",
                        HexToStr(pmioBase, 4)),
            pmioBase > 0x0F ? hi : warn);
    }

    ++row;
    Renderer::DrawText(2, row++, "  [I/O base selection]", hi);
    if (d.IoBase == 0) {
        Row("  I/O base used:", "0000  — NOT FOUND (SMBus locked result)", warn);
    } else {
        Row("  I/O base used:", Ui::Concat2(HexToStr(d.IoBase, 4), "h"), hi);
    }

    ++row;
    Renderer::DrawText(2, row++, "  [Controller state at DetectSpd entry]", hi);
    if (d.IoBase == 0) {
        Row("  HSTSTS (initial):", "N/A — no base found");
    } else {
        Row("  HSTSTS (initial):",
            Ui::Concat3(HexToStr(d.InitHststs, 2), "h  HOST_BUSY? ",
                        (d.InitHststs & 1) ? "YES (was stuck)" : "no"),
            (d.InitHststs & 1) ? warn : text);
    }

    ++row;
    Renderer::DrawText(2, row++, "  [DIMM probe: SMBus addr 0x50, slot 0]", hi);
    if (d.IoBase == 0) {
        Row("  Byte 0 (SPD size):", "N/A");
        Row("  Byte 2 (dev type):", "N/A");
    } else {
        const char* b0note = (d.Slot50B0 == 0xFF) ? "  0xFF = no response / read fail" :
                             (d.Slot50B0 == 0x00) ? "  0x00 = no DIMM" : "  valid";
        Row("  Byte 0 (SPD size):",
            Ui::Concat2(HexToStr(d.Slot50B0, 2), b0note),
            (d.Slot50B0 == 0xFF || d.Slot50B0 == 0x00) ? warn : text);

        const char* dtnote = (d.Slot50Dt == 0x0C) ? "  DDR4" :
                             (d.Slot50Dt == 0x12) ? "  DDR5" :
                             (d.Slot50Dt == 0x0B) ? "  DDR3" :
                             (d.Slot50Dt == 0xFF) ? "  0xFF = read fail" : "  unknown";
        Row("  Byte 2 (dev type):",
            Ui::Concat2(HexToStr(d.Slot50Dt, 2), dtnote),
            (d.Slot50Dt == 0xFF) ? warn : text);
    }
}

NavResult SmbusDebugScreen::HandleKey(Tui& /*tui*/, EFI_INPUT_KEY /*key*/) {
    return NavBack();  // any key returns
}
