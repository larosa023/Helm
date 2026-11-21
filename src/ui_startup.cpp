// Helm - Startup apps page
#include "ui_internal.h"
#include "startup.h"
#include "util.h"
#include <algorithm>

static StartupList g_list;
static bool g_loaded = false;
static bool g_loading = false;

void StartupReload() {
    if (g_loading) return;
    g_loading = true;
    startup::Load(g_hwnd);
}

void StartupOnList(StartupList* l) {
    g_loading = false;
    g_list = *l;
    g_loaded = true;
}

bool StartupCounts(int* enabled, int* total) {
    if (!g_loaded) return false;
    int e = 0;
    for (auto& x : g_list.entries) if (x.enabled) ++e;
    *enabled = e;
    *total = (int)g_list.entries.size();
    return true;
}

void DrawStartup(float x0) {
    PageBox L = PageLayout(x0, 960);
    float left = L.left, right = L.right;
    float y = 24 - g_scroll;
    int en = 0, total = 0;
    StartupCounts(&en, &total);
    std::wstring sub = !g_loaded ? L"Reading…" : total == 0 ? L"Nothing starts with Windows." :
                       std::to_wstring(en) + L" of " + std::to_wstring(total) + L" start when you sign in";
    float ty = y;
    y = PageTitle(left, right, y, L"Startup apps", sub);
    ButtonRight(right, ty + 6, L"Refresh", icon::Refresh, Btn::Secondary, Hit::SuRefresh, -1, 0, !g_loading);

    bool anyAllUsers = false;
    for (auto& e : g_list.entries) anyAllUsers |= e.allUsers;
    if (anyAllUsers && !IsElevated()) {
        y += Banner(left, right, y, icon::Admin, g_t.info, L"Apps set up for everyone on this PC need administrator rights to turn off.",
                    L"Restart as administrator", Hit::Elevate, -1, icon::Admin) + 14;
    }
    TextWrap(L"Turning an app off here only stops it starting by itself. It stays installed, and you can turn it back on any time. Task Manager shows the same switches.",
             12.5f, D2D1::RectF(left + 2, y, right, y + 40), g_t.subtext);
    y += WrapHeight(L"Turning an app off here only stops it starting by itself. It stays installed, and you can turn it back on any time. Task Manager shows the same switches.",
                    12.5f, right - left - 2) + 16;

    float rowH = 64;
    size_t n = g_list.entries.size();
    float h = std::max(1.0f, (float)n) * rowH + 8;
    D2D1_RECT_F card = D2D1::RectF(left, y, right, y + h);
    Card(card);
    if (n == 0) Text(g_loaded ? L"No apps start with Windows." : L"Reading…", 13, D2D1::RectF(left + 20, y, right - 20, y + h), g_t.subtext);
    float ry = y + 4;
    for (size_t i = 0; i < n; ++i, ry += rowH) {
        if (!Visible(ry, ry + rowH)) continue;
        const StartupEntry& e = g_list.entries[i];
        if (i) Divider(left + 18, right - 18, ry);
        bool locked = e.allUsers && !IsElevated();
        bool rowHot = IsHover(Hit::SuToggle, (int)i) || IsHover(Hit::SuOpen, (int)i);
        float hv = Anim(AKey(5000, (int)i), rowHot ? 1.0f : 0.0f, 0.3f);
        if (hv > 0.01f) g_c.FillRound(D2D1::RectF(left + 4, ry + 2, right - 4, ry + rowH - 2), 8, Alpha(g_t.surfaceHover, hv));
        ProgramIcon(e.exe, D2D1::RectF(left + 18, ry + 16, left + 50, ry + 48), icon::Apps);
        float tr = right - 230;
        Text(e.name, 14, D2D1::RectF(left + 64, ry + 12, tr, ry + 32), e.enabled ? g_t.text : g_t.subtext, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        std::wstring detail = e.publisher;
        if (!e.description.empty() && !IEquals(e.description, e.name)) detail += (detail.empty() ? L"" : L" · ") + e.description;
        if (detail.empty()) detail = e.command;
        Text(detail, 12, D2D1::RectF(left + 64, ry + 32, tr, ry + 50), g_t.subtext);
        float px = right - 76;
        Text(e.enabled ? L"On" : L"Off", 12.5f, D2D1::RectF(px - 34, ry, px - 4, ry + rowH), g_t.subtext, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING);
        px -= 40;
        if (locked) {
            Glyph(icon::Lock, 12, D2D1::RectF(px - 18, ry + 22, px, ry + 42), g_t.faint);
            px -= 24;
        }
        if (e.missing) px -= Pill(px, ry + 22, L"Program missing", g_t.warn, true) + 8;
        Toggle(right - 64, ry + 21, e.enabled, Hit::SuToggle, (int)i, 0, !locked);
        bool canOpen = !e.missing && e.exe.find(L'\\') != std::wstring::npos;
        D2D1_RECT_F ob = D2D1::RectF(px - 34, ry + 16, px - 2, ry + 48);
        if (rowHot && canOpen) Button(ob, L"", icon::Folder, Btn::Subtle, Hit::SuOpen, (int)i);
        else if (canOpen) AddHot(ob, Hit::SuOpen, (int)i);
    }
    y += h + 30;
    g_contentH = y + g_scroll;
}

bool ClickStartup(const HotRect& h) {
    switch (h.kind) {
        case Hit::SuRefresh: StartupReload(); return true;
        case Hit::SuToggle: {
            if (h.index < 0 || h.index >= (int)g_list.entries.size()) return true;
            StartupEntry& e = g_list.entries[h.index];
            std::wstring err;
            if (startup::SetEnabled(e, !e.enabled, &err)) {
                e.enabled = !e.enabled;
                Toast(e.name + (e.enabled ? L" will start with Windows." : L" won't start with Windows any more."));
            } else {
                Toast(err, true);
            }
            return true;
        }
        case Hit::SuOpen:
            if (h.index >= 0 && h.index < (int)g_list.entries.size()) ShowInFolder(g_list.entries[h.index].exe);
            return true;
        default: return false;
    }
}
