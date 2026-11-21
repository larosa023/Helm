// Helm - Themes page (the same themes and theme files as Hangar)
#include "ui_internal.h"
#include "themes.h"
#include "config.h"
#include "util.h"
#include <shellapi.h>
#include <commdlg.h>
#include <algorithm>

static std::vector<ThemeDef> g_customThemes;
static ID2D1Layer* g_previewLayer = nullptr;

void ThemesInit() { g_customThemes = LoadCustomThemes(); }
void ThemesDiscardDevice() { SafeRelease(g_previewLayer); }
const std::vector<ThemeDef>& CustomThemes() { return g_customThemes; }

void ApplyTheme() {
    g_t = MakeTheme(FindTheme(g_cfg.theme, g_customThemes), g_cfg.accent);
    SafeRelease(g_previewLayer);   // belongs to the render target, which may be rebuilt
    ThemeChanged();
}

static std::vector<ThemeDef> AllThemes() {
    std::vector<ThemeDef> all = BuiltinThemes();
    all.insert(all.end(), g_customThemes.begin(), g_customThemes.end());
    return all;
}

static std::wstring HexColor(uint32_t c) {
    wchar_t b[16];
    swprintf(b, 16, L"#%06X", c & 0xFFFFFF);
    return b;
}

static bool PickCustomAccent() {
    static COLORREF custom[16] = {};
    uint32_t cur = ResolveAccent(FindTheme(g_cfg.theme, g_customThemes), g_cfg.accent);
    CHOOSECOLORW cc{sizeof(cc)};
    cc.hwndOwner = g_hwnd;
    cc.rgbResult = RGB((cur >> 16) & 0xFF, (cur >> 8) & 0xFF, cur & 0xFF);
    cc.lpCustColors = custom;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (!ChooseColorW(&cc)) return false;
    g_cfg.accent = HexColor((GetRValue(cc.rgbResult) << 16) | (GetGValue(cc.rgbResult) << 8) | GetBValue(cc.rgbResult));
    return true;
}

static void ExportTheme() {
    ThemeDef cur = FindTheme(g_cfg.theme, g_customThemes);
    if (cur.followWindows) cur = BuiltinThemes()[SystemUsesLightTheme() ? 1 : 0];
    std::wstring base = cur.custom ? cur.name : cur.name + L" (copy)";
    for (auto& ch : base) if (wcschr(L"\\/:*?\"<>|", ch)) ch = L'-';
    EnsureDir(ThemesDir());
    std::wstring path = ThemesDir() + L"\\" + base + L".json";
    for (int n = 2; FileExists(path) && !cur.custom; ++n) path = ThemesDir() + L"\\" + base + L" " + std::to_wstring(n) + L".json";
    if (!WriteTextFileAtomic(path, ThemeToJson(cur, g_cfg.accent))) { Toast(L"Couldn't save the theme file.", true); return; }
    g_customThemes = LoadCustomThemes();
    Toast(L"Saved " + PathName(path) + L". Edit it, then press Reload.");
    ShowInFolder(path);
}

// A miniature Helm window drawn in the theme's colors
static void DrawThemePreview(const ThemeDef& def0, const D2D1_RECT_F& r, int index, bool selected) {
    ThemeDef def = def0;
    if (def.followWindows) def = BuiltinThemes()[SystemUsesLightTheme() ? 1 : 0];
    std::wstring accentSetting = selected ? g_cfg.accent : L"theme";
    Theme pt = MakeTheme(def, accentSetting);
    bool hot = IsHover(Hit::ThemePick, -1, index);
    float hv = Anim(AKey(6000, index), hot ? 1.0f : 0.0f, 0.3f);
    g_c.FillRound(r, 10, Mix(g_t.surface, g_t.surfaceHover, hv));
    D2D1_RECT_F pv = D2D1::RectF(r.left + 10, r.top + 10, r.right - 10, r.bottom - 50);
    ID2D1RoundedRectangleGeometry* clip = nullptr;
    d2d->CreateRoundedRectangleGeometry(D2D1::RoundedRect(pv, 7, 7), &clip);
    if (!g_previewLayer) g_rt->CreateLayer(&g_previewLayer);
    if (clip && g_previewLayer) g_rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), clip), g_previewLayer);
    if (def.backdrop == L"acrylic" || def.surfaceOpacity < 1.0f) {
        ID2D1GradientStopCollection* stops = nullptr;
        D2D1_GRADIENT_STOP gs[3] = {{0, Rgb(0x3A5BD9)}, {0.55f, Rgb(0x8E4FD6)}, {1, Rgb(0xE0679A)}};
        g_rt->CreateGradientStopCollection(gs, 3, &stops);
        ID2D1LinearGradientBrush* lg = nullptr;
        if (stops) g_rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(pv.left, pv.top), D2D1::Point2F(pv.right, pv.bottom)), stops, &lg);
        if (lg) { g_rt->FillRectangle(pv, lg); lg->Release(); }
        if (stops) stops->Release();
        g_c.Fill(pv, Rgb(def.bg, 1.0f - def.transparency));
    } else {
        g_c.Fill(pv, Rgb(def.bg));
    }
    float w = pv.right - pv.left, h = pv.bottom - pv.top;
    D2D1_RECT_F sb = D2D1::RectF(pv.left, pv.top, pv.left + w * 0.27f, pv.bottom);
    g_c.Fill(sb, def.backdrop == L"acrylic" ? Rgb(def.sidebar, 0.35f) : Rgb(def.sidebar));
    g_c.FillRound(D2D1::RectF(sb.left + 6, pv.top + 7, sb.left + 14, pv.top + 15), 2, pt.accent);
    for (int k = 0; k < 4; ++k) {
        float ly = pv.top + 24 + k * 11;
        if (k == 0) g_c.FillRound(D2D1::RectF(sb.left + 4, ly - 3, sb.right - 4, ly + 6), 2, Alpha(pt.text, 0.10f));
        g_c.FillRound(D2D1::RectF(sb.left + 8, ly, sb.left + 8 + (sb.right - sb.left - 18) * (k == 0 ? 0.8f : 0.55f + 0.1f * (k % 2)), ly + 3), 1.5f,
                      k == 0 ? pt.text : pt.subtext);
    }
    // content: title, a big number with a sparkline, a progress bar
    float cx = sb.right + 8;
    g_c.FillRound(D2D1::RectF(cx, pv.top + 8, cx + w * 0.22f, pv.top + 12), 2, pt.text);
    float cw = (pv.right - cx - 8 - 6) / 2, ch = h - 34;
    for (int k = 0; k < 2; ++k) {
        D2D1_RECT_F c = D2D1::RectF(cx + k * (cw + 6), pv.top + 20, cx + k * (cw + 6) + cw, pv.top + 20 + ch);
        g_c.FillRound(c, 4, def.surfaceOpacity < 1.0f ? Rgb(def.surface, def.surfaceOpacity * 1.8f) : pt.surface);
        g_c.FillRound(D2D1::RectF(c.left + 5, c.top + 5, c.left + 5 + cw * 0.35f, c.top + 8), 1.5f, pt.subtext);
        g_c.FillRound(D2D1::RectF(c.left + 5, c.top + 12, c.left + 5 + cw * 0.3f, c.top + 18), 2, pt.text);
        // a little load graph
        float gy = c.bottom - 9, gh = std::max(4.0f, ch - 32);
        for (int b = 0; b < 6; ++b) {
            float bh = gh * (0.25f + 0.12f * ((b * 7 + k * 3) % 6));
            float bx = c.left + 5 + b * (cw - 10) / 6.0f;
            g_c.FillRound(D2D1::RectF(bx, gy - bh, bx + (cw - 10) / 6.0f - 2, gy), 1, k == 0 ? pt.accent : Alpha(pt.accent, 0.55f));
        }
        g_c.FillRound(D2D1::RectF(c.left + 5, c.bottom - 6, c.right - 5, c.bottom - 4), 1, pt.surfaceStrong);
    }
    if (g_previewLayer && clip) g_rt->PopLayer();
    if (clip) clip->Release();
    g_c.StrokeRound(pv, 7, Alpha(g_t.text, 0.10f));
    Text(def0.name, 13.5f, D2D1::RectF(r.left + 14, r.bottom - 44, r.right - 40, r.bottom - 24), g_t.text, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    Text(def0.description, 11.5f, D2D1::RectF(r.left + 14, r.bottom - 25, r.right - 12, r.bottom - 8), g_t.subtext);
    if (def0.custom) Pill(r.right - 12, r.bottom - 43, L"Custom", g_t.info, true);
    if (selected) {
        g_c.StrokeRound(r, 10, g_t.accent, 2);
        g_c.Circle(r.right - 22, r.top + 22, 10, g_t.accent);
        Glyph(icon::Check, 10, D2D1::RectF(r.right - 32, r.top + 12, r.right - 12, r.top + 32), g_t.onAccent);
    } else if (hv > 0.01f) {
        g_c.StrokeRound(r, 10, Alpha(g_t.accent, 0.5f * hv));
    }
    AddHot(r, Hit::ThemePick, -1, index);
}

void DrawThemes(float x0) {
    PageBox L = PageLayout(x0, 1100);
    float left = L.left, right = L.right;
    float y = 24 - g_scroll;
    y = PageTitle(left, right, y, L"Themes", L"Click a theme to try it. It changes right away.");

    auto all = AllThemes();
    float gap = 14, minW = 200, avail = right - left;
    int cols = std::max(1, (int)((avail + gap) / (minW + gap)));
    float cw = (avail - gap * (cols - 1)) / cols, ch = std::min(190.0f, cw * 0.62f + 50);
    for (size_t k = 0; k < all.size(); ++k) {
        int c = (int)(k % cols), row = (int)(k / cols);
        D2D1_RECT_F r = D2D1::RectF(left + c * (cw + gap), y + row * (ch + gap), left + c * (cw + gap) + cw, y + row * (ch + gap) + ch);
        if (Visible(r.top, r.bottom)) DrawThemePreview(all[k], r, (int)k, all[k].id == g_cfg.theme);
    }
    y += ((int)all.size() + cols - 1) / cols * (ch + gap) + 18;

    y = SectionTitle(left, right, y, L"ACCENT COLOR");
    D2D1_RECT_F box = D2D1::RectF(left, y, right, y + 64);
    Card(box);
    float ax = left + 16, ay = y + 16;
    ThemeDef cur = FindTheme(g_cfg.theme, g_customThemes);
    auto chip = [&](const std::wstring& label, uint32_t color, int arg, bool sel) {
        auto* f = Format(12.5f, sel ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL);
        float w = MeasureWidth(label, f) + 42;
        D2D1_RECT_F r = D2D1::RectF(ax, ay, ax + w, ay + 32);
        g_c.FillRound(r, 16, sel ? Alpha(g_t.accent, 0.18f) : (IsHover(Hit::AccentPick, -1, arg) ? g_t.surfaceHover : Mix(g_t.surface, g_t.surfaceStrong, 0.4f)));
        if (sel) g_c.StrokeRound(r, 16, g_t.accent, 1.5f);
        g_c.Circle(r.left + 16, ay + 16, 7, Rgb(color));
        g_c.Text(label, f, D2D1::RectF(r.left + 29, ay, r.right - 8, ay + 32), g_t.text);
        AddHot(r, Hit::AccentPick, -1, arg);
        ax += w + 8;
    };
    chip(L"Theme's own", ResolveAccent(cur, L"theme"), 0, g_cfg.accent == L"theme");
    chip(L"Windows", SystemAccent(), 1, g_cfg.accent == L"windows");
    ax += 6;
    bool customSel = g_cfg.accent.size() == 7 && g_cfg.accent[0] == L'#';
    for (int k = 0; k < kAccentSwatchCount; ++k) {
        bool sel = IEquals(g_cfg.accent, HexColor(kAccentSwatches[k]));
        if (sel) customSel = false;
        float cxx = ax + 16;
        if (cxx + 16 > right - 110) break;
        if (sel) g_c.Circle(cxx, ay + 16, 15, g_t.text);
        else if (IsHover(Hit::AccentPick, -1, 2 + k)) g_c.Circle(cxx, ay + 16, 15, Alpha(g_t.text, 0.3f));
        g_c.Circle(cxx, ay + 16, sel ? 12.5f : 13, Rgb(kAccentSwatches[k]));
        AddHot(D2D1::RectF(ax, ay, ax + 32, ay + 32), Hit::AccentPick, -1, 2 + k);
        ax += 38;
    }
    ax += 4;
    if (ax + 100 < right) chip(customSel ? g_cfg.accent : L"Custom…", customSel ? ResolveAccent(cur, g_cfg.accent) : 0x888888, 100, customSel);
    y += 64 + 26;

    y = SectionTitle(left, right, y, L"YOUR THEMES");
    D2D1_RECT_F b2 = D2D1::RectF(left, y, right, y + 64);
    Card(b2);
    float bx = right - 18;
    bx = ButtonRight(bx, y + 16, L"Reload", icon::Refresh, Btn::Secondary, Hit::ThemeReload) - 8;
    bx = ButtonRight(bx, y + 16, L"Open folder", icon::Folder, Btn::Secondary, Hit::ThemeFolder) - 8;
    bx = ButtonRight(bx, y + 16, L"Save as file", icon::Download, Btn::Secondary, Hit::ThemeExport) - 8;
    Text(L"Theme files", 14, D2D1::RectF(left + 18, y + 12, bx - 10, y + 32), g_t.text);
    Text(L"Save the current look as a .json file to tweak or share. Hangar's theme files work here too.", 12,
         D2D1::RectF(left + 18, y + 32, bx - 10, y + 50), g_t.subtext);
    y += 64 + 30;
    g_contentH = y + g_scroll;
}

bool ClickThemes(const HotRect& h) {
    switch (h.kind) {
        case Hit::ThemeBrowse: Navigate(Page::Themes); return true;
        case Hit::ThemePick: {
            auto all = AllThemes();
            if (h.arg >= 0 && h.arg < (int)all.size()) { g_cfg.theme = all[h.arg].id; ConfigSave(); ApplyTheme(); }
            return true;
        }
        case Hit::AccentPick:
            if (h.arg == 0) g_cfg.accent = L"theme";
            else if (h.arg == 1) g_cfg.accent = L"windows";
            else if (h.arg >= 2 && h.arg < 2 + kAccentSwatchCount) g_cfg.accent = HexColor(kAccentSwatches[h.arg - 2]);
            else if (!PickCustomAccent()) return true;
            ConfigSave();
            ApplyTheme();
            return true;
        case Hit::ThemeExport: ExportTheme(); return true;
        case Hit::ThemeFolder:
            EnsureDir(ThemesDir());
            OpenFolder(ThemesDir());
            return true;
        case Hit::ThemeReload:
            g_customThemes = LoadCustomThemes();
            ApplyTheme();
            Toast(std::to_wstring(g_customThemes.size()) + (g_customThemes.size() == 1 ? L" custom theme loaded." : L" custom themes loaded."));
            return true;
        default: return false;
    }
}
