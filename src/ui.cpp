// Helm - main window: sidebar, page switching, widgets, input, Helm's own updates.
// Drawn with Direct2D. All state lives on this thread; background work reports back with posted
// messages (see common.h).
#include "ui.h"
#include "ui_internal.h"
#include "config.h"
#include "util.h"
#include "json.h"
#include "themes.h"
#include "sysinfo.h"
#include "cleanup.h"
#include "storage.h"
#include "startup.h"
#include "icons.h"
#include "tools.h"
#include "selfupdate.h"
#include <windowsx.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <psapi.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <unordered_map>

// ---------------------------------------------------------------- shared state
HWND g_hwnd;
ID2D1HwndRenderTarget* g_rt;
static ID2D1SolidColorBrush* g_br;
Theme g_t;
Canvas g_c;
float g_W = 0, g_H = 0, g_scale = 1.0f;
float g_scroll = 0, g_scrollTarget = 0, g_contentH = 0, g_viewH = 0;
Page g_page = Page::Overview;
HotRect g_hover{{0, 0, 0, 0}, Hit::None, -1, 0, false};
HotRect g_pressed{{0, 0, 0, 0}, Hit::None, -1, 0, false};
bool g_animations = true;

static bool g_rtOpaque = false;
static std::vector<HotRect> g_hot;
static bool g_tracking = false;
static float g_pageAnim = 1.0f;
static std::map<Page, float> g_savedScroll;
static bool g_animTimer = false, g_animPending = false;
static LARGE_INTEGER g_qpf{}, g_lastAnimQpc{};

static std::wstring g_toast;
static bool g_toastBad = false;
static DWORD g_toastAt = 0;
static float g_toastAnim = 0;

// Helm's own updater
SelfUpdateState g_self;
std::vector<selfupdate::Previous> g_prevVersions;
static DWORD g_selfCheckedTick = 0;
static bool g_applyWhenIdle = false;
static std::wstring g_whatsNew, g_whatsNewVersion;
static StartOptions g_start;

enum : UINT_PTR { TIMER_ANIM = 1, TIMER_TOAST, TIMER_SELF, TIMER_HEALTHY, TIMER_REFRESH };

void Invalidate() { if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE); }
static void EnsureAnimTimer() {
    if (!g_animTimer && g_hwnd) { SetTimer(g_hwnd, TIMER_ANIM, 15, nullptr); g_animTimer = true; }
}
void StartAnim() {
    EnsureAnimTimer();
    Invalidate();
}
void RepaintNow() {
    if (!g_hwnd) return;
    g_pressed = {{0, 0, 0, 0}, Hit::None, -1, 0, false};
    RedrawWindow(g_hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
}

std::wstring PageId(Page p) {
    switch (p) {
        case Page::Cleanup: return L"cleanup";
        case Page::Storage: return L"storage";
        case Page::Startup: return L"startup";
        case Page::Tweaks: return L"tweaks";
        case Page::Tools: return L"tools";
        case Page::Themes: return L"themes";
        case Page::Settings: return L"settings";
        default: return L"overview";
    }
}
Page PageFromId(const std::wstring& id) {
    for (int i = 0; i < (int)Page::Count; ++i) if (PageId((Page)i) == id) return (Page)i;
    return Page::Overview;
}

void Toast(const std::wstring& text, bool bad) {
    if (!g_hwnd) return;
    g_toast = text;
    g_toastBad = bad;
    g_toastAt = GetTickCount();
    SetTimer(g_hwnd, TIMER_TOAST, bad ? 7000 : 4500, nullptr);
    StartAnim();
}

// From an elevated Helm, things it opens (a browser, another app) should run as you, not as
// administrator: hand them to Explorer, which runs unelevated.
void LaunchAsUser(const std::wstring& target) {
    if (IsElevated()) ShellExecuteW(g_hwnd, L"open", L"explorer.exe", (L"\"" + target + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
    else ShellExecuteW(g_hwnd, L"open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void OpenUrl(const std::wstring& url) { LaunchAsUser(url); }
void OpenFolder(const std::wstring& path) {
    if (!DirExists(path)) { Toast(L"That folder isn't there any more.", true); return; }
    ShellExecuteW(g_hwnd, L"open", L"explorer.exe", (L"\"" + path + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
}
void ShowInFolder(const std::wstring& file) {
    if (!FileExists(file) && !DirExists(file)) { Toast(L"That file isn't there any more.", true); return; }
    ShellExecuteW(g_hwnd, L"open", L"explorer.exe", (L"/select,\"" + file + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
}

bool Busy() { return CleanupBusy() || StorageBusy(); }

// ---------------------------------------------------------------- hit testing
void AddHot(const D2D1_RECT_F& r, Hit kind, int index, int arg, bool enabled) { g_hot.push_back({r, kind, index, arg, enabled}); }
bool IsHover(Hit kind, int index, int arg) { return g_hover.kind == kind && g_hover.index == index && g_hover.arg == arg; }
bool IsPressed(Hit kind, int index, int arg) {
    return g_pressed.kind == kind && g_pressed.index == index && g_pressed.arg == arg && IsHover(kind, index, arg);
}

static HotRect HitAt(float x, float y) {
    for (auto it = g_hot.rbegin(); it != g_hot.rend(); ++it)
        if (x >= it->r.left && x < it->r.right && y >= it->r.top && y < it->r.bottom) return *it;
    return {{0, 0, 0, 0}, Hit::None, -1, 0, false};
}

// ---------------------------------------------------------------- animation
struct AnimVal { float v, target, rate; };
static std::unordered_map<uint64_t, AnimVal> g_anim;

float Anim(uint64_t key, float target, float rate) {
    auto it = g_anim.find(key);
    if (it == g_anim.end()) { g_anim.emplace(key, AnimVal{target, target, rate}); return target; }
    it->second.target = target;
    it->second.rate = rate;
    if (!g_animations) it->second.v = target;
    if (std::fabs(it->second.v - target) > 0.001f) g_animPending = true;
    return it->second.v;
}

static double QpcMs(LARGE_INTEGER a, LARGE_INTEGER b) { return (b.QuadPart - a.QuadPart) * 1000.0 / g_qpf.QuadPart; }

static bool Ease(float& v, float target, float rate, float frames, float snap) {
    if (!g_animations || std::fabs(target - v) <= snap) { v = target; return false; }
    v += (target - v) * (1.0f - std::pow(1.0f - rate, frames));
    return true;
}

static void ReadAnimationSetting() {
    BOOL on = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &on, 0)) g_animations = on != FALSE;
}

static bool Animate() {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    float dt = g_lastAnimQpc.QuadPart ? (float)QpcMs(g_lastAnimQpc, now) : 16.67f;
    g_lastAnimQpc = now;
    float frames = std::clamp(dt / 16.67f, 0.25f, 4.0f);
    bool more = false;
    more |= Ease(g_scroll, g_scrollTarget, 0.26f, frames, 0.4f);
    for (auto& kv : g_anim) {
        AnimVal& a = kv.second;
        float span = std::max(1.0f, std::fabs(a.target));
        more |= Ease(a.v, a.target, a.rate, frames, 0.002f * span);
    }
    if (g_pageAnim < 1.0f) {
        g_pageAnim = g_animations ? std::min(1.0f, g_pageAnim + 0.11f * frames) : 1.0f;
        more |= g_pageAnim < 1.0f;
    }
    float toastTarget = (!g_toast.empty() && g_toastAt) ? 1.0f : 0.0f;
    if (!Ease(g_toastAnim, toastTarget, 0.25f, frames, 0.01f) && toastTarget == 0) g_toast.clear();
    else if (g_toastAnim != toastTarget) more = true;
    if (!more) g_lastAnimQpc.QuadPart = 0;
    return more;
}

// ---------------------------------------------------------------- widgets
void Glyph(wchar_t g, float size, const D2D1_RECT_F& r, D2D1_COLOR_F c) {
    g_c.Glyph(g, Format(size, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER, false, true), r, c);
}

void Text(const std::wstring& s, float size, const D2D1_RECT_F& r, D2D1_COLOR_F c, DWRITE_FONT_WEIGHT w, DWRITE_TEXT_ALIGNMENT align) {
    g_c.Text(s, Format(size, w, align), r, c);
}

void TextWrap(const std::wstring& s, float size, const D2D1_RECT_F& r, D2D1_COLOR_F c) {
    g_c.Text(s, Format(size, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, true), r, c);
}

float WrapHeight(const std::wstring& s, float size, float width) {
    return MeasureHeight(s, Format(size, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, true), width);
}

void Card(const D2D1_RECT_F& r, bool hover) {
    g_c.FillRound(r, 10, hover ? g_t.surfaceHover : g_t.surface);
}

void Divider(float left, float right, float y) { g_c.Line(left, y + 0.5f, right, y + 0.5f, g_t.divider); }

float ButtonWidth(const std::wstring& label, wchar_t glyph) {
    float w = MeasureWidth(label, Format(13, DWRITE_FONT_WEIGHT_SEMI_BOLD)) + 28;
    if (glyph) w += label.empty() ? 6 : 22;
    return std::max(w, label.empty() ? 34.0f : 76.0f);
}

void Button(const D2D1_RECT_F& r, const std::wstring& label, wchar_t glyph, Btn style, Hit kind, int index, int arg, bool enabled) {
    bool hot = enabled && IsHover(kind, index, arg);
    bool down = enabled && IsPressed(kind, index, arg);
    D2D1_COLOR_F bg, fg;
    switch (style) {
        case Btn::Primary: bg = hot ? g_t.accentHover : g_t.accent; fg = g_t.onAccent; break;
        case Btn::Danger: bg = hot ? Alpha(g_t.bad, 0.22f) : Alpha(g_t.bad, 0.12f); fg = g_t.bad; break;
        case Btn::Subtle: bg = hot ? g_t.surfaceHover : Alpha(g_t.surface, 0.0f); fg = g_t.text; break;
        default: bg = hot ? g_t.surfaceStrong : Mix(g_t.surface, g_t.surfaceStrong, 0.55f); fg = g_t.text; break;
    }
    if (!enabled) { bg = Alpha(g_t.surfaceStrong, 0.5f); fg = g_t.faint; }
    if (down) bg = Mix(bg, g_t.dark ? Rgb(0x000000) : Rgb(0xFFFFFF), 0.12f);
    g_c.FillRound(r, 6, bg);
    if (style == Btn::Secondary && enabled) g_c.StrokeRound(r, 6, g_t.divider);
    float w = r.right - r.left;
    auto* tf = Format(13, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
    if (glyph && label.empty()) {
        Glyph(glyph, 14, r, fg);
    } else if (glyph) {
        float tw = MeasureWidth(label, tf) + 22;
        float x = r.left + (w - tw) / 2;
        g_c.Glyph(glyph, Format(13, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, false, true), D2D1::RectF(x, r.top, x + 18, r.bottom), fg);
        g_c.Text(label, Format(13, DWRITE_FONT_WEIGHT_SEMI_BOLD), D2D1::RectF(x + 22, r.top, r.right, r.bottom), fg);
    } else {
        g_c.Text(label, tf, r, fg);
    }
    AddHot(r, kind, index, arg, enabled);
}

float ButtonRight(float right, float top, const std::wstring& label, wchar_t glyph, Btn style, Hit kind, int index, int arg, bool enabled) {
    float w = ButtonWidth(label, glyph);
    Button(D2D1::RectF(right - w, top, right, top + 32), label, glyph, style, kind, index, arg, enabled);
    return right - w;
}

float Pill(float x, float y, const std::wstring& text, D2D1_COLOR_F color, bool rightAlign) {
    if (text.empty()) return 0;
    auto* f = Format(11.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
    float w = MeasureWidth(text, f) + 16;
    if (rightAlign) x -= w;
    D2D1_RECT_F r = D2D1::RectF(x, y, x + w, y + 20);
    g_c.FillRound(r, 10, Alpha(color, g_t.dark ? 0.16f : 0.12f));
    g_c.Text(text, f, r, color);
    return w;
}

void ProgressBar(const D2D1_RECT_F& r, float pct, D2D1_COLOR_F color) {
    float rad = (r.bottom - r.top) / 2;
    g_c.FillRound(r, rad, g_t.surfaceStrong);
    float w = r.right - r.left;
    if (pct < 0) {
        float t = (GetTickCount() % 1400) / 1400.0f;
        float seg = w * 0.3f;
        float x = r.left + (w + seg) * t - seg;
        D2D1_RECT_F s = D2D1::RectF(std::max(r.left, x), r.top, std::min(r.right, x + seg), r.bottom);
        if (s.right > s.left) g_c.FillRound(s, rad, color);
        g_animPending = true;   // keeps moving
    } else {
        D2D1_RECT_F s = r;
        s.right = r.left + w * std::clamp(pct / 100.0f, 0.0f, 1.0f);
        if (s.right - s.left > 1) g_c.FillRound(s, rad, color);
    }
}

void Toggle(float x, float y, bool on, Hit kind, int index, int arg, bool enabled) {
    D2D1_RECT_F r = D2D1::RectF(x, y, x + 40, y + 22);
    float t = Anim(AKey((int)kind, index, arg + 7), on ? 1.0f : 0.0f, 0.3f);
    bool hot = enabled && IsHover(kind, index, arg);
    D2D1_COLOR_F offBg = hot ? Mix(g_t.surfaceStrong, g_t.text, 0.10f) : g_t.surfaceStrong;
    D2D1_COLOR_F bg = Mix(offBg, hot ? g_t.accentHover : g_t.accent, t);
    if (!enabled) bg = Alpha(bg, 0.45f);
    g_c.FillRound(r, 11, bg);
    if (t < 0.5f) g_c.StrokeRound(r, 11, Alpha(g_t.text, enabled ? 0.28f : 0.14f));
    float knobR = 7.0f + (IsPressed(kind, index, arg) ? 1.0f : 0.0f);
    float cx = x + 11 + t * 18;
    D2D1_COLOR_F knob = Mix(Alpha(g_t.text, 0.85f), g_t.onAccent, t);
    if (!enabled) knob = Alpha(knob, 0.5f);
    g_c.Circle(cx, y + 11, knobR, knob);
    AddHot(D2D1::RectF(x - 6, y - 6, x + 46, y + 28), kind, index, arg, enabled);
}

void Checkbox(float x, float y, bool checked, bool enabled, bool hot) {
    D2D1_RECT_F r = D2D1::RectF(x, y, x + 20, y + 20);
    if (checked) {
        g_c.FillRound(r, 5, enabled ? (hot ? g_t.accentHover : g_t.accent) : Alpha(g_t.accent, 0.4f));
        Glyph(icon::Check, 12, r, g_t.onAccent);
    } else {
        g_c.FillRound(r, 5, hot && enabled ? g_t.surfaceHover : Alpha(g_t.surface, 0.0f));
        g_c.StrokeRound(r, 5, Alpha(g_t.text, enabled ? 0.45f : 0.18f), 1.5f);
    }
}

float Segmented(float x, float y, const std::vector<std::wstring>& opts, int sel, Hit kind, bool rightAlign) {
    float total = 0;
    for (auto& o : opts) total += MeasureWidth(o, Format(13, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER)) + 30 + 6;
    total -= 6;
    float cx = rightAlign ? x - total : x;
    float start = cx;
    for (size_t k = 0; k < opts.size(); ++k) {
        float w = MeasureWidth(opts[k], Format(13, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER)) + 30;
        D2D1_RECT_F r = D2D1::RectF(cx, y, cx + w, y + 32);
        bool s = (int)k == sel;
        g_c.FillRound(r, 6, s ? Alpha(g_t.accent, 0.18f) : (IsHover(kind, -1, (int)k) ? g_t.surfaceHover : Mix(g_t.surface, g_t.surfaceStrong, 0.3f)));
        if (s) g_c.StrokeRound(r, 6, Alpha(g_t.accent, 0.6f));
        Text(opts[k], 13, r, s ? g_t.text : g_t.subtext, s ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
        AddHot(r, kind, -1, (int)k);
        cx += w + 6;
    }
    return start;
}

float Banner(float left, float right, float y, wchar_t glyph, D2D1_COLOR_F color, const std::wstring& text, const std::wstring& button, Hit kind,
             int index, wchar_t buttonGlyph) {
    float bw = button.empty() ? 0 : ButtonWidth(button, buttonGlyph) + 16;
    float tw = right - left - 52 - bw - 12;
    float th = std::max(20.0f, WrapHeight(text, 13, tw));
    float h = std::max(52.0f, th + 26);
    D2D1_RECT_F r = D2D1::RectF(left, y, right, y + h);
    g_c.FillRound(r, 10, Alpha(color, g_t.dark ? 0.13f : 0.10f));
    g_c.StrokeRound(r, 10, Alpha(color, 0.30f));
    Glyph(glyph, 15, D2D1::RectF(left + 14, y, left + 42, y + h), color);
    TextWrap(text, 13, D2D1::RectF(left + 48, y + (h - th) / 2, left + 48 + tw, y + (h + th) / 2 + 2), g_t.text);
    if (!button.empty()) ButtonRight(right - 12, y + (h - 32) / 2, button, buttonGlyph, Btn::Secondary, kind, index);
    return h;
}

float PageTitle(float left, float right, float y, const std::wstring& title, const std::wstring& sub) {
    Text(title, 24, D2D1::RectF(left, y, right, y + 34), g_t.text, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    if (!sub.empty()) Text(sub, 13, D2D1::RectF(left, y + 34, right, y + 54), g_t.subtext);
    return y + (sub.empty() ? 46 : 70);
}

float SectionTitle(float left, float right, float y, const std::wstring& text) {
    Text(text, 12, D2D1::RectF(left + 2, y, right, y + 20), g_t.subtext, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    return y + 28;
}

PageBox PageLayout(float x0, float maxWidth) {
    float left = x0 + kPad;
    float right = std::min(g_W - kPad + 8, left + maxWidth);
    return {left, std::max(left + 200, right)};
}

// ---------------------------------------------------------------- program icons
struct IconEntry {
    std::vector<uint8_t> pixels;
    UINT w = 0, h = 0;
    ID2D1Bitmap* bmp = nullptr;
    bool done = false;
};
static std::unordered_map<std::wstring, IconEntry> g_icons;

static UINT IconPx() { return (UINT)std::clamp(std::ceil(32 * g_scale), 16.0f, 96.0f); }

void ProgramIcon(const std::wstring& path, const D2D1_RECT_F& r, wchar_t fallback) {
    std::wstring key = Lower(path);
    if (!key.empty()) {
        auto it = g_icons.find(key);
        if (it == g_icons.end()) {
            g_icons[key] = IconEntry{};
            icons::Request(g_hwnd, path, IconPx());
        } else {
            IconEntry& e = it->second;
            if (!e.bmp && !e.pixels.empty() && g_rt) {
                D2D1_BITMAP_PROPERTIES bp = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
                g_rt->CreateBitmap(D2D1::SizeU(e.w, e.h), e.pixels.data(), e.w * 4, bp, &e.bmp);
            }
            if (e.bmp) {
                g_rt->DrawBitmap(e.bmp, r, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                return;
            }
        }
    }
    g_c.FillRound(r, (r.right - r.left) * 0.24f, Alpha(g_t.accent, 0.16f));
    Glyph(fallback, (r.bottom - r.top) * 0.5f, r, g_t.accent);
}

static void OnIcon(IconResult* r) {
    auto it = g_icons.find(r->key);
    if (it == g_icons.end()) return;
    SafeRelease(it->second.bmp);
    it->second.pixels = std::move(r->pixels);
    it->second.w = r->w;
    it->second.h = r->h;
    it->second.done = true;
    Invalidate();
}

// ---------------------------------------------------------------- render target
static void DiscardDevice() {
    for (auto& kv : g_icons) SafeRelease(kv.second.bmp);
    ThemesDiscardDevice();
    SafeRelease(g_br);
    SafeRelease(g_rt);
}

static bool EnsureDevice() {
    if (g_rt) return true;
    RECT rc;
    GetClientRect(g_hwnd, &rc);
    g_rtOpaque = !g_t.backdrop;
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, g_rtOpaque ? D2D1_ALPHA_MODE_IGNORE : D2D1_ALPHA_MODE_PREMULTIPLIED));
    UINT dpi = GetDpiForWindow(g_hwnd);
    props.dpiX = props.dpiY = (float)dpi;
    if (FAILED(d2d->CreateHwndRenderTarget(props, D2D1::HwndRenderTargetProperties(g_hwnd, D2D1::SizeU(std::max<LONG>(1, rc.right), std::max<LONG>(1, rc.bottom)),
                                           D2D1_PRESENT_OPTIONS_NONE), &g_rt))) return false;
    g_rt->SetTextAntialiasMode(g_rtOpaque ? D2D1_TEXT_ANTIALIAS_MODE_DEFAULT : D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    g_rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), &g_br);
    return true;
}

// called by the Themes page after g_t changes
void ThemeChanged() {
    if (g_rt && g_rtOpaque != !g_t.backdrop) DiscardDevice();   // switch between ClearType and see-through
    if (g_hwnd) { ApplyWindowTheme(g_hwnd, g_t); InvalidateRect(g_hwnd, nullptr, FALSE); }
}

// ---------------------------------------------------------------- sidebar
struct NavDef { Page page; const wchar_t* label; wchar_t glyph; };
static const NavDef kNav[] = {
    {Page::Overview, L"Overview", icon::Home},
    {Page::Cleanup, L"Cleanup", icon::Broom},
    {Page::Storage, L"Storage", icon::Drive},
    {Page::Startup, L"Startup apps", icon::Lightning},
    {Page::Tweaks, L"Tweaks", icon::Sliders},
    {Page::Tools, L"Tools", icon::Tools},
};

static void NavItem(float y, Page page, const std::wstring& label, wchar_t glyph, const std::wstring& badge, bool badgeAccent) {
    D2D1_RECT_F r = D2D1::RectF(10, y, kSidebar - 10, y + 38);
    bool sel = g_page == page;
    bool hot = IsHover(Hit::Nav, (int)page);
    float h = Anim(AKey(1000, (int)page), hot ? 1.0f : 0.0f, 0.3f);
    if (sel) g_c.FillRound(r, 7, g_t.dark ? Rgb(0xFFFFFF, 0.08f) : Rgb(0x000000, 0.06f));
    else if (h > 0.01f) g_c.FillRound(r, 7, g_t.dark ? Rgb(0xFFFFFF, 0.045f * h) : Rgb(0x000000, 0.04f * h));
    float bar = Anim(AKey(1001, (int)page), sel ? 1.0f : 0.0f, 0.25f);
    if (bar > 0.02f) g_c.FillRound(D2D1::RectF(r.left, y + 19 - 8 * bar, r.left + 3, y + 19 + 8 * bar), 1.5f, g_t.accent);
    Glyph(glyph, 16, D2D1::RectF(r.left + 10, y, r.left + 36, y + 38), sel ? g_t.accent : g_t.subtext);
    Text(label, 13.5f, D2D1::RectF(r.left + 44, y, r.right - 60, y + 38), g_t.text, sel ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL);
    if (!badge.empty()) {
        if (badgeAccent) {
            auto* f = Format(11.5f, DWRITE_FONT_WEIGHT_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
            float w = std::max(22.0f, MeasureWidth(badge, f) + 14);
            D2D1_RECT_F b = D2D1::RectF(r.right - 10 - w, y + 9, r.right - 10, y + 29);
            g_c.FillRound(b, 10, Alpha(g_t.accent, 0.2f));
            g_c.Text(badge, f, b, g_t.accent);
        } else {
            Text(badge, 12.5f, D2D1::RectF(r.left, y, r.right - 12, y + 38), g_t.faint, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING);
        }
    }
    AddHot(r, Hit::Nav, (int)page);
}

static void DrawLogo(float x, float y, float s) {
    D2D1_RECT_F tile = D2D1::RectF(x, y, x + s, y + s);
    g_c.FillRound(tile, s * 0.27f, g_t.accent);
    // a ship's wheel: rim, eight spokes with handles, hub
    float cx = x + s / 2, cy = y + s / 2;
    float rim = s * 0.25f, stroke = std::max(1.6f, s * 0.07f);
    g_br->SetColor(g_t.onAccent);
    g_rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), rim, rim), g_br, stroke);
    for (int k = 0; k < 8; ++k) {
        float a = k * 3.14159265f / 4;
        float c = std::cos(a), sn = std::sin(a);
        g_rt->DrawLine(D2D1::Point2F(cx + c * s * 0.08f, cy + sn * s * 0.08f), D2D1::Point2F(cx + c * s * 0.38f, cy + sn * s * 0.38f), g_br, stroke);
    }
    g_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), s * 0.09f, s * 0.09f), g_br);
}

static void DrawSidebar() {
    g_c.Fill(D2D1::RectF(0, 0, kSidebar, g_H), g_t.sidebar);
    g_c.Line(kSidebar - 0.5f, 0, kSidebar - 0.5f, g_H, g_t.divider);
    DrawLogo(22, 22, 30);
    Text(L"Helm", 17, D2D1::RectF(62, 20, kSidebar, 40), g_t.text, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    Text(L"PC care", 11.5f, D2D1::RectF(62, 39, kSidebar, 55), g_t.subtext);

    float y = 80;
    for (auto& n : kNav) {
        std::wstring badge;
        bool accent = false;
        if (n.page == Page::Cleanup) {
            uint64_t junk = 0;
            if (CleanupBusy()) badge = L"…";
            else if (CleanupJunk(&junk) && junk >= 50ull * 1024 * 1024) { badge = FormatBytes(junk); accent = true; }
        } else if (n.page == Page::Startup) {
            int en = 0, total = 0;
            if (StartupCounts(&en, &total)) badge = std::to_wstring(en);
        } else if (n.page == Page::Storage && StorageBusy()) {
            badge = L"…";
        }
        NavItem(y, n.page, n.label, n.glyph, badge, accent);
        y += 40;
    }
    float bottom = g_H - 46;
    float settingsY = bottom - 40, themesY = settingsY - 40;
    if (g_self.kind == SelfUpdateState::Ready && y + 112 < themesY) {
        D2D1_RECT_F b = D2D1::RectF(12, themesY - 100, kSidebar - 12, themesY - 10);
        g_c.FillRound(b, 9, Alpha(g_t.accent, 0.14f));
        g_c.StrokeRound(b, 9, Alpha(g_t.accent, 0.35f));
        Text(L"Helm " + g_self.version + L" is ready", 12.5f, D2D1::RectF(b.left + 12, b.top + 8, b.right - 8, b.top + 28), g_t.text, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        Text(Busy() ? L"Installs when Helm is done" : L"Takes a second · you stay here", 11.5f, D2D1::RectF(b.left + 12, b.top + 27, b.right - 8, b.top + 44), g_t.subtext);
        Button(D2D1::RectF(b.left + 10, b.bottom - 38, b.right - 10, b.bottom - 8), L"Restart to update", icon::Update, Btn::Primary, Hit::SelfRestart);
    }
    if (y + 4 < themesY) NavItem(themesY, Page::Themes, L"Themes", icon::Color, L"", false);
    if (y + 4 < settingsY) NavItem(settingsY, Page::Settings, L"Settings", icon::Settings, L"", false);
    std::wstring status = std::wstring(L"Helm ") + HELM_VERSION;
    if (IsElevated()) status += L" · Administrator";
    Text(status, 11.5f, D2D1::RectF(24, g_H - 36, kSidebar - 12, g_H - 16), g_t.faint);
}

// ---------------------------------------------------------------- overlays
static void DrawToast() {
    if (g_toastAnim <= 0.01f || g_toast.empty()) return;
    auto* f = Format(13.5f);
    float maxW = g_W - kSidebar - 80;
    float w = std::min(maxW, MeasureWidth(g_toast, f) + 74);
    float cx = kSidebar + (g_W - kSidebar) / 2;
    float y = g_H - 20 - 46 * g_toastAnim;
    D2D1_RECT_F r = D2D1::RectF(cx - w / 2, y, cx + w / 2, y + 42);
    g_c.FillRound(r, 9, Alpha(g_t.dark ? Rgb(0x2E2E36) : Rgb(0x26262C), g_toastAnim));
    Glyph(g_toastBad ? icon::Warning : icon::Check, 13, D2D1::RectF(r.left + 8, r.top, r.left + 38, r.bottom),
          Alpha(g_toastBad ? Rgb(0xFF8A8A) : Rgb(0x7BE0A6), g_toastAnim));
    g_c.Text(g_toast, f, D2D1::RectF(r.left + 40, r.top, r.right - 32, r.bottom), Alpha(Rgb(0xF4F4F6), g_toastAnim));
    Glyph(icon::Cancel, 9, D2D1::RectF(r.right - 32, r.top + 8, r.right - 8, r.bottom - 8), Alpha(Rgb(0xB0B0BA), g_toastAnim));
    AddHot(r, Hit::ToastClose);
}

static std::wstring CleanNotes(const std::wstring& md) {
    std::wstring out, line;
    size_t i = 0;
    while (i <= md.size()) {
        size_t e = md.find(L'\n', i);
        if (e == std::wstring::npos) e = md.size();
        line = md.substr(i, e - i);
        i = e + 1;
        while (!line.empty() && (line.back() == L'\r' || line.back() == L' ')) line.pop_back();
        size_t s = line.find_first_not_of(L' ');
        if (s == std::wstring::npos) { if (!out.empty() && out.back() != L'\n') out += L"\n"; continue; }
        std::wstring t = line.substr(s);
        while (!t.empty() && t[0] == L'#') t.erase(0, 1);
        if (IStartsWith(t, L"- ") || IStartsWith(t, L"* ")) t = L"•  " + t.substr(2);
        std::wstring clean;
        for (size_t k = 0; k < t.size(); ++k) {
            if (t[k] == L'*' || t[k] == L'`') continue;
            clean += t[k];
        }
        clean = Trim(clean);
        // the notes start with "## Helm 0.1.1", which the dialog's title already says
        if (out.empty() && IStartsWith(clean, L"Helm ") && clean.find(L' ', 5) == std::wstring::npos) continue;
        out += clean + L"\n";
        if (out.size() > 3000) break;
    }
    return Trim(out);
}

static void DrawWhatsNew() {
    if (g_whatsNew.empty()) return;
    g_hot.clear();   // modal
    g_c.Fill(D2D1::RectF(0, 0, g_W, g_H), Rgb(0x000000, 0.45f));
    AddHot(D2D1::RectF(0, 0, g_W, g_H), Hit::WhatsNewClose);
    float w = std::min(560.0f, g_W - 80);
    float th = std::min(WrapHeight(g_whatsNew, 13.5f, w - 56), g_H - 260);
    float h = th + 150;
    D2D1_RECT_F r = D2D1::RectF((g_W - w) / 2, (g_H - h) / 2, (g_W + w) / 2, (g_H + h) / 2);
    g_c.FillRound(r, 12, g_t.dark ? Rgb(0x1F1F25) : Rgb(0xFFFFFF));
    g_c.StrokeRound(r, 12, g_t.divider);
    AddHot(r, Hit::Modal);
    Glyph(icon::Star, 18, D2D1::RectF(r.left + 22, r.top + 22, r.left + 50, r.top + 50), g_t.accent);
    Text(L"Helm updated to " + g_whatsNewVersion, 19, D2D1::RectF(r.left + 56, r.top + 20, r.right - 24, r.top + 52), g_t.text, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    Text(L"You're right where you left off.", 12.5f, D2D1::RectF(r.left + 56, r.top + 50, r.right - 24, r.top + 70), g_t.subtext);
    g_rt->PushAxisAlignedClip(D2D1::RectF(r.left + 28, r.top + 84, r.right - 28, r.top + 84 + th), D2D1_ANTIALIAS_MODE_ALIASED);
    TextWrap(g_whatsNew, 13.5f, D2D1::RectF(r.left + 28, r.top + 84, r.right - 28, r.top + 84 + th + 4), g_t.subtext);
    g_rt->PopAxisAlignedClip();
    Button(D2D1::RectF(r.right - 120, r.bottom - 50, r.right - 24, r.bottom - 18), L"Got it", 0, Btn::Primary, Hit::WhatsNewClose);
}

// ---------------------------------------------------------------- scrolling
static float g_trackTop = 0, g_trackH = 0, g_thumbTop = 0, g_thumbH = 0, g_dragGrab = 0;
static bool g_dragThumb = false;

static void ClampScroll() {
    float maxS = std::max(0.0f, g_contentH - g_viewH);
    g_scrollTarget = std::clamp(g_scrollTarget, 0.0f, maxS);
    g_scroll = std::clamp(g_scroll, 0.0f, maxS);
}

static void Scroll(float delta) {
    g_scrollTarget -= delta;
    ClampScroll();
    StartAnim();
}

static void DrawScrollbar() {
    if (g_contentH <= g_viewH + 1) { g_thumbH = 0; return; }
    g_trackTop = 4;
    g_trackH = g_viewH - 8;
    g_thumbH = std::max(40.0f, g_trackH * g_viewH / g_contentH);
    float t = g_scroll / (g_contentH - g_viewH);
    g_thumbTop = g_trackTop + (g_trackH - g_thumbH) * std::clamp(t, 0.0f, 1.0f);
    bool active = g_dragThumb || IsHover(Hit::ScrollThumb) || IsHover(Hit::ScrollTrack);
    float a = Anim(AKey(1100), active ? 1.0f : 0.0f, 0.3f);
    float w = 4.0f + 4.0f * a;
    if (a > 0.01f) g_c.FillRound(D2D1::RectF(g_W - 12, g_trackTop, g_W - 2, g_trackTop + g_trackH), 5, Alpha(g_t.text, 0.05f * a));
    D2D1_COLOR_F thumb = g_t.dark ? Rgb(0xFFFFFF, 0.18f + 0.16f * a) : Rgb(0x000000, 0.2f + 0.16f * a);
    g_c.FillRound(D2D1::RectF(g_W - 3 - w, g_thumbTop, g_W - 3, g_thumbTop + g_thumbH), w / 2, thumb);
    AddHot(D2D1::RectF(g_W - 14, g_trackTop, g_W, g_trackTop + g_trackH), Hit::ScrollTrack);
    AddHot(D2D1::RectF(g_W - 14, g_thumbTop, g_W, g_thumbTop + g_thumbH), Hit::ScrollThumb);
}

static void DragThumbTo(float y) {
    float range = g_trackH - g_thumbH;
    if (range <= 0) return;
    float t = std::clamp((y - g_dragGrab - g_trackTop) / range, 0.0f, 1.0f);
    g_scroll = g_scrollTarget = t * (g_contentH - g_viewH);
    Invalidate();
}

// ---------------------------------------------------------------- paint
static void Paint() {
    if (!EnsureDevice()) return;
    D2D1_SIZE_F sz = g_rt->GetSize();
    g_W = sz.width;
    g_H = sz.height;
    g_viewH = g_H;
    g_c.rt = g_rt;
    g_c.br = g_br;
    g_hot.clear();
    g_animPending = false;
    g_rt->BeginDraw();
    g_rt->SetTransform(D2D1::Matrix3x2F::Identity());
    g_rt->Clear(g_t.bg);
    float x0 = kSidebar;
    g_rt->PushAxisAlignedClip(D2D1::RectF(x0, 0, g_W, g_H), D2D1_ANTIALIAS_MODE_ALIASED);
    float slide = (1.0f - g_pageAnim) * 12;
    if (slide > 0.05f) g_rt->SetTransform(D2D1::Matrix3x2F::Translation(0, slide));
    switch (g_page) {
        case Page::Overview: DrawOverview(x0); break;
        case Page::Cleanup: DrawCleanup(x0); break;
        case Page::Storage: DrawStorage(x0); break;
        case Page::Startup: DrawStartup(x0); break;
        case Page::Tweaks: DrawTweaks(x0); break;
        case Page::Tools: DrawTools(x0); break;
        case Page::Themes: DrawThemes(x0); break;
        default: DrawSettings(x0); break;
    }
    g_rt->SetTransform(D2D1::Matrix3x2F::Identity());
    if (g_pageAnim < 0.999f) { D2D1_COLOR_F o = g_t.bg; o.a = (1.0f - g_pageAnim) * 0.85f; g_c.Fill(D2D1::RectF(x0, 0, g_W, g_H), o); }
    g_rt->PopAxisAlignedClip();
    if (slide > 0.05f) {
        // hit areas were recorded before the slide; nudge them to where things are drawn
        for (auto& h : g_hot) { h.r.top += slide; h.r.bottom += slide; }
    }
    DrawScrollbar();
    DrawSidebar();
    DrawToast();
    DrawWhatsNew();
    HRESULT hr = g_rt->EndDraw();
    if (hr == (HRESULT)D2DERR_RECREATE_TARGET) DiscardDevice();
    gfx::FrameDone();
    ClampScroll();
    // keep animating on the timer; invalidating from inside a paint would starve WM_TIMER
    if (g_animPending) EnsureAnimTimer();
}

// ---------------------------------------------------------------- navigation
static void UpdateMonitor() {
    bool visible = g_hwnd && IsWindowVisible(g_hwnd) && !IsIconic(g_hwnd);
    sysinfo::SetActive(visible && g_page == Page::Overview);
}

static void OnEnter(Page p) {
    switch (p) {
        case Page::Overview: OverviewEnter(); break;
        case Page::Tweaks: TweaksEnter(); break;
        case Page::Tools: ToolsEnter(); break;
        case Page::Settings: SettingsEnter(); break;
        default: break;
    }
}

void Navigate(Page p) {
    if (p == g_page) { g_scrollTarget = 0; StartAnim(); return; }
    g_savedScroll[g_page] = g_scrollTarget;
    g_page = p;
    g_scroll = g_scrollTarget = g_savedScroll.count(p) ? g_savedScroll[p] : 0;
    g_pageAnim = g_animations ? 0.0f : 1.0f;
    g_cfg.page = PageId(p);
    OnEnter(p);
    UpdateMonitor();
    StartAnim();
}

// ---------------------------------------------------------------- Helm's own updates, elevation
static void SaveWindowPlacement() {
    WINDOWPLACEMENT wp{sizeof(wp)};
    if (!GetWindowPlacement(g_hwnd, &wp)) return;
    g_cfg.maximized = wp.showCmd == SW_SHOWMAXIMIZED;
    RECT r = wp.rcNormalPosition;
    g_cfg.windowX = r.left;
    g_cfg.windowY = r.top;
    g_cfg.windowW = r.right - r.left;
    g_cfg.windowH = r.bottom - r.top;
}

static std::wstring RestoreJson() {
    json::Value v = json::Value::MakeObject();
    v.Set(L"page", json::Value::Str(PageId(g_page)));
    v.Set(L"scroll", json::Value::Num(g_scrollTarget));
    v.Set(L"background", json::Value::Boolean(GetForegroundWindow() != g_hwnd));
    v.Set(L"minimized", json::Value::Boolean(IsIconic(g_hwnd) != 0));
    std::wstring out;
    json::Write(v, out);
    return out;
}

void ApplySelf(const std::wstring& exe, const std::wstring& version, bool userAsked) {
    if (Busy()) {
        g_applyWhenIdle = true;
        if (userAsked) Toast(L"Helm will restart to update once it's done.");
        return;
    }
    g_applyWhenIdle = false;
    SaveWindowPlacement();
    ConfigSave();
    selfupdate::SaveRestoreState(RestoreJson());
    Log(L"self-update: restarting into %ls", version.c_str());
    std::wstring err;
    if (selfupdate::Apply(g_hwnd, exe, version, &err)) {
        DestroyWindow(g_hwnd);   // the new copy has taken over
        return;
    }
    DeleteFileW((DataDir() + L"\\restore.json").c_str());
    if (IStartsWith(err, L"Helm " + version + L" didn't start")) {
        SkipVersion(version);
        ConfigSave();
        g_self = SelfUpdateState{};
        g_self.kind = SelfUpdateState::Error;
        g_self.message = err;
    }
    Toast(err, true);
}

static void MaybeAutoApplySelf() {
    if (g_self.kind != SelfUpdateState::Ready || Busy()) return;
    if (!g_cfg.helmAuto && !g_applyWhenIdle) return;
    if (!g_whatsNew.empty()) return;
    LASTINPUTINFO li{sizeof(li)};
    bool idle = GetLastInputInfo(&li) && GetTickCount() - li.dwTime > 2 * 60 * 1000;
    bool away = IsIconic(g_hwnd) || idle;
    if (g_applyWhenIdle || away) ApplySelf(g_self.staged, g_self.version, false);
}

void CheckSelf(bool force) {
    g_selfCheckedTick = GetTickCount();
    selfupdate::Check(g_hwnd, force);
}

void RelaunchElevated() {
    if (IsElevated()) return;
    if (Busy()) { Toast(L"Wait for Helm to finish, then try again.", true); return; }
    SaveWindowPlacement();
    ConfigSave();
    if (!ShellRunAs(ExePath(), L"--elevated --page " + PageId(g_page), true)) {
        Toast(L"Helm is still running without administrator rights.", true);
        return;
    }
    // the new copy waits for this one to let go
    ReleaseInstanceMutex();
    DestroyWindow(g_hwnd);
}

// ---------------------------------------------------------------- input
static void Click(const HotRect& h) {
    if (!h.enabled) return;
    switch (h.kind) {
        case Hit::Nav: if (h.index >= 0 && h.index < (int)Page::Count) Navigate((Page)h.index); break;
        case Hit::ToastClose: g_toastAt = 0; break;
        case Hit::WhatsNewClose: g_whatsNew.clear(); break;
        case Hit::Elevate: RelaunchElevated(); return;
        case Hit::SelfRestart: if (g_self.kind == SelfUpdateState::Ready) ApplySelf(g_self.staged, g_self.version, true); return;
        default:
            if (ClickOverview(h) || ClickCleanup(h) || ClickStorage(h) || ClickStartup(h) || ClickTweaks(h) || ClickTools(h) || ClickThemes(h) ||
                ClickSettings(h)) {}
            break;
    }
    // Lists may have changed (a new scan, a different folder): redraw now, so a quick second click
    // lands on what's actually on screen and never on a stale row.
    StartAnim();
    RepaintNow();
}

static void OnKey(WPARAM vk) {
    if (!g_whatsNew.empty()) {
        if (vk == VK_ESCAPE || vk == VK_RETURN || vk == VK_SPACE) { g_whatsNew.clear(); Invalidate(); }
        return;
    }
    bool ctrl = GetKeyState(VK_CONTROL) < 0;
    if (ctrl && vk >= '1' && vk <= '6') { Navigate(kNav[vk - '1'].page); return; }
    if (ctrl && vk == VK_OEM_COMMA) { Navigate(Page::Settings); return; }
    if (vk == VK_F5) {
        switch (g_page) {
            case Page::Cleanup: if (!CleanupBusy()) CleanupStartScan(); break;
            case Page::Startup: StartupReload(); break;
            default: OnEnter(g_page); break;
        }
        Invalidate();
        return;
    }
    if (g_page == Page::Storage && KeyStorage(vk)) return;
    switch (vk) {
        case VK_DOWN: Scroll(-60); break;
        case VK_UP: Scroll(60); break;
        case VK_NEXT: case VK_SPACE: Scroll(-(g_viewH - 80)); break;
        case VK_PRIOR: Scroll(g_viewH - 80); break;
        case VK_HOME: g_scrollTarget = 0; StartAnim(); break;
        case VK_END: g_scrollTarget = g_contentH; ClampScroll(); StartAnim(); break;
    }
}

// ---------------------------------------------------------------- window
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            g_hwnd = hwnd;
            g_scale = GetDpiForWindow(hwnd) / 96.0f;
            ThemesInit();
            ApplyTheme();
            // an elevated Helm still accepts "come to the front" from a normal one
            ChangeWindowMessageFilterEx(hwnd, WM_HELM_ACTIVATE, MSGFLT_ALLOW, nullptr);
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            Paint();
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_SIZE:
            if (g_rt) g_rt->Resize(D2D1::SizeU(LOWORD(lp), HIWORD(lp)));
            if (wp == SIZE_MINIMIZED) {
                gfx::ClearTextCaches();
                SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
            }
            UpdateMonitor();
            Invalidate();
            return 0;
        case WM_SHOWWINDOW:
            UpdateMonitor();
            break;
        case WM_GETMINMAXINFO: {
            auto* mm = (MINMAXINFO*)lp;
            float s = GetDpiForWindow(hwnd) / 96.0f;
            mm->ptMinTrackSize.x = (LONG)(900 * s);
            mm->ptMinTrackSize.y = (LONG)(600 * s);
            return 0;
        }
        case WM_DPICHANGED: {
            g_scale = HIWORD(wp) / 96.0f;
            RECT* r = (RECT*)lp;
            SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            if (g_rt) g_rt->SetDpi((float)HIWORD(wp), (float)HIWORD(wp));
            Invalidate();
            return 0;
        }
        case WM_SETTINGCHANGE:
            ReadAnimationSetting();
            if (lp && wcscmp((const wchar_t*)lp, L"ImmersiveColorSet") == 0) { InvalidateSystemColors(); ApplyTheme(); }
            return 0;
        case WM_DWMCOLORIZATIONCOLORCHANGED:
            InvalidateSystemColors();
            ApplyTheme();
            return 0;
        case WM_MOUSEMOVE: {
            float x = GET_X_LPARAM(lp) / g_scale, y = GET_Y_LPARAM(lp) / g_scale;
            if (g_dragThumb) { DragThumbTo(y); return 0; }
            if (!g_tracking) {
                TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, hwnd, 0};
                TrackMouseEvent(&t);
                g_tracking = true;
            }
            HotRect h = HitAt(x, y);
            if (h.kind != g_hover.kind || h.index != g_hover.index || h.arg != g_hover.arg) { g_hover = h; StartAnim(); }
            bool hand = h.kind != Hit::None && h.kind != Hit::Modal && h.enabled && h.kind != Hit::ScrollThumb && h.kind != Hit::ScrollTrack;
            SetCursor(LoadCursorW(nullptr, hand ? IDC_HAND : IDC_ARROW));
            return 0;
        }
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT) return TRUE;
            break;
        case WM_MOUSELEAVE:
            g_tracking = false;
            g_hover = {{0, 0, 0, 0}, Hit::None, -1, 0, false};
            StartAnim();
            return 0;
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK: {
            SetFocus(hwnd);
            float x = GET_X_LPARAM(lp) / g_scale, y = GET_Y_LPARAM(lp) / g_scale;
            g_pressed = HitAt(x, y);
            if (g_pressed.kind == Hit::ScrollThumb) {
                g_dragThumb = true;
                g_dragGrab = y - g_thumbTop;
            } else if (g_pressed.kind == Hit::ScrollTrack) {
                Scroll(y < g_thumbTop ? g_viewH - 60 : -(g_viewH - 60));
            }
            SetCapture(hwnd);
            Invalidate();
            return 0;
        }
        case WM_LBUTTONUP: {
            ReleaseCapture();
            if (g_dragThumb) { g_dragThumb = false; g_pressed = {{0, 0, 0, 0}, Hit::None, -1, 0, false}; Invalidate(); return 0; }
            float x = GET_X_LPARAM(lp) / g_scale, y = GET_Y_LPARAM(lp) / g_scale;
            HotRect up = HitAt(x, y);
            HotRect down = g_pressed;
            g_pressed = {{0, 0, 0, 0}, Hit::None, -1, 0, false};
            if (up.kind == down.kind && up.index == down.index && up.arg == down.arg && up.kind != Hit::None) Click(up);
            Invalidate();
            return 0;
        }
        case WM_CAPTURECHANGED:
            g_dragThumb = false;
            return 0;
        case WM_XBUTTONUP:
            if (GET_XBUTTON_WPARAM(wp) == XBUTTON1 && g_page == Page::Storage) KeyStorage(VK_BROWSER_BACK);
            return TRUE;
        case WM_MOUSEWHEEL:
            if (g_whatsNew.empty()) Scroll(GET_WHEEL_DELTA_WPARAM(wp) / 120.0f * 96.0f);
            return 0;
        case WM_KEYDOWN:
            OnKey(wp);
            return 0;
        case WM_SYSKEYDOWN:
            if (wp == VK_LEFT && g_page == Page::Storage) { KeyStorage(VK_BROWSER_BACK); return 0; }
            break;
        case WM_TIMER:
            switch (wp) {
                case TIMER_ANIM:
                    if (!Animate()) { KillTimer(hwnd, TIMER_ANIM); g_animTimer = false; }
                    Invalidate();
                    break;
                case TIMER_TOAST:
                    KillTimer(hwnd, TIMER_TOAST);
                    g_toastAt = 0;
                    StartAnim();
                    break;
                case TIMER_SELF:
                    if (GetTickCount() - g_selfCheckedTick > (DWORD)g_cfg.checkHours * 3600 * 1000 || !g_selfCheckedTick) CheckSelf(false);
                    MaybeAutoApplySelf();
                    break;
                case TIMER_HEALTHY:
                    KillTimer(hwnd, TIMER_HEALTHY);
                    selfupdate::SignalHealthy(g_start.after);
                    selfupdate::FinishAfterUpdate(g_start.after);
                    break;
                case TIMER_REFRESH:
                    // drives, Recycle Bin and pending restarts change slowly; only look while you're looking
                    if (IsWindowVisible(hwnd) && !IsIconic(hwnd) && g_page == Page::Overview) {
                        sysinfo::RefreshDrives(hwnd);
                        sysinfo::RefreshHealth(hwnd);
                    }
                    break;
            }
            return 0;
        case WM_ACTIVATEAPP:
            if (wp && g_page == Page::Overview) { sysinfo::RefreshDrives(hwnd); sysinfo::RefreshHealth(hwnd); }
            return 0;
        case WM_HELM_SPECS: { std::unique_ptr<Specs> r((Specs*)lp); OverviewOnSpecs(r.get()); Invalidate(); return 0; }
        case WM_HELM_SAMPLE: { std::unique_ptr<Sample> r((Sample*)lp); OverviewOnSample(r.get()); StartAnim(); return 0; }
        case WM_HELM_DRIVES: { std::unique_ptr<DriveList> r((DriveList*)lp); OverviewOnDrives(r.get()); RepaintNow(); return 0; }
        case WM_HELM_HEALTH: { std::unique_ptr<HealthInfo> r((HealthInfo*)lp); OverviewOnHealth(r.get()); RepaintNow(); return 0; }
        case WM_HELM_CLEAN_SCAN: { std::unique_ptr<CleanScanResult> r((CleanScanResult*)lp); CleanupOnScan(r.get()); RepaintNow(); return 0; }
        case WM_HELM_CLEAN_PROGRESS: { std::unique_ptr<CleanProgress> r((CleanProgress*)lp); CleanupOnProgress(r.get()); StartAnim(); return 0; }
        case WM_HELM_CLEAN_DONE: {
            std::unique_ptr<CleanDone> r((CleanDone*)lp);
            CleanupOnDone(r.get());
            RepaintNow();
            if (g_applyWhenIdle) MaybeAutoApplySelf();
            return 0;
        }
        case WM_HELM_STORAGE: {
            std::unique_ptr<StorageProgress> r((StorageProgress*)lp);
            bool done = r->done;
            StorageOnProgress(r.get());
            if (done) { RepaintNow(); if (g_applyWhenIdle) MaybeAutoApplySelf(); }
            else StartAnim();
            return 0;
        }
        case WM_HELM_STARTUP: { std::unique_ptr<StartupList> r((StartupList*)lp); StartupOnList(r.get()); RepaintNow(); return 0; }
        case WM_HELM_ICON: { std::unique_ptr<IconResult> r((IconResult*)lp); OnIcon(r.get()); return 0; }
        case WM_HELM_TOOL: ToolsOnResult((int)wp, lp != 0); StartAnim(); return 0;
        case WM_HELM_SUITE: { std::unique_ptr<std::vector<SuiteApp>> r((std::vector<SuiteApp>*)lp); ToolsOnSuite(r.get()); RepaintNow(); return 0; }
        case WM_HELM_SELF: {
            std::unique_ptr<SelfUpdateState> r((SelfUpdateState*)lp);
            bool wasReady = g_self.kind == SelfUpdateState::Ready;
            if (!(wasReady && (r->kind == SelfUpdateState::Error || r->kind == SelfUpdateState::Checking))) g_self = *r;
            if (r->kind == SelfUpdateState::Ready && !wasReady) {
                if (!g_cfg.helmAuto) Toast(L"Helm " + r->version + L" is ready. Restart Helm to update.");
                MaybeAutoApplySelf();
            }
            Invalidate();
            return 0;
        }
        case WM_HELM_ACTIVATE:
            if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
            return 0;
        case WM_CLOSE:
            if (CleanupBusy()) {
                if (MessageBoxW(hwnd, L"Helm is still cleaning. Stop and quit?", L"Helm", MB_OKCANCEL | MB_ICONWARNING) != IDOK) return 0;
                cleanup::Cancel();
            }
            if (StorageBusy()) storage::Cancel();
            SaveWindowPlacement();
            ConfigSave();
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            sysinfo::StopMonitor();
            DiscardDevice();
            g_hwnd = nullptr;
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

HWND UiCreate(HINSTANCE inst, int show, const StartOptions& opts) {
    g_start = opts;
    QueryPerformanceFrequency(&g_qpf);
    ReadAnimationSetting();
    if (!gfx::Init()) return nullptr;
    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    wc.lpszClassName = HELM_WINDOW_CLASS;
    RegisterClassExW(&wc);

    g_page = opts.page.empty() ? Page::Overview : PageFromId(opts.page);   // a fresh start opens on the Overview
    float s = GetDpiForSystem() / 96.0f;
    int w = g_cfg.windowW > 0 ? g_cfg.windowW : (int)(1200 * s);
    int h = g_cfg.windowH > 0 ? g_cfg.windowH : (int)(800 * s);
    int x = g_cfg.windowX, y = g_cfg.windowY;
    if (x != CW_USEDEFAULT) {
        RECT r{x, y, x + w, y + h};
        if (!MonitorFromRect(&r, MONITOR_DEFAULTTONULL)) x = y = CW_USEDEFAULT;   // that monitor is gone
    }
    std::wstring title = IsElevated() ? L"Helm (Administrator)" : L"Helm";
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, title.c_str(), WS_OVERLAPPEDWINDOW, x, y, w, h, nullptr, nullptr, inst, nullptr);
    if (!hwnd) return nullptr;
    PruneSkippedVersions(HELM_VERSION);

    if (opts.after.active) {
        std::wstring text = selfupdate::TakeRestoreState();
        json::Value v;
        if (!text.empty() && json::Parser(text).Parse(v) && v.type == json::Value::Object) {
            g_page = PageFromId(v.GetStr(L"page", PageId(g_page)));
            g_scroll = g_scrollTarget = (float)v.GetNum(L"scroll", 0);
            if (v.GetBool(L"minimized", false)) show = SW_SHOWMINNOACTIVE;
            else if (v.GetBool(L"background", false)) show = SW_SHOWNOACTIVATE;
        }
        if (CompareVersions(opts.after.fromVersion, HELM_VERSION) < 0) {
            std::wstring notes;
            LoadResourceText(101, notes);
            g_whatsNew = CleanNotes(notes);
            if (g_whatsNew.empty()) g_whatsNew = L"See the release notes on GitHub.";
            g_whatsNewVersion = HELM_VERSION;
        } else {
            Toast(std::wstring(L"Rolled back to Helm ") + HELM_VERSION + L".");
        }
        Log(L"started after an update from %ls", opts.after.fromVersion.c_str());
    }
    g_cfg.page = PageId(g_page);

    // background work starts before the window shows, so the first frames already have data
    sysinfo::LoadSpecs(hwnd);
    sysinfo::StartMonitor(hwnd);
    sysinfo::RefreshDrives(hwnd);
    sysinfo::RefreshHealth(hwnd);
    StartupReload();
    if (g_cfg.autoScan || g_page == Page::Cleanup) CleanupStartScan();
    OnEnter(g_page);

    bool maximize = g_cfg.maximized && (show == SW_SHOWNORMAL || show == SW_SHOWNOACTIVATE || show == SW_SHOWDEFAULT);
    ShowWindow(hwnd, maximize ? SW_SHOWMAXIMIZED : show);
    UpdateWindow(hwnd);
    UpdateMonitor();
    if (opts.after.active) SetTimer(hwnd, TIMER_HEALTHY, 400, nullptr);
    SetTimer(hwnd, TIMER_SELF, 20 * 1000, nullptr);
    SetTimer(hwnd, TIMER_REFRESH, 30 * 1000, nullptr);
    return hwnd;
}
