// Helm - repair tools and suite apps
#include "tools.h"
#include "util.h"
#include "gfx.h"
#include "json.h"
#include <shlobj.h>
#include <shobjidl.h>
#include <tlhelp32.h>

static std::wstring SystemDrive() {
    wchar_t b[MAX_PATH];
    UINT n = GetWindowsDirectoryW(b, MAX_PATH);
    return (n >= 2 && n < MAX_PATH) ? std::wstring(b, 2) : L"C:";
}

const std::vector<RepairTool>& RepairTools() {
    static std::vector<RepairTool> list;
    if (!list.empty()) return list;
    using namespace gfx;
    auto add = [&](const wchar_t* id, const wchar_t* name, const std::wstring& desc, const wchar_t* dur, wchar_t glyph, bool admin, bool console,
                   const std::wstring& cmd) {
        RepairTool t;
        t.id = id; t.name = name; t.description = desc; t.duration = dur; t.glyph = glyph; t.admin = admin; t.console = console; t.command = cmd;
        list.push_back(t);
    };
    add(L"restore", L"Create a restore point", L"A snapshot of system settings you can go back to. Good to have before big changes.",
        L"About a minute", icon::History, true, true,
        L"powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \"Checkpoint-Computer -Description 'Helm' -RestorePointType MODIFY_SETTINGS; "
        L"if ($?) { Write-Host 'Restore point created.' }\"");
    add(L"sfc", L"Check system files", L"Finds and repairs damaged or missing Windows files (sfc /scannow).",
        L"10 to 20 minutes", icon::Shield, true, true, L"sfc /scannow");
    add(L"dism", L"Repair the Windows image", L"Fixes the store Windows repairs itself from. Run this if \"Check system files\" can't fix something.",
        L"10 to 30 minutes", icon::Repair, true, true, L"DISM /Online /Cleanup-Image /RestoreHealth");
    add(L"winsxs", L"Shrink the component store", L"Removes old versions of Windows components that updates replaced. Frees space in WinSxS.",
        L"5 to 15 minutes", icon::Broom, true, true, L"DISM /Online /Cleanup-Image /StartComponentCleanup");
    add(L"chkdsk", L"Check the system drive for errors", L"Scans " + SystemDrive() + L" for file system problems while you keep working.",
        L"A few minutes", icon::Drive, true, true, L"chkdsk " + SystemDrive() + L" /scan");
    add(L"network", L"Reset networking", L"Fixes most \"connected but no internet\" problems. Restart your PC afterwards.",
        L"Under a minute", icon::Network, true, true, L"ipconfig /flushdns & netsh winsock reset & netsh int ip reset");
    add(L"dns", L"Flush the DNS cache", L"Forgets saved website addresses. Helps when one site won't load.",
        L"A second", icon::Globe, false, false, L"ipconfig /flushdns");
    add(L"icons", L"Refresh icons", L"Fixes blank or wrong icons on the desktop and in Explorer.",
        L"A second", icon::Apps, false, false, L"ie4uinit.exe -show");
    add(L"store", L"Reset the Microsoft Store cache", L"Fixes Store downloads that are stuck. The Store opens when it's done.",
        L"Under a minute", icon::Download, false, false, L"wsreset.exe");
    return list;
}

namespace tools {

static std::wstring Cmd() {
    wchar_t b[MAX_PATH];
    UINT n = GetSystemDirectoryW(b, MAX_PATH);
    return (n && n < MAX_PATH) ? std::wstring(b) + L"\\cmd.exe" : L"cmd.exe";
}

bool Run(const RepairTool& t, std::wstring* error) {
    if (!t.console) {
        // quiet tools are run from a worker thread with RunQuiet
        return false;
    }
    std::wstring inner = L"title Helm - " + t.name + L" & echo " + t.name + L" & echo. & " + t.command +
                         L" & echo. & echo Finished. You can close this window. & pause >nul";
    std::wstring args = L"/d /c \"" + inner + L"\"";
    bool ok;
    if (t.admin && !IsElevated()) ok = ShellRunAs(Cmd(), args, true);
    else {
        std::wstring cmdline = L"\"" + Cmd() + L"\" " + args;
        std::vector<wchar_t> buf(cmdline.begin(), cmdline.end());
        buf.push_back(0);
        STARTUPINFOW si{sizeof(si)};
        PROCESS_INFORMATION pi{};
        ok = CreateProcessW(Cmd().c_str(), buf.data(), nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE, nullptr, nullptr, &si, &pi) != 0;
        if (ok) { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
    }
    if (!ok && error) *error = t.admin ? L"It needs administrator rights, and Windows didn't give them." : L"Couldn't start it.";
    Log(L"tool %ls: %ls", t.id.c_str(), ok ? L"started" : L"didn't start");
    return ok;
}

bool RunQuiet(const RepairTool& t) {
    DWORD code = 1;
    bool finished = RunAndWait(Cmd(), L"/d /c \"" + t.command + L"\"", 120000, &code, true);
    Log(L"tool %ls: exit %lu", t.id.c_str(), code);
    return finished && code == 0;
}

// ---------------------------------------------------------------- the suite
static std::wstring Local() { return KnownFolder(FOLDERID_LocalAppData); }

// Hangar remembers where it found each app (DataDir of Hangar\locations.json)
static std::wstring HangarLocation(const std::wstring& id) {
    std::wstring text;
    if (!ReadTextFile(Local() + L"\\Hangar\\locations.json", text)) return L"";
    json::Value v;
    if (!json::Parser(text).Parse(v) || v.type != json::Value::Object) return L"";
    std::wstring p = v.GetStr(id);
    return FileExists(p) ? p : L"";
}

static std::wstring RunningPath(const std::wstring& exeName) {
    std::wstring found;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return found;
    PROCESSENTRY32W pe{sizeof(pe)};
    if (Process32FirstW(snap, &pe)) {
        do {
            if (!IEquals(pe.szExeFile, exeName)) continue;
            if (HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID)) {
                wchar_t buf[MAX_PATH * 2];
                DWORD n = MAX_PATH * 2;
                if (QueryFullProcessImageNameW(h, 0, buf, &n)) found = std::wstring(buf, n);
                CloseHandle(h);
            }
        } while (found.empty() && Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

static std::wstring ShortcutTarget(const std::wstring& lnk) {
    if (!FileExists(lnk)) return L"";
    std::wstring out;
    IShellLinkW* link = nullptr;
    IPersistFile* pf = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&link)) &&
        SUCCEEDED(link->QueryInterface(IID_IPersistFile, (void**)&pf)) && SUCCEEDED(pf->Load(lnk.c_str(), STGM_READ))) {
        wchar_t t[MAX_PATH * 2] = {};
        if (SUCCEEDED(link->GetPath(t, MAX_PATH * 2, nullptr, 0)) && FileExists(t)) out = t;
    }
    if (pf) pf->Release();
    if (link) link->Release();
    return out;
}

static std::wstring Locate(const std::wstring& id, const std::wstring& name, bool velopack) {
    std::wstring exe = name + L".exe";
    std::wstring p = velopack ? Local() + L"\\" + name + L"\\current\\" + exe : Local() + L"\\Programs\\" + name + L"\\" + exe;
    if (FileExists(p)) return p;
    if (!(p = HangarLocation(id)).empty()) return p;
    if (!(p = ShortcutTarget(KnownFolder(FOLDERID_Programs) + L"\\" + name + L".lnk")).empty() && IContains(PathName(p), name)) return p;
    return RunningPath(exe);
}

std::wstring FindHangar() {
    std::wstring p = Local() + L"\\Programs\\Hangar\\Hangar.exe";
    if (FileExists(p)) return p;
    if (!(p = ShortcutTarget(KnownFolder(FOLDERID_Programs) + L"\\Hangar.lnk")).empty()) return p;
    if (!(p = ShortcutTarget(KnownFolder(FOLDERID_Desktop) + L"\\Hangar.lnk")).empty()) return p;
    return RunningPath(L"Hangar.exe");
}

std::vector<SuiteApp> FindSuiteApps() {
    using namespace gfx;
    struct Def { const wchar_t *id, *name, *desc; wchar_t glyph; bool velopack; };
    static const Def defs[] = {
        {L"ember", L"Ember", L"See what's running and end anything that's stuck. A clean, light Task Manager.", icon::Gauge, true},
        {L"restock", L"Restock", L"Update every other app on your PC in one click.", icon::Update, false},
        {L"pitstop", L"Pitstop", L"Install, clean-install and roll back NVIDIA drivers.", icon::Game, false},
        {L"rigcard", L"Rigcard", L"Benchmarks, temperatures and your build on one shareable card.", icon::Diagnostic, false},
        {L"breaker", L"Breaker", L"Every Windows setting and Control Panel page in one fast app.", icon::Settings, false},
        {L"reticle", L"Reticle", L"An FPS counter and crosshair overlay for games.", icon::Monitor, false},
    };
    std::vector<SuiteApp> out;
    for (auto& d : defs) {
        SuiteApp a;
        a.id = d.id; a.name = d.name; a.description = d.desc; a.glyph = d.glyph;
        a.repo = std::wstring(L"larosa023/") + d.name;
        a.exe = Locate(d.id, d.name, d.velopack);
        out.push_back(a);
    }
    return out;
}

} // namespace tools
