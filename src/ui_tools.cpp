// Helm - Tools page: Windows repair tools and the rest of the suite
#include "ui_internal.h"
#include "tools.h"
#include "tweaks.h"
#include "util.h"
#include <thread>
#include <set>

static std::vector<SuiteApp> g_suite;
static bool g_suiteLoaded = false, g_suiteLoading = false;
static std::wstring g_hangar;
static std::set<int> g_running;            // quiet tools running now

void ToolsEnter() {
    if (g_suiteLoading) return;
    g_suiteLoading = true;
    HWND hwnd = g_hwnd;
    std::thread([hwnd] {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        auto* apps = new std::vector<SuiteApp>(tools::FindSuiteApps());
        SuiteApp hangar;
        hangar.id = L"hangar";
        hangar.exe = tools::FindHangar();
        apps->push_back(hangar);   // last entry carries Hangar's location
        CoUninitialize();
        if (!PostMessageW(hwnd, WM_HELM_SUITE, 0, (LPARAM)apps)) delete apps;
    }).detach();
}

void ToolsOnSuite(std::vector<SuiteApp>* apps) {
    g_suiteLoading = false;
    g_suiteLoaded = true;
    g_hangar.clear();
    if (!apps->empty() && apps->back().id == L"hangar") {
        g_hangar = apps->back().exe;
        apps->pop_back();
    }
    g_suite = *apps;
}

void ToolsOnResult(int index, bool ok) {
    g_running.erase(index);
    const auto& list = RepairTools();
    if (index < 0 || index >= (int)list.size()) return;
    const RepairTool& t = list[index];
    if (t.id == L"dns") Toast(ok ? L"DNS cache cleared." : L"Windows couldn't clear the DNS cache.", !ok);
    else if (t.id == L"icons") Toast(ok ? L"Icons refreshed." : L"Windows couldn't refresh the icons.", !ok);
    else if (t.id == L"store") Toast(ok ? L"Microsoft Store cache reset." : L"The Store reset didn't finish.", !ok);
    else Toast(ok ? t.name + L": done." : t.name + L" didn't finish.", !ok);
}

static void RunTool(int index) {
    const auto& list = RepairTools();
    if (index < 0 || index >= (int)list.size()) return;
    const RepairTool& t = list[index];
    if (t.console) {
        std::wstring err;
        if (tools::Run(t, &err)) Toast(t.name + L" is running in its own window.");
        else Toast(err, true);
        return;
    }
    if (g_running.count(index)) return;
    g_running.insert(index);
    HWND hwnd = g_hwnd;
    RepairTool copy = t;
    std::thread([hwnd, index, copy] {
        bool ok = tools::RunQuiet(copy);
        PostMessageW(hwnd, WM_HELM_TOOL, (WPARAM)index, ok ? 1 : 0);
    }).detach();
}

void DrawTools(float x0) {
    PageBox L = PageLayout(x0, 960);
    float left = L.left, right = L.right;
    float y = 24 - g_scroll;
    y = PageTitle(left, right, y, L"Tools", L"Windows' own repair tools, one click away, and the rest of your suite.");

    y = SectionTitle(left, right, y, L"FIX AND REPAIR");
    const auto& list = RepairTools();
    float rowH = 70;
    float h = (list.size() + 1) * rowH + 8;
    D2D1_RECT_F card = D2D1::RectF(left, y, right, y + h);
    Card(card);
    float ry = y + 4;
    for (size_t i = 0; i < list.size(); ++i, ry += rowH) {
        if (!Visible(ry, ry + rowH)) continue;
        const RepairTool& t = list[i];
        if (i) Divider(left + 18, right - 18, ry);
        g_c.FillRound(D2D1::RectF(left + 18, ry + 18, left + 52, ry + 52), 8, Alpha(g_t.accent, 0.12f));
        Glyph(t.glyph, 15, D2D1::RectF(left + 18, ry + 18, left + 52, ry + 52), g_t.accent);
        float br = right - 18 - ButtonWidth(L"Run", icon::Play) - 14;
        float nameW = MeasureWidth(t.name, Format(14, DWRITE_FONT_WEIGHT_SEMI_BOLD));
        Text(t.name, 14, D2D1::RectF(left + 66, ry + 13, br, ry + 33), g_t.text, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        float px = std::min(left + 66 + nameW + 10, br - 140);
        if (t.admin) px += Pill(px, ry + 13, L"Admin", g_t.info) + 6;
        Pill(px, ry + 13, t.duration, g_t.subtext);
        Text(t.description, 12.5f, D2D1::RectF(left + 66, ry + 35, br, ry + 55), g_t.subtext);
        bool running = g_running.count((int)i) > 0;
        ButtonRight(right - 18, ry + 19, running ? L"Running…" : L"Run", running ? 0 : icon::Play, Btn::Secondary, Hit::ToRun, (int)i, 0, !running);
    }
    // Explorer restart lives here too
    if (Visible(ry, ry + rowH)) {
        Divider(left + 18, right - 18, ry);
        g_c.FillRound(D2D1::RectF(left + 18, ry + 18, left + 52, ry + 52), 8, Alpha(g_t.accent, 0.12f));
        Glyph(icon::Folder, 15, D2D1::RectF(left + 18, ry + 18, left + 52, ry + 52), g_t.accent);
        float br = right - 18 - ButtonWidth(L"Run", icon::Play) - 14;
        Text(L"Restart File Explorer", 14, D2D1::RectF(left + 66, ry + 13, br, ry + 33), g_t.text, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        Text(L"Fixes a frozen taskbar or Start menu. Open folder windows will close.", 12.5f, D2D1::RectF(left + 66, ry + 35, br, ry + 55), g_t.subtext);
        ButtonRight(right - 18, ry + 19, L"Run", icon::Play, Btn::Secondary, Hit::ToExplorer);
    }
    y += h + 30;

    y = SectionTitle(left, right, y, L"MORE FROM YOUR SUITE");
    if (!g_suiteLoaded) {
        Text(L"Looking for your apps…", 13, D2D1::RectF(left + 2, y, right, y + 24), g_t.subtext);
        y += 40;
    } else {
        float gap = 14, minW = 280;
        int cols = std::max(1, (int)((right - left + gap) / (minW + gap)));
        float cw = (right - left - gap * (cols - 1)) / cols, ch = 156;
        for (size_t i = 0; i < g_suite.size(); ++i) {
            int c = (int)(i % cols), row = (int)(i / cols);
            D2D1_RECT_F r = D2D1::RectF(left + c * (cw + gap), y + row * (ch + gap), left + c * (cw + gap) + cw, y + row * (ch + gap) + ch);
            if (!Visible(r.top, r.bottom)) continue;
            const SuiteApp& a = g_suite[i];
            bool installed = !a.exe.empty();
            Card(r);
            ProgramIcon(installed ? a.exe : L"", D2D1::RectF(r.left + 18, r.top + 18, r.left + 50, r.top + 50), a.glyph);
            Text(a.name, 15, D2D1::RectF(r.left + 62, r.top + 16, r.right - 16, r.top + 38), g_t.text, DWRITE_FONT_WEIGHT_SEMI_BOLD);
            Text(installed ? L"Installed" : L"Not installed", 12, D2D1::RectF(r.left + 62, r.top + 36, r.right - 16, r.top + 54),
                 installed ? g_t.good : g_t.faint);
            TextWrap(a.description, 12.5f, D2D1::RectF(r.left + 18, r.top + 60, r.right - 18, r.bottom - 52), g_t.subtext);
            std::wstring label = installed ? L"Open" : (g_hangar.empty() ? L"Get it" : L"Get it in Hangar");
            float bw = ButtonWidth(label, installed ? icon::OpenNew : icon::Download);
            Button(D2D1::RectF(r.left + 18, r.bottom - 46, r.left + 18 + bw, r.bottom - 14), label, installed ? icon::OpenNew : icon::Download,
                   Btn::Secondary, Hit::ToSuite, (int)i);
        }
        int rows = ((int)g_suite.size() + cols - 1) / cols;
        y += rows * (ch + gap) + 16;
    }
    g_contentH = y + g_scroll;
}

bool ClickTools(const HotRect& h) {
    switch (h.kind) {
        case Hit::ToRun: RunTool(h.index); return true;
        case Hit::ToExplorer: tweaks::RestartExplorer(); Toast(L"Restarting File Explorer…"); return true;
        case Hit::ToSuite: {
            if (h.index < 0 || h.index >= (int)g_suite.size()) return true;
            const SuiteApp& a = g_suite[h.index];
            if (!a.exe.empty()) {
                LaunchAsUser(a.exe);
            } else if (!g_hangar.empty()) {
                LaunchAsUser(g_hangar);
                Toast(L"Hangar is opening. Find " + a.name + L" there.");
            } else {
                OpenUrl(L"https://github.com/" + a.repo + L"/releases/latest");
            }
            return true;
        }
        default: return false;
    }
}
