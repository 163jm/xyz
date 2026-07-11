// Material 3 主题配色（种子色→ColorScheme）
// 移植自 Google Material Color Utilities 的简化实现（HCT 色彩空间近似）
#pragma once
#include <cstdint>
#include <d2d1.h>

namespace meplayer {

struct ColorScheme {
    // 主要角色色
    D2D1_COLOR_F primary            = {0,0,0,1};
    D2D1_COLOR_F onPrimary          = {0,0,0,1};
    D2D1_COLOR_F primaryContainer   = {0,0,0,1};
    D2D1_COLOR_F onPrimaryContainer = {0,0,0,1};
    D2D1_COLOR_F secondary          = {0,0,0,1};
    D2D1_COLOR_F onSecondary        = {0,0,0,1};
    D2D1_COLOR_F surface            = {0,0,0,1};
    D2D1_COLOR_F onSurface          = {0,0,0,1};
    D2D1_COLOR_F surfaceVariant     = {0,0,0,1};
    D2D1_COLOR_F onSurfaceVariant   = {0,0,0,1};
    D2D1_COLOR_F background         = {0,0,0,1};
    D2D1_COLOR_F onBackground       = {0,0,0,1};
    D2D1_COLOR_F error              = {0,0,0,1};
    D2D1_COLOR_F onError            = {0,0,0,1};
    D2D1_COLOR_F outline            = {0,0,0,1};
    D2D1_COLOR_F outlineVariant     = {0,0,0,1};
    // 是否深色
    bool dark = false;
};

class Theme {
public:
    // 由种子色 + 明暗生成完整 ColorScheme
    static ColorScheme fromSeed(uint32_t seedARGB, bool dark);

    // 预设种子色（与原项目一致：蓝/绿/红/紫/橙/青）
    static constexpr uint32_t kSeedBlue   = 0xff3498db;
    static constexpr uint32_t kSeedGreen  = 0xff2ecc71;
    static constexpr uint32_t kSeedRed    = 0xffe74c3c;
    static constexpr uint32_t kSeedPurple = 0xff9b59b6;
    static constexpr uint32_t kSeedOrange = 0xffe67e22;
    static constexpr uint32_t kSeedTeal   = 0xff1abc9c;

    // ARGB 转 D2D1_COLOR_F
    static D2D1_COLOR_F toColorF(uint32_t argb);

    // HSL ↔ RGB
    static void rgbToHsl(float r, float g, float b, float& h, float& s, float& l);
    static void hslToRgb(float h, float s, float l, float& r, float& g, float& b);

    // 由 HSL 生成 ARGB
    static uint32_t hslToArgb(float h, float s, float l);
};

}  // namespace meplayer
