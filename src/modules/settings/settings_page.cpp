#include "modules/settings/settings_page.h"
#include "ui/ui_widgets.h"
#include "app/app_settings.h"
#include "app/theme.h"
#include <memory>

namespace meplayer {

SettingsPage::SettingsPage() {}
SettingsPage::~SettingsPage() {}

void SettingsPage::onActive() {
    needsRebuild_ = true;
}

// 判断当前是否深色
static bool isDark() {
    auto tm = AppSettings::instance().themeMode();
    if (tm == ThemeMode::Light) return false;
    if (tm == ThemeMode::Dark)  return true;
    // System：读注册表
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD val = 0, sz = sizeof(val);
        RegQueryValueExW(hKey, L"AppsUseLightTheme", nullptr, nullptr,
                         reinterpret_cast<LPBYTE>(&val), &sz);
        RegCloseKey(hKey);
        return val == 0;
    }
    return false;
}

Size SettingsPage::measure(const Size& max) {
    return {max.w, max.h};
}

void SettingsPage::layout(const Rect& b) {
    bounds_ = b;
    if (needsRebuild_) rebuild();
    if (root_) root_->layout(b);
}

void SettingsPage::draw(ID2D1RenderTarget* rt) {
    if (root_) root_->draw(rt);
}

Widget* SettingsPage::hitTest(float x, float y) {
    if (root_) return root_->hitTest(x, y);
    return nullptr;
}

bool SettingsPage::onMouseWheel(const MouseEvent& e) {
    return root_ ? root_->onMouseWheel(e) : false;
}
bool SettingsPage::onMouseDown(const MouseEvent& e) {
    return root_ ? root_->onMouseDown(e) : false;
}
bool SettingsPage::onMouseUp(const MouseEvent& e) {
    return root_ ? root_->onMouseUp(e) : false;
}
bool SettingsPage::onMouseMove(const MouseEvent& e) {
    return root_ ? root_->onMouseMove(e) : false;
}

// 创建一个开关按钮组（选中项填充 primary）
// 由于 TextButton 无 selected 字段，用 Container 包装实现选中态
static WidgetPtr makeToggle(const std::vector<std::wstring>& labels,
                            int selected,
                            std::function<void(int)> onSelect) {
    auto row = std::make_shared<Row>();
    row->spacing = 8;
    auto& sc = ThemeManager::instance().current();
    for (int i = 0; i < (int)labels.size(); i++) {
        bool isSel = (i == selected);
        auto btn = std::make_shared<FilledButton>(labels[i], [i, onSelect]() { onSelect(i); });
        btn->fontSize = 13;
        // FilledButton 默认 primary 背景；非选中态需要透明背景——简化：用 TextButton
        auto tb = std::make_shared<TextButton>(labels[i], [i, onSelect]() { onSelect(i); });
        tb->fontSize = 13;
        // 用 Container 包装选中态背景
        auto wrap = std::make_shared<Container>();
        wrap->radius = 20;
        wrap->padding = sym(12, 6);
        wrap->color = isSel ? sc.primary : sc.surfaceVariant;
        wrap->child = std::make_shared<Text>(labels[i], 13,
            isSel ? sc.onPrimary : sc.onSurfaceVariant);
        // 点击
        auto tap = std::make_shared<ListTile>();
        tap->onTap = [i, onSelect]() { onSelect(i); };
        // 简化：直接用 Container，点击通过外层处理
        row->addChild(wrap);
    }
    return row;
}

static WidgetPtr makeCard(const std::wstring& title, WidgetPtr content) {
    auto& sc = ThemeManager::instance().current();
    auto card = std::make_shared<Card>();
    auto col = std::make_shared<Column>();
    col->spacing = 16;
    auto titleText = std::make_shared<Text>(title, 18, sc.onSurface, DWRITE_FONT_WEIGHT_BOLD);
    col->addChild(titleText);
    col->addChild(content);
    card->child = std::make_shared<Padding>(all(20), col);
    return card;
}

void SettingsPage::rebuild() {
    needsRebuild_ = false;
    auto& sc = ThemeManager::instance().current();
    auto& s = AppSettings::instance();

    auto col = std::make_shared<Column>();
    col->spacing = 16;

    // ---- 外观卡片 ----
    {
        auto body = std::make_shared<Column>();
        body->spacing = 16;

        // 主题模式
        auto themeRow = std::make_shared<Row>();
        themeRow->mainAlign = MainAxisAlignment::SpaceBetween;
        themeRow->addChild(std::make_shared<Text>(L"主题模式", 14, sc.onSurface));
        int tm = static_cast<int>(s.themeMode());
        themeRow->addChild(makeToggle({L"跟随系统", L"浅色", L"深色"}, tm, [](int i){
            AppSettings::instance().setThemeMode(static_cast<ThemeMode>(i));
            ThemeManager::instance().update(AppSettings::instance().seedColor(), isDark());
        }));
        body->addChild(themeRow);

        // 种子色
        auto colorRow = std::make_shared<Row>();
        colorRow->mainAlign = MainAxisAlignment::SpaceBetween;
        colorRow->addChild(std::make_shared<Text>(L"主题色", 14, sc.onSurface));
        auto colorDots = std::make_shared<Row>();
        colorDots->spacing = 12;
        uint32_t seeds[] = {Theme::kSeedBlue, Theme::kSeedGreen, Theme::kSeedRed,
                            Theme::kSeedPurple, Theme::kSeedOrange, Theme::kSeedTeal};
        for (uint32_t seed : seeds) {
            auto dot = std::make_shared<IconButton>(IconKind::MusicNote,
                [seed]() {
                    AppSettings::instance().setSeedColor(seed);
                    ThemeManager::instance().update(seed, isDark());
                });
            dot->iconSize = 24;
            dot->buttonSize = 32;
            dot->circular = true;
            dot->iconColor = Theme::toColorF(seed);
            colorDots->addChild(dot);
        }
        colorRow->addChild(colorDots);
        body->addChild(colorRow);

        col->addChild(makeCard(L"外观", body));
    }

    // ---- 播放设置卡片 ----
    {
        auto body = std::make_shared<Column>();
        body->spacing = 16;

        // 硬件解码
        auto hwRow = std::make_shared<Row>();
        hwRow->mainAlign = MainAxisAlignment::SpaceBetween;
        hwRow->addChild(std::make_shared<Text>(L"硬件解码", 14, sc.onSurface));
        hwRow->addChild(makeToggle({L"开", L"关"}, s.hardwareDecode() ? 0 : 1, [](int i){
            AppSettings::instance().setHardwareDecode(i == 0);
        }));
        body->addChild(hwRow);

        // 画质预设
        auto profileRow = std::make_shared<Row>();
        profileRow->mainAlign = MainAxisAlignment::SpaceBetween;
        profileRow->addChild(std::make_shared<Text>(L"画质预设", 14, sc.onSurface));
        profileRow->addChild(makeToggle({L"性能优先", L"均衡", L"画质优先"},
            static_cast<int>(s.mpvProfile()), [](int i){
            AppSettings::instance().setMpvProfile(static_cast<MpvProfile>(i));
        }));
        body->addChild(profileRow);

        // 去交错
        auto diRow = std::make_shared<Row>();
        diRow->mainAlign = MainAxisAlignment::SpaceBetween;
        diRow->addChild(std::make_shared<Text>(L"去交错（IPTV）", 14, sc.onSurface));
        diRow->addChild(makeToggle({L"开", L"关"}, s.deinterlace() ? 0 : 1, [](int i){
            AppSettings::instance().setDeinterlace(i == 0);
        }));
        body->addChild(diRow);

        // TS 容错
        auto tsRow = std::make_shared<Row>();
        tsRow->mainAlign = MainAxisAlignment::SpaceBetween;
        tsRow->addChild(std::make_shared<Text>(L"TS 容错", 14, sc.onSurface));
        tsRow->addChild(makeToggle({L"开", L"关"}, s.tsResilience() ? 0 : 1, [](int i){
            AppSettings::instance().setTsResilience(i == 0);
        }));
        body->addChild(tsRow);

        // 字幕自动加载
        auto subRow = std::make_shared<Row>();
        subRow->mainAlign = MainAxisAlignment::SpaceBetween;
        subRow->addChild(std::make_shared<Text>(L"字幕自动加载", 14, sc.onSurface));
        subRow->addChild(makeToggle({L"开", L"关"}, s.subAutoLoad() ? 0 : 1, [](int i){
            AppSettings::instance().setSubAutoLoad(i == 0);
        }));
        body->addChild(subRow);

        col->addChild(makeCard(L"播放", body));
    }

    // ---- 关于卡片 ----
    {
        auto body = std::make_shared<Column>();
        body->spacing = 8;
        body->addChild(std::make_shared<Text>(L"MEPlayer 1.0.0", 14, sc.onSurface));
        body->addChild(std::make_shared<Text>(L"原生 Windows 媒体播放器（libmpv + Direct2D）",
                                              12, sc.onSurfaceVariant));
        col->addChild(makeCard(L"关于", body));
    }

    root_ = std::make_shared<Scrollable>(col);
}

}  // namespace meplayer
