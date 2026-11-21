// Helm - Settings page
#include "ui_internal.h"
#include "config.h"
#include "themes.h"
#include "util.h"

static const int kHours[] = {3, 6, 12, 24};

const std::vector<ThemeDef>& CustomThemes();   // ui_themes.cpp

void SettingsEnter() { g_prevVersions = selfupdate::PreviousVersions(); }

void DrawSettings(float x0) {
    PageBox L = PageLayout(x0, 820);
    float left = L.left, right = L.right;
    float y = 24 - g_scroll;
    y = PageTitle(left, right, y, L"Settings", L"");
    auto section = [&](const std::wstring& title, float h) {
        y = SectionTitle(left, right, y, title);
        D2D1_RECT_F r = D2D1::RectF(left, y, right, y + h);
        Card(r);
        return r;
    };
    auto rowLabel = [&](float ry, const std::wstring& t, const std::wstring& sub) {
        Text(t, 14, D2D1::RectF(left + 18, ry + 8, right - 330, ry + 28), g_t.text);
        if (!sub.empty()) Text(sub, 12, D2D1::RectF(left + 18, ry + 28, right - 330, ry + 46), g_t.subtext);
    };

    D2D1_RECT_F s1 = section(L"APPEARANCE", 64);
    {
        ThemeDef cur = FindTheme(g_cfg.theme, CustomThemes());
        rowLabel(s1.top + 4, L"Theme", cur.name + L" · " + cur.description);
        ButtonRight(right - 18, s1.top + 16, L"Browse themes", icon::Color, Btn::Secondary, Hit::ThemeBrowse);
    }
    y = s1.bottom + 26;

    D2D1_RECT_F s2 = section(L"CLEANUP", 64);
    rowLabel(s2.top + 4, L"Look for junk when Helm starts", L"Takes a few seconds in the background. Nothing is removed until you say so.");
    Toggle(right - 64, s2.top + 21, g_cfg.autoScan, Hit::SetAutoScan, -1);
    y = s2.bottom + 26;

    D2D1_RECT_F s3 = section(L"ADMINISTRATOR", 64);
    if (IsElevated()) {
        rowLabel(s3.top + 4, L"Running as administrator", L"Everything Helm can do is available.");
        Pill(right - 18, s3.top + 22, L"On", g_t.good, true);
    } else {
        rowLabel(s3.top + 4, L"Running without administrator rights", L"Some cleanup, tweaks and startup apps need them. Windows will ask first.");
        ButtonRight(right - 18, s3.top + 16, L"Restart as administrator", icon::Admin, Btn::Secondary, Hit::Elevate);
    }
    y = s3.bottom + 26;

    D2D1_RECT_F s5 = section(L"HELM UPDATES", 64 * 4 + (g_prevVersions.empty() ? 0 : 64));
    {
        std::wstring status;
        switch (g_self.kind) {
            case SelfUpdateState::Checking: status = L"Checking…"; break;
            case SelfUpdateState::UpToDate: status = std::wstring(L"Up to date"); break;
            case SelfUpdateState::Downloading: status = L"Downloading " + g_self.version + (g_self.percent >= 0 ? L" · " + std::to_wstring(g_self.percent) + L"%" : L""); break;
            case SelfUpdateState::Ready: status = L"Helm " + g_self.version + L" is downloaded and verified"; break;
            case SelfUpdateState::Error: status = g_self.message; break;
            default: status = L"Checks GitHub in the background"; break;
        }
        rowLabel(s5.top + 4, std::wstring(L"Helm ") + HELM_VERSION, status);
        if (g_self.kind == SelfUpdateState::Ready)
            ButtonRight(right - 18, s5.top + 16, L"Restart to update", icon::Update, Btn::Primary, Hit::SelfRestart);
        else
            ButtonRight(right - 18, s5.top + 16, L"Check now", icon::Refresh, Btn::Secondary, Hit::SelfCheck, -1, 0,
                        g_self.kind != SelfUpdateState::Checking && g_self.kind != SelfUpdateState::Downloading);
        Divider(left + 18, right - 18, s5.top + 64);
        rowLabel(s5.top + 68, L"Channel", L"Beta gets new features first");
        Segmented(right - 18, s5.top + 80, {L"Stable", L"Beta"}, g_cfg.helmBeta ? 1 : 0, Hit::SelfChannel, true);
        Divider(left + 18, right - 18, s5.top + 128);
        rowLabel(s5.top + 132, L"Install updates automatically", L"Only when Helm isn't busy and you're not using it");
        Toggle(right - 64, s5.top + 149, g_cfg.helmAuto, Hit::SelfAuto, -1);
        Divider(left + 18, right - 18, s5.top + 192);
        rowLabel(s5.top + 196, L"Check for updates every", L"");
        int hi = 1;
        for (int k = 0; k < 4; ++k) if (kHours[k] == g_cfg.checkHours) hi = k;
        Segmented(right - 18, s5.top + 208, {L"3 h", L"6 h", L"12 h", L"24 h"}, hi, Hit::SetHours, true);
        if (!g_prevVersions.empty()) {
            Divider(left + 18, right - 18, s5.top + 256);
            rowLabel(s5.top + 260, L"Previous versions", L"Go back if a new Helm gives you trouble");
            float bx = right - 18;
            for (size_t k = 0; k < g_prevVersions.size() && k < 2; ++k)
                bx = ButtonRight(bx, s5.top + 272, L"Roll back to " + g_prevVersions[k].version, 0, Btn::Secondary, Hit::SelfRollback, -1, (int)k) - 8;
        }
    }
    y = s5.bottom + 26;

    D2D1_RECT_F s6 = section(L"ADVANCED", 128);
    rowLabel(s6.top + 4, L"Data folder", DataDir());
    ButtonRight(right - 18, s6.top + 16, L"Open", icon::Folder, Btn::Secondary, Hit::SetOpenData);
    Divider(left + 18, right - 18, s6.top + 64);
    rowLabel(s6.top + 68, L"Log", L"helm.log, for troubleshooting");
    ButtonRight(right - 18, s6.top + 80, L"Open log", 0, Btn::Secondary, Hit::SetOpenLog);
    y = s6.bottom + 26;

    D2D1_RECT_F s7 = section(L"ABOUT", 64);
    rowLabel(s7.top + 4, std::wstring(L"Helm ") + HELM_VERSION + (IsPortable() ? L" (portable)" : L""), L"Keeps your PC fast, clean and private · larosa023");
    ButtonRight(right - 18, s7.top + 16, L"github.com/larosa023/Helm", 0, Btn::Subtle, Hit::SetRepo);
    y = s7.bottom + 30;
    g_contentH = y + g_scroll;
}

bool ClickSettings(const HotRect& h) {
    switch (h.kind) {
        case Hit::SetAutoScan: g_cfg.autoScan = !g_cfg.autoScan; ConfigSave(); return true;
        case Hit::SelfCheck: CheckSelf(true); return true;
        case Hit::SelfChannel: g_cfg.helmBeta = h.arg == 1; ConfigSave(); CheckSelf(true); return true;
        case Hit::SelfAuto: g_cfg.helmAuto = !g_cfg.helmAuto; ConfigSave(); return true;
        case Hit::SetHours: if (h.arg >= 0 && h.arg < 4) { g_cfg.checkHours = kHours[h.arg]; ConfigSave(); } return true;
        case Hit::SelfRollback: {
            if (h.arg < 0 || h.arg >= (int)g_prevVersions.size()) return true;
            auto prev = g_prevVersions[h.arg];
            std::wstring msg = L"Go back to Helm " + prev.version + L"?\n\nHelm restarts and won't install " + HELM_VERSION +
                               L" again automatically. Your settings aren't touched.";
            if (MessageBoxW(g_hwnd, msg.c_str(), L"Roll back Helm", MB_OKCANCEL | MB_ICONQUESTION) != IDOK) return true;
            SkipVersion(HELM_VERSION);
            ConfigSave();
            ApplySelf(prev.exe, prev.version, true);
            return true;
        }
        case Hit::SetOpenData: OpenFolder(DataDir()); return true;
        case Hit::SetOpenLog: LaunchAsUser(LogPath()); return true;
        case Hit::SetRepo: OpenUrl(L"https://github.com/larosa023/Helm"); return true;
        default: return false;
    }
}
