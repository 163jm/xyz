#include "app/theme.h"
#include <algorithm>
#include <cmath>

namespace meplayer {

D2D1_COLOR_F Theme::toColorF(uint32_t argb) {
    return D2D1::ColorF(
        ((argb >> 16) & 0xff) / 255.0f,
        ((argb >> 8) & 0xff) / 255.0f,
        (argb & 0xff) / 255.0f,
        ((argb >> 24) & 0xff) / 255.0f);
}

void Theme::rgbToHsl(float r, float g, float b, float& h, float& s, float& l) {
    float maxc = std::max({r, g, b});
    float minc = std::min({r, g, b});
    l = (maxc + minc) / 2.0f;
    if (maxc == minc) { h = s = 0; return; }
    float d = maxc - minc;
    s = l > 0.5f ? d / (2.0f - maxc - minc) : d / (maxc + minc);
    if (maxc == r)      h = (g - b) / d + (g < b ? 6.0f : 0.0f);
    else if (maxc == g) h = (b - r) / d + 2.0f;
    else                h = (r - g) / d + 4.0f;
    h /= 6.0f;
}

static float hueToRgb(float p, float q, float t) {
    if (t < 0) t += 1;
    if (t > 1) t -= 1;
    if (t < 1.0f/6.0f) return p + (q - p) * 6.0f * t;
    if (t < 1.0f/2.0f) return q;
    if (t < 2.0f/3.0f) return p + (q - p) * (2.0f/3.0f - t) * 6.0f;
    return p;
}

void Theme::hslToRgb(float h, float s, float l, float& r, float& g, float& b) {
    if (s == 0) { r = g = b = l; return; }
    float q = l < 0.5f ? l * (1.0f + s) : l + s - l * s;
    float p = 2.0f * l - q;
    r = hueToRgb(p, q, h + 1.0f/3.0f);
    g = hueToRgb(p, q, h);
    b = hueToRgb(p, q, h - 1.0f/3.0f);
}

uint32_t Theme::hslToArgb(float h, float s, float l) {
    float r, g, b;
    hslToRgb(h, s, l, r, g, b);
    uint8_t R = static_cast<uint8_t>(std::round(r * 255));
    uint8_t G = static_cast<uint8_t>(std::round(g * 255));
    uint8_t B = static_cast<uint8_t>(std::round(b * 255));
    return 0xff000000u | (R << 16) | (G << 8) | B;
}

ColorScheme Theme::fromSeed(uint32_t seedARGB, bool dark) {
    ColorScheme c;
    c.dark = dark;

    float r = ((seedARGB >> 16) & 0xff) / 255.0f;
    float g = ((seedARGB >> 8) & 0xff) / 255.0f;
    float b = (seedARGB & 0xff) / 255.0f;
    float h, s, l;
    rgbToHsl(r, g, b, h, s, l);

    // 色相保持，按明暗档位生成 tonal palette（近似 Material 3 tone 0-100）
    auto tone = [&](float lightness) -> D2D1_COLOR_F {
        uint32_t argb = hslToArgb(h, std::min(s, 0.7f), lightness);
        return toColorF(argb);
    };

    if (dark) {
        // 深色主题：主色调亮、容器色暗、背景暗
        c.primary            = tone(0.80f);
        c.onPrimary          = tone(0.20f);
        c.primaryContainer   = tone(0.30f);
        c.onPrimaryContainer = tone(0.90f);
        c.secondary          = tone(0.70f);
        c.onSecondary        = tone(0.20f);
        c.surface            = tone(0.10f);
        c.onSurface          = tone(0.90f);
        c.surfaceVariant     = tone(0.18f);
        c.onSurfaceVariant   = tone(0.80f);
        c.background         = tone(0.06f);
        c.onBackground       = tone(0.90f);
        c.error              = toColorF(0xffcf6679);
        c.onError            = toColorF(0xff690005);
        c.outline            = tone(0.55f);
        c.outlineVariant     = tone(0.28f);
    } else {
        // 浅色主题
        c.primary            = tone(0.40f);
        c.onPrimary          = tone(0.98f);
        c.primaryContainer   = tone(0.85f);
        c.onPrimaryContainer = tone(0.12f);
        c.secondary          = tone(0.45f);
        c.onSecondary        = tone(0.98f);
        c.surface            = tone(0.98f);
        c.onSurface          = tone(0.10f);
        c.surfaceVariant     = tone(0.88f);
        c.onSurfaceVariant   = tone(0.25f);
        c.background         = tone(0.96f);
        c.onBackground       = tone(0.10f);
        c.error              = toColorF(0xffb3261e);
        c.onError            = toColorF(0xffffffff);
        c.outline            = tone(0.45f);
        c.outlineVariant     = tone(0.80f);
    }
    return c;
}

}  // namespace meplayer
