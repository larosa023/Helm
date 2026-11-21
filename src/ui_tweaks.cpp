// Helm - Tweaks page
#include "ui_internal.h"
#include "tweaks.h"
#include "tools.h"
#include "util.h"
#include <algorithm>

static std::vector<char> g_on;
static bool g_loaded = false;
static std::vector<tweaks::PowerPlan> g_plans;
static bool g_hasUltimate = true;
static bool g_needExplorer = false, g_needRestart = false;

static void Load() {
    const auto& all = AllTweaks();
    g_on.assign(all.size(), 0);
    for (size_t i = 0; i < all.size(); ++i) g_on[i] = tweaks::IsOn(all[i]) ? 1 : 0;
    g_plans = tweaks::PowerPlans();
    g_hasUltimate = tweaks::HasUltimatePlan();
    g_loaded = true;
}

void TweaksEnter() { Load(); }

int TweaksRecommendedOff() {
    if (!g_loaded) Load();
    const auto& all = AllTweaks();
    int n = 0;
    for (size_t i = 0; i < all.size() && i < g_on.size(); ++i) if (all[i].recommended && !g_on[i]) ++n;
    return n;
}

static void Changed(const Tweak& t) {
    if (t.after == AfterTweak::Explorer) g_needExplorer = true;
    if (t.after == AfterTweak::Restart) g_needRestart = true;
}

static void ApplyRecommended() {
    if (!g_loaded) Load();
    const auto& all = AllTweaks();
    std::vector<int> todo;
    int skippedAdmin = 0;
    for (size_t i = 0; i < all.size(); ++i) {
        if (!all[i].recommended || g_on[i]) continue;
        if (all[i].admin && !IsElevated()) { ++skippedAdmin; continue; }
        todo.push_back((int)i);
    }
    if (todo.empty()) {
        Toast(skippedAdmin ? L"The rest need administrator rights. Restart Helm as administrator to turn them on." : L"All recommended settings are already on.",
              skippedAdmin > 0);
        return;
    }
    std::wstring msg = L"Turn on " + std::to_wstring(todo.size()) + L" recommended privacy and ad settings?\n\n";
    for (int i : todo) msg += L"•  " + all[i].name + L"\n";
    msg += L"\nYou can turn any of them back off here.";
    if (skippedAdmin) msg += L" " + std::to_wstring(skippedAdmin) + L" more need administrator rights and are skipped.";
    if (MessageBoxW(g_hwnd, msg.c_str(), L"Helm", MB_OKCANCEL | MB_ICONQUESTION) != IDOK) return;
    int ok = 0;
    std::wstring firstErr;
    for (int i : todo) {
        std::wstring err;
        if (tweaks::Set(all[i], true, &err)) { ++ok; Changed(all[i]); }
        else if (firstErr.empty()) firstErr = err;
    }
    Load();
    if (!firstErr.empty()) Toast(firstErr, true);
    else Toast(std::to_wstring(ok) + (ok == 1 ? L" setting turned on." : L" settings turned on."));
}

void DrawTweaks(float x0) {
    if (!g_loaded) Load();
    PageBox L = PageLayout(x0, 960);
    float left = L.left, right = L.right;
    float y = 24 - g_scroll;
    float ty = y;
    y = PageTitle(left, right, y, L"Tweaks", L"Each switch is one change. Turn it off and Windows goes back to how it was.");
    {
        int off = TweaksRecommendedOff();
        float bx = ButtonRight(right, ty + 6, L"Turn on recommended", icon::Shield, Btn::Primary, Hit::TwRecommended, -1, 0, off > 0);
        ButtonRight(bx - 8, ty + 6, L"Restore point", icon::History, Btn::Secondary, Hit::TwRestorePoint);
    }
    if (g_needExplorer)
        y += Banner(left, right, y, icon::Refresh, g_t.info, L"Some changes show up after File Explorer restarts. Open folders will close.", L"Restart Explorer",
                    Hit::TwRestartExplorer) + 12;
    if (g_needRestart)
        y += Banner(left, right, y, icon::Power, g_t.warn, L"Restart your PC to finish a change.", L"OK", Hit::TwDismiss) + 12;
    bool anyAdmin = false;
    for (auto& t : AllTweaks()) anyAdmin |= t.admin;
    if (anyAdmin && !IsElevated())
        y += Banner(left, right, y, icon::Admin, g_t.info, L"Switches marked Admin change settings for everyone on this PC and need administrator rights.",
                    L"Restart as administrator", Hit::Elevate, -1, icon::Admin) + 12;
    y += 8;

    const auto& all = AllTweaks();
    std::wstring group;
    size_t i = 0;
    while (i < all.size()) {
        group = all[i].group;
        size_t end = i;
        while (end < all.size() && all[end].group == group) ++end;
        y = SectionTitle(left, right, y, Upper(group));
        // row heights depend on wrapped descriptions
        std::vector<float> hs;
        float total = 8;
        for (size_t k = i; k < end; ++k) {
            float dh = WrapHeight(all[k].description, 12.5f, (right - 120) - (left + 20));   // same box as drawn below
            float hh = std::max(64.0f, 34 + dh + 14);
            hs.push_back(hh);
            total += hh;
        }
        D2D1_RECT_F card = D2D1::RectF(left, y, right, y + total);
        if (Visible(card.top, card.bottom)) {
            Card(card);
            float ry = y + 4;
            for (size_t k = i; k < end; ++k) {
                const Tweak& t = all[k];
                float hh = hs[k - i];
                if (k > i) Divider(left + 18, right - 18, ry);
                if (Visible(ry, ry + hh)) {
                    bool locked = t.admin && !IsElevated();
                    float nx = left + 20;
                    float nameW = MeasureWidth(t.name, Format(14, DWRITE_FONT_WEIGHT_SEMI_BOLD));
                    Text(t.name, 14, D2D1::RectF(nx, ry + 12, right - 100, ry + 32), g_t.text, DWRITE_FONT_WEIGHT_SEMI_BOLD);
                    float px = std::min(nx + nameW + 10, right - 200);
                    if (t.admin) px += Pill(px, ry + 12, L"Admin", g_t.info) + 6;
                    if (t.after == AfterTweak::Restart) px += Pill(px, ry + 12, L"Restart", g_t.warn) + 6;
                    if (t.recommended) Pill(px, ry + 12, L"Recommended", g_t.good);
                    TextWrap(t.description, 12.5f, D2D1::RectF(nx, ry + 34, right - 120, ry + hh - 6), g_t.subtext);
                    if (locked) Glyph(icon::Lock, 12, D2D1::RectF(right - 96, ry + (hh - 20) / 2, right - 76, ry + (hh + 20) / 2), g_t.faint);
                    Toggle(right - 64, ry + (hh - 22) / 2, k < g_on.size() && g_on[k], Hit::TwToggle, (int)k, 0, !locked);
                }
                ry += hh;
            }
        }
        y += total + 26;
        i = end;
    }

    // power plans
    y = SectionTitle(left, right, y, L"POWER PLAN");
    {
        size_t rows = g_plans.size() + (g_hasUltimate ? 0 : 1);
        float rowH = 52;
        float h = std::max<size_t>(1, rows) * rowH + 8;
        D2D1_RECT_F card = D2D1::RectF(left, y, right, y + h);
        Card(card);
        float ry = y + 4;
        if (rows == 0) Text(L"Windows didn't list any power plans.", 13, D2D1::RectF(left + 20, ry, right, ry + rowH), g_t.subtext);
        for (size_t k = 0; k < g_plans.size(); ++k, ry += rowH) {
            const auto& p = g_plans[k];
            if (k) Divider(left + 18, right - 18, ry);
            bool hot = IsHover(Hit::TwPower, (int)k);
            D2D1_RECT_F row = D2D1::RectF(left + 4, ry + 2, right - 4, ry + rowH - 2);
            if (hot && !p.active) g_c.FillRound(row, 8, g_t.surfaceHover);
            float cx = left + 30, cy = ry + rowH / 2;
            g_c.Circle(cx, cy, 9, p.active ? g_t.accent : Alpha(g_t.text, 0.35f));
            g_c.Circle(cx, cy, p.active ? 4 : 7.5f, p.active ? g_t.onAccent : g_t.surface);
            Text(p.name, 14, D2D1::RectF(left + 52, ry, right - 160, ry + rowH), g_t.text, p.active ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL);
            if (p.active) Pill(right - 20, ry + (rowH - 20) / 2, L"In use", g_t.good, true);
            AddHot(row, Hit::TwPower, (int)k, 0, !p.active);
        }
        if (!g_hasUltimate) {
            if (!g_plans.empty()) Divider(left + 18, right - 18, ry);
            Text(L"Ultimate Performance", 14, D2D1::RectF(left + 52, ry + 6, right - 180, ry + 26), g_t.text);
            Text(L"Never lets the CPU idle down. Uses more power.", 12, D2D1::RectF(left + 52, ry + 26, right - 180, ry + 44), g_t.subtext);
            ButtonRight(right - 18, ry + 10, L"Add it", 0, Btn::Secondary, Hit::TwAddUltimate);
        }
        y += h + 30;
    }
    g_contentH = y + g_scroll;
}

bool ClickTweaks(const HotRect& h) {
    switch (h.kind) {
        case Hit::TwToggle: {
            const auto& all = AllTweaks();
            if (h.index < 0 || h.index >= (int)all.size() || h.index >= (int)g_on.size()) return true;
            const Tweak& t = all[h.index];
            bool want = !g_on[h.index];
            std::wstring err;
            if (tweaks::Set(t, want, &err)) {
                Changed(t);
                g_on[h.index] = tweaks::IsOn(t) ? 1 : 0;
                if ((bool)g_on[h.index] != want) Toast(L"Windows changed \"" + t.name + L"\" back. A policy may be managing it.", true);
            } else {
                Toast(err, true);
            }
            return true;
        }
        case Hit::TwRecommended: ApplyRecommended(); return true;
        case Hit::TwRestorePoint:
            for (auto& tool : RepairTools()) {
                if (tool.id != L"restore") continue;
                std::wstring err;
                if (!tools::Run(tool, &err)) Toast(err, true);
            }
            return true;
        case Hit::TwRestartExplorer:
            tweaks::RestartExplorer();
            Toast(L"Restarting File Explorer…");
            g_needExplorer = false;
            return true;
        case Hit::TwDismiss: g_needRestart = false; return true;
        case Hit::TwPower:
            if (h.index >= 0 && h.index < (int)g_plans.size()) {
                std::wstring err;
                if (tweaks::SetPowerPlan(g_plans[h.index].guid, &err)) Toast(L"Switched to " + g_plans[h.index].name + L".");
                else Toast(err, true);
                Load();
            }
            return true;
        case Hit::TwAddUltimate: {
            std::wstring err;
            if (tweaks::AddUltimatePlan(&err)) Toast(L"Added Ultimate Performance. Pick it above to use it.");
            else Toast(err, true);
            Load();
            return true;
        }
        default: return false;
    }
}
