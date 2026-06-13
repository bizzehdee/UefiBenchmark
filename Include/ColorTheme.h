#pragma once
// Colour palette for the TUI. All colours are 32-bit ARGB (0xAARRGGBB).
// The alpha channel is ignored by GOP but kept for clarity.

#include "UefiTypes.h"

struct Color {
    UINT8 B, G, R, Reserved;

    constexpr Color() : B(0), G(0), R(0), Reserved(0) {}
    constexpr Color(UINT8 r, UINT8 g, UINT8 b)
        : B(b), G(g), R(r), Reserved(0) {}

    constexpr bool operator==(const Color& o) const {
        return R == o.R && G == o.G && B == o.B;
    }
    constexpr bool operator!=(const Color& o) const { return !(*this == o); }

    UINT32 Pack() const {
        return (static_cast<UINT32>(R) << 16) |
               (static_cast<UINT32>(G) << 8)  |
               static_cast<UINT32>(B);
    }
};

namespace Theme {
    constexpr Color Background   {  10,  10,  28 };
    constexpr Color HeaderBorder {   0, 180, 216 };
    constexpr Color HeaderText   { 255, 255, 255 };
    constexpr Color Text         { 210, 210, 210 };
    constexpr Color TextDim      { 120, 120, 130 };
    constexpr Color Highlight    {   0,  70,  90 };
    constexpr Color HighlightTxt { 255, 255, 255 };
    constexpr Color Accent       {   0, 200, 220 };
    constexpr Color Success      {  80, 200,  80 };
    constexpr Color Warning      { 220, 180,   0 };
    constexpr Color Error        { 220,  60,  60 };
    constexpr Color Separator    {  50,  50,  70 };
    constexpr Color CheckMark    {   0, 220, 160 };
    constexpr Color Footer       { 180, 160,  60 };
}
