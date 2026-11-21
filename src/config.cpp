// Helm - settings
#include "config.h"
#include "util.h"
#include "json.h"
#include <algorithm>

Config g_cfg;

static std::wstring ConfigPath() { return DataDir() + L"\\settings.json"; }

void ConfigLoad() {
    std::wstring text;
    if (!ReadTextFile(ConfigPath(), text)) return;
    json::Value v;
    if (!json::Parser(text).Parse(v) || v.type != json::Value::Object) { Log(L"settings.json is damaged; using defaults"); return; }
    Config c;
    c.theme = v.GetStr(L"theme", c.theme);
    c.accent = v.GetStr(L"accent", c.accent);
    c.page = v.GetStr(L"page", c.page);
    c.helmBeta = v.GetBool(L"helmBeta", c.helmBeta);
    c.helmAuto = v.GetBool(L"helmAuto", c.helmAuto);
    c.checkHours = std::clamp((int)v.GetNum(L"checkHours", c.checkHours), 1, 48);
    c.skipVersion = v.GetStr(L"skipVersion");
    c.totalFreed = std::max(0.0, v.GetNum(L"totalFreed", 0));
    c.lastCleanAt = (long long)v.GetNum(L"lastCleanAt", 0);
    c.autoScan = v.GetBool(L"autoScan", c.autoScan);
    if (const json::Value* w = v.Get(L"window"); w && w->type == json::Value::Object) {
        c.windowX = (int)w->GetNum(L"x", CW_USEDEFAULT);
        c.windowY = (int)w->GetNum(L"y", CW_USEDEFAULT);
        c.windowW = (int)w->GetNum(L"w", 0);
        c.windowH = (int)w->GetNum(L"h", 0);
        c.maximized = w->GetBool(L"maximized", false);
    }
    if (const json::Value* p = v.Get(L"cleanPick"); p && p->type == json::Value::Object)
        for (auto& kv : p->obj) if (kv.second.type == json::Value::Bool) c.cleanPick[kv.first] = kv.second.b;
    g_cfg = c;
}

void ConfigSave() {
    json::Value v = json::Value::MakeObject();
    v.Set(L"theme", json::Value::Str(g_cfg.theme));
    v.Set(L"accent", json::Value::Str(g_cfg.accent));
    v.Set(L"page", json::Value::Str(g_cfg.page));
    v.Set(L"helmBeta", json::Value::Boolean(g_cfg.helmBeta));
    v.Set(L"helmAuto", json::Value::Boolean(g_cfg.helmAuto));
    v.Set(L"checkHours", json::Value::Num(g_cfg.checkHours));
    v.Set(L"skipVersion", json::Value::Str(g_cfg.skipVersion));
    v.Set(L"totalFreed", json::Value::Num(g_cfg.totalFreed));
    v.Set(L"lastCleanAt", json::Value::Num((double)g_cfg.lastCleanAt));
    v.Set(L"autoScan", json::Value::Boolean(g_cfg.autoScan));
    json::Value w = json::Value::MakeObject();
    w.Set(L"x", json::Value::Num(g_cfg.windowX));
    w.Set(L"y", json::Value::Num(g_cfg.windowY));
    w.Set(L"w", json::Value::Num(g_cfg.windowW));
    w.Set(L"h", json::Value::Num(g_cfg.windowH));
    w.Set(L"maximized", json::Value::Boolean(g_cfg.maximized));
    v.Set(L"window", w);
    json::Value p = json::Value::MakeObject();
    for (auto& kv : g_cfg.cleanPick) p.Set(kv.first, json::Value::Boolean(kv.second));
    v.Set(L"cleanPick", p);
    std::wstring out;
    json::Write(v, out);
    if (!WriteTextFileAtomic(ConfigPath(), out)) Log(L"couldn't save settings.json");
}

static std::vector<std::wstring> SkipList() {
    std::vector<std::wstring> out;
    size_t i = 0;
    const std::wstring& s = g_cfg.skipVersion;
    while (i < s.size()) {
        size_t e = s.find(L',', i);
        if (e == std::wstring::npos) e = s.size();
        std::wstring v = Trim(s.substr(i, e - i));
        if (!v.empty()) out.push_back(v);
        i = e + 1;
    }
    return out;
}

static void SetSkipList(const std::vector<std::wstring>& list) {
    g_cfg.skipVersion.clear();
    for (auto& v : list) g_cfg.skipVersion += (g_cfg.skipVersion.empty() ? L"" : L",") + v;
}

bool IsSkippedVersion(const std::wstring& v) {
    for (auto& s : SkipList()) if (CompareVersions(s, v) == 0) return true;
    return false;
}

void SkipVersion(const std::wstring& v) {
    if (IsSkippedVersion(v)) return;
    auto list = SkipList();
    list.push_back(NormalizeVersion(v));
    SetSkipList(list);
}

void PruneSkippedVersions(const std::wstring& current) {
    auto list = SkipList();
    std::vector<std::wstring> keep;
    for (auto& v : list) if (CompareVersions(v, current) > 0) keep.push_back(v);
    SetSkipList(keep);
}
