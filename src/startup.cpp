// Helm - startup apps
#include "startup.h"
#include "util.h"
#include <shlobj.h>
#include <shobjidl.h>
#include <thread>
#include <algorithm>

namespace startup {

static const wchar_t* kRun = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* kRun32 = L"Software\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* kApproved = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\";

struct Source {
    HKEY root;              // where the entry and its StartupApproved value live
    const wchar_t* runKey;  // registry sources
    const wchar_t* approved;// subkey under StartupApproved
    bool folder;            // a Startup folder instead of a Run key
    const wchar_t* label;
};

static const Source kSources[] = {
    {HKEY_CURRENT_USER, kRun, L"Run", false, L"Registry"},
    {HKEY_LOCAL_MACHINE, kRun, L"Run", false, L"Registry (all users)"},
    {HKEY_LOCAL_MACHINE, kRun32, L"Run32", false, L"Registry (all users, 32-bit)"},
    {HKEY_CURRENT_USER, nullptr, L"StartupFolder", true, L"Startup folder"},
    {HKEY_LOCAL_MACHINE, nullptr, L"StartupFolder", true, L"Startup folder (all users)"},
};

// StartupApproved: 12 bytes; an odd first byte means "disabled" (Task Manager writes 03 + the time)
static bool ReadApproved(const Source& s, const std::wstring& name) {
    BYTE b[64];
    DWORD size = sizeof(b), type = 0;
    std::wstring key = std::wstring(kApproved) + s.approved;
    if (RegGetValueW(s.root, key.c_str(), name.c_str(), RRF_RT_REG_BINARY | RRF_SUBKEY_WOW6464KEY, &type, b, &size) != ERROR_SUCCESS || size < 1)
        return true;
    return (b[0] & 1) == 0;
}

static std::wstring ExpandQuoted(const std::wstring& cmd0, std::wstring& exe) {
    std::wstring cmd = ExpandEnv(Trim(cmd0));
    exe.clear();
    if (cmd.empty()) return cmd;
    if (cmd[0] == L'"') {
        size_t e = cmd.find(L'"', 1);
        exe = cmd.substr(1, e == std::wstring::npos ? std::wstring::npos : e - 1);
    } else {
        // unquoted: try longer and longer prefixes ("C:\Program Files\App\app.exe -min")
        size_t pos = 0;
        while (true) {
            size_t sp = cmd.find(L' ', pos);
            std::wstring cand = cmd.substr(0, sp);
            if (FileExists(cand)) { exe = cand; break; }
            if (!IContains(cand, L".") && FileExists(cand + L".exe")) { exe = cand + L".exe"; break; }
            if (sp == std::wstring::npos) break;
            pos = sp + 1;
        }
        if (exe.empty()) {
            size_t sp = cmd.find(L' ');
            exe = cmd.substr(0, sp);
        }
    }
    // a bare name like "rundll32.exe" or "ctfmon": look it up on the search path
    if (!exe.empty() && exe.find(L'\\') == std::wstring::npos) {
        wchar_t found[MAX_PATH];
        if (SearchPathW(nullptr, exe.c_str(), L".exe", MAX_PATH, found, nullptr)) exe = found;
    }
    return cmd;
}

static void Describe(StartupEntry& e) {
    if (e.exe.empty() || !FileExists(e.exe)) { e.missing = !e.exe.empty() && e.exe.find(L'\\') != std::wstring::npos; return; }
    std::wstring company, product, desc;
    ExeStrings(e.exe, company, product, desc);
    e.publisher = company;
    e.description = !desc.empty() ? desc : product;
}

static void ReadRunKey(int sourceId, std::vector<StartupEntry>& out) {
    const Source& s = kSources[sourceId];
    HKEY h;
    if (RegOpenKeyExW(s.root, s.runKey, 0, KEY_READ | KEY_WOW64_64KEY, &h) != ERROR_SUCCESS) return;
    for (DWORD i = 0; i < 512; ++i) {
        wchar_t name[512];
        DWORD nameLen = 512, type = 0, dataLen = 0;
        LONG r = RegEnumValueW(h, i, name, &nameLen, nullptr, &type, nullptr, &dataLen);
        if (r == ERROR_NO_MORE_ITEMS) break;
        if (r != ERROR_SUCCESS && r != ERROR_MORE_DATA) continue;
        if (type != REG_SZ && type != REG_EXPAND_SZ) continue;
        std::vector<wchar_t> data(dataLen / sizeof(wchar_t) + 2, 0);
        nameLen = 512;
        DWORD dl = (DWORD)(data.size() * sizeof(wchar_t));
        if (RegEnumValueW(h, i, name, &nameLen, nullptr, &type, (BYTE*)data.data(), &dl) != ERROR_SUCCESS) continue;
        StartupEntry e;
        e.name = name;
        if (e.name.empty()) continue;   // the key's default value isn't a startup entry
        e.command = ExpandQuoted(data.data(), e.exe);
        e.source = s.label;
        e.sourceId = sourceId;
        e.allUsers = s.root == HKEY_LOCAL_MACHINE;
        e.enabled = ReadApproved(s, e.name);
        e.key = std::to_wstring(sourceId) + L"|" + e.name;
        Describe(e);
        out.push_back(e);
    }
    RegCloseKey(h);
}

static void ReadFolder(int sourceId, std::vector<StartupEntry>& out) {
    const Source& s = kSources[sourceId];
    std::wstring dir = KnownFolder(s.root == HKEY_CURRENT_USER ? FOLDERID_Startup : FOLDERID_CommonStartup);
    if (dir.empty()) return;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    IShellLinkW* link = nullptr;
    IPersistFile* pf = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&link)))
        link->QueryInterface(IID_IPersistFile, (void**)&pf);
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring n = fd.cFileName;
        if (IEquals(n, L"desktop.ini")) continue;
        StartupEntry e;
        e.name = n;
        e.source = s.label;
        e.sourceId = sourceId;
        e.allUsers = s.root == HKEY_LOCAL_MACHINE;
        e.enabled = ReadApproved(s, n);
        e.key = std::to_wstring(sourceId) + L"|" + n;
        std::wstring full = dir + L"\\" + n;
        e.command = full;
        e.exe = full;
        size_t dot = n.find_last_of(L'.');
        std::wstring ext = dot == std::wstring::npos ? L"" : Lower(n.substr(dot));
        if (ext == L".lnk" && pf && SUCCEEDED(pf->Load(full.c_str(), STGM_READ))) {
            wchar_t target[MAX_PATH * 2] = {}, args[1024] = {};
            if (SUCCEEDED(link->GetPath(target, MAX_PATH * 2, nullptr, SLGP_RAWPATH)) && target[0]) {
                e.exe = ExpandEnv(target);
                link->GetArguments(args, 1024);
                e.command = L"\"" + e.exe + L"\"" + (args[0] ? std::wstring(L" ") + args : L"");
            }
        }
        if (dot != std::wstring::npos) e.name = n.substr(0, dot);   // "Spotify.lnk" -> "Spotify"
        Describe(e);
        out.push_back(e);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    if (pf) pf->Release();
    if (link) link->Release();
}

void Load(HWND notify) {
    std::thread([notify] {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        auto* list = new StartupList;
        for (int i = 0; i < (int)(sizeof(kSources) / sizeof(kSources[0])); ++i) {
            if (kSources[i].folder) ReadFolder(i, list->entries);
            else ReadRunKey(i, list->entries);
        }
        std::sort(list->entries.begin(), list->entries.end(), [](const StartupEntry& a, const StartupEntry& b) {
            return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
        });
        CoUninitialize();
        if (!PostMessageW(notify, WM_HELM_STARTUP, 0, (LPARAM)list)) delete list;
    }).detach();
}

bool SetEnabled(const StartupEntry& e, bool enabled, std::wstring* error) {
    if (e.sourceId < 0 || e.sourceId >= (int)(sizeof(kSources) / sizeof(kSources[0]))) return false;
    const Source& s = kSources[e.sourceId];
    std::wstring key = std::wstring(kApproved) + s.approved;
    // for a Startup folder entry the value name is the file name, including ".lnk"
    std::wstring valueName = e.name;
    if (s.folder) {
        size_t bar = e.key.find(L'|');
        valueName = bar == std::wstring::npos ? e.name : e.key.substr(bar + 1);
    }
    HKEY h;
    LONG r = RegCreateKeyExW(s.root, key.c_str(), 0, nullptr, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr, &h, nullptr);
    if (r != ERROR_SUCCESS) {
        if (error) *error = r == ERROR_ACCESS_DENIED ? L"Changing apps that start for all users needs administrator rights."
                                                     : L"Windows didn't allow the change (error " + std::to_wstring(r) + L").";
        return false;
    }
    BYTE b[12] = {};
    b[0] = enabled ? 0x02 : 0x03;
    if (!enabled) {
        FILETIME now;
        GetSystemTimeAsFileTime(&now);
        memcpy(b + 4, &now, sizeof(now));
    }
    r = RegSetValueExW(h, valueName.c_str(), 0, REG_BINARY, b, sizeof(b));
    RegCloseKey(h);
    if (r != ERROR_SUCCESS) {
        if (error) *error = r == ERROR_ACCESS_DENIED ? L"Changing apps that start for all users needs administrator rights."
                                                     : L"Windows didn't allow the change (error " + std::to_wstring(r) + L").";
        return false;
    }
    Log(L"startup: %ls %ls (%ls)", enabled ? L"enabled" : L"disabled", e.name.c_str(), s.label);
    return true;
}

} // namespace startup
