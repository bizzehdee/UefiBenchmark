#include "Screens/SystemInfoScreen.h"
#include "Screens/UiHelpers.h"
#include "Tui.h"
#include "Renderer.h"
#include "ColorTheme.h"
#include "SystemInfo.h"
#include "BenchmarkRegistry.h"
#include "IBenchmark.h"
#include "Timer.h"
#include "VideoEngine.h"
#include "Freestanding.h"

namespace {
// Build a "  LABEL(padded to 24 chars)VALUE" line and append to a viewport.
void VpAddInfo(ScrollViewport& vp, const char* label, const char* value) {
    char buf[ScrollViewport::MAX_WIDTH];
    int p = 0;
    buf[p++] = ' '; buf[p++] = ' ';
    int llen = 0;
    while (label[llen] && p < ScrollViewport::MAX_WIDTH - 1)
        buf[p++] = label[llen++];
    while (llen < 24 && p < ScrollViewport::MAX_WIDTH - 1)
        { buf[p++] = ' '; ++llen; }
    while (*value && p < ScrollViewport::MAX_WIDTH - 1)
        buf[p++] = *value++;
    buf[p] = '\0';
    vp.AddLine(buf, Theme::Current().Accent);
}
}  // namespace

void SystemInfoScreen::OnEnter(Tui& /*tui*/) {
    ScrollViewport& vp = mVp;
    vp.Clear();

    auto FormatCache = [](UINT32 kb) -> const char* {
        if (kb >= 1024) return Ui::Concat2(UintToStr(kb / 1024), " MB");
        return Ui::Concat2(UintToStr(kb), " KB");
    };

    // ── CPU section ───────────────────────────────────────────────
    vp.AddLine("  [CPU]", Theme::Current().TextDim);
    VpAddInfo(vp, "Vendor:",       SystemInfo::GetCpuVendor());
    VpAddInfo(vp, "Brand:",        SystemInfo::GetCpuBrand());
    VpAddInfo(vp, "Stepping:",     UintToStr(SystemInfo::GetCpuStepping()));
    VpAddInfo(vp, "Logical CPUs:", UintToStr(SystemInfo::GetCpuCoreCount()));
    VpAddInfo(vp, "MP Services:",  SystemInfo::HasMpServices() ? "Available" : "Not available");
    if (SystemInfo::HasMpServices()) {
        UINT32 enabled = SystemInfo::GetEnabledProcessorCount();
        char apBuf[32]; int ap = 0;
        const char* ns = UintToStr(enabled > 1 ? enabled - 1 : 0);
        while (ns[ap]) { apBuf[ap] = ns[ap]; ++ap; }
        for (const char* s = " APs + BSP"; *s; ++s) apBuf[ap++] = *s;
        apBuf[ap] = '\0';
        VpAddInfo(vp, "Processors:", apBuf);
    }

    // ── Cache section ─────────────────────────────────────────────
    vp.AddLine();
    vp.AddLine("  [Cache]", Theme::Current().TextDim);
    if (SystemInfo::GetL1DataCacheKB() > 0)
        VpAddInfo(vp, "L1D Cache:", FormatCache(SystemInfo::GetL1DataCacheKB()));
    if (SystemInfo::GetL1InstCacheKB() > 0)
        VpAddInfo(vp, "L1I Cache:", FormatCache(SystemInfo::GetL1InstCacheKB()));
    if (SystemInfo::GetL2CacheKB() > 0)
        VpAddInfo(vp, "L2 Cache:",  FormatCache(SystemInfo::GetL2CacheKB()));
    VpAddInfo(vp, "L3 Cache:", SystemInfo::GetL3CacheKB() > 0
                                ? FormatCache(SystemInfo::GetL3CacheKB()) : "None");

    // ── Memory section ────────────────────────────────────────────
    vp.AddLine();
    vp.AddLine("  [Memory]", Theme::Current().TextDim);
    VpAddInfo(vp, "Available:",   Ui::Concat2(UintToStr(SystemInfo::GetTotalMemoryMB()), " MB"));
    VpAddInfo(vp, "Type:",        SystemInfo::GetMemoryType());
    if (SystemInfo::GetMemorySpeedMHz() > 0)
        VpAddInfo(vp, "Speed:",   Ui::Concat2(UintToStr(SystemInfo::GetMemorySpeedMHz()), " MHz"));
    if (SystemInfo::GetMemoryConfiguredSpeedMHz() > 0 &&
        SystemInfo::GetMemoryConfiguredSpeedMHz() != SystemInfo::GetMemorySpeedMHz())
        VpAddInfo(vp, "Configured:", Ui::Concat2(UintToStr(SystemInfo::GetMemoryConfiguredSpeedMHz()), " MHz"));
    if (SystemInfo::GetMemoryChannelCount() > 0)
        VpAddInfo(vp, "Channels:", UintToStr(SystemInfo::GetMemoryChannelCount()));
    if (SystemInfo::GetMemoryVoltageMv() > 0)
        VpAddInfo(vp, "Voltage:", Ui::Concat2(UintToStr(SystemInfo::GetMemoryVoltageMv()), " mV"));
    else
        VpAddInfo(vp, "Voltage:", "N/A (SMBIOS 2.8+ required)");
    if (SystemInfo::GetSpdTCL() > 0) {
        char tStr[32]; int tp = 0;
        auto AppNum = [&](UINT32 n) {
            const char* s = UintToStr((UINT64)n);
            for (int i = 0; s[i] && tp < 31; ++i) tStr[tp++] = s[i];
        };
        AppNum(SystemInfo::GetSpdTCL());  tStr[tp++] = '-';
        AppNum(SystemInfo::GetSpdTRCD()); tStr[tp++] = '-';
        AppNum(SystemInfo::GetSpdTRP());  tStr[tp++] = '-';
        AppNum(SystemInfo::GetSpdTRAS()); tStr[tp] = '\0';
        VpAddInfo(vp, "Timings (SPD):", tStr);
    } else if (SystemInfo::IsSpdDdr5()) {
        VpAddInfo(vp, "Timings (SPD):", "DDR5 (parsed separately)");
    } else {
        const char* mt = SystemInfo::GetMemoryType();
        if (StrCmp(mt, "DDR4") == 0 || StrCmp(mt, "DDR5") == 0 ||
            StrCmp(mt, "LPDDR") == 0 || StrCmp(mt, "LPDDR3") == 0 ||
            StrCmp(mt, "LPDDR4") == 0 || StrCmp(mt, "LPDDR4X") == 0)
            VpAddInfo(vp, "Timings:", "N/A (SMBus locked)");
        else
            VpAddInfo(vp, "Timings:", "N/A (DDR3/older or SMBus locked)");
    }

    // ── Display & system section ──────────────────────────────────
    vp.AddLine();
    vp.AddLine("  [Display & System]", Theme::Current().TextDim);
    {
        RenderMode rm = VideoEngine::GetRenderMode();
        const char* rmStr = (rm == RenderMode::GopBlt) ? "GOP Blt"          :
                            (rm == RenderMode::Avx2)   ? "AVX2 non-temporal" :
                                                         "Memcpy";
        VpAddInfo(vp, "Render Mode:", rmStr);
    }
    VpAddInfo(vp, "Display:",   Ui::Concat3(UintToStr(Renderer::ScreenWidth()),  "x",
                                            UintToStr(Renderer::ScreenHeight())));
    VpAddInfo(vp, "Text Grid:", Ui::Concat3(UintToStr(Renderer::Columns()), "x",
                                            UintToStr(Renderer::Rows())));
    VpAddInfo(vp, "Timer Calibrated:", Timer::IsCalibrated() ? "Yes" : "No");
    if (Timer::IsCalibrated())
        VpAddInfo(vp, "Cycles/us:", UintToStr(Timer::CyclesPerUs()));
    VpAddInfo(vp, "Invariant TSC:",   Timer::HasInvariantTSC() ? "Yes" : "No");
    VpAddInfo(vp, "Registered Tests:", UintToStr(BenchmarkRegistry::Count()));

    // ── Benchmark list section ────────────────────────────────────
    vp.AddLine();
    vp.AddSeparator();
    vp.AddLine("  Benchmarks:", Theme::Current().TextDim);

    IBenchmark** all  = BenchmarkRegistry::GetAll();
    UINTN        bmCount = BenchmarkRegistry::Count();
    char bmLine[ScrollViewport::MAX_WIDTH];
    for (UINTN i = 0; i < bmCount; ++i) {
        int p = 0;
        bmLine[p++] = ' '; bmLine[p++] = ' '; bmLine[p++] = ' '; bmLine[p++] = '-'; bmLine[p++] = ' ';
        const char* nm = all[i]->GetName();
        for (int j = 0; nm[j] && p < 50; ++j) bmLine[p++] = nm[j];
        while (p < 52) bmLine[p++] = ' ';
        bmLine[p++] = '[';
        const char* cat = all[i]->GetCategory();
        for (int j = 0; cat[j] && p < 65; ++j) bmLine[p++] = cat[j];
        bmLine[p++] = ']';
        while (p < 70) bmLine[p++] = ' ';
        ThreadingMode tm = all[i]->GetThreadingMode();
        const char* tmStr = (tm == ThreadingMode::SingleOnly) ? "Single" :
                            (tm == ThreadingMode::MultiOnly)  ? "Multi"  : "Either";
        for (int j = 0; tmStr[j] && p < 80; ++j) bmLine[p++] = tmStr[j];
        bmLine[p] = '\0';
        vp.AddLine(bmLine, Theme::Current().Text);
    }
}

void SystemInfoScreen::Draw(Tui& /*tui*/, int top, int /*bottom*/) {
    int renderStart = top + 1;   // header(3) + blank(1) in the original layout
    int footerRows  = 1;         // original used footerRows=1 for the view height
    mViewRows = static_cast<int>(Renderer::Rows()) - renderStart - footerRows;
    if (mViewRows < 1) mViewRows = 1;
    mVp.Render(renderStart, mViewRows);
}

NavResult SystemInfoScreen::HandleKey(Tui& /*tui*/, EFI_INPUT_KEY key) {
    if (key.ScanCode == SCAN_ESC) return NavBack();
    if (key.UnicodeChar == 'a' || key.UnicodeChar == 'A')
        return NavPush(ScreenId::AiSuitability);
    if (key.UnicodeChar == 'd' || key.UnicodeChar == 'D')
        return NavPush(ScreenId::SmbusDebug);
    mVp.HandleKey(key, mViewRows);
    return NavStay();
}
