// Helm - tweaks (see tweaks.h)
#include "tweaks.h"
#include "util.h"
#include "json.h"
#include <shlobj.h>
#include <powrprof.h>
#include <mutex>
#include <thread>
#include <algorithm>

// ---------------------------------------------------------------- the list
static const wchar_t* CDM = L"Software\\Microsoft\\Windows\\CurrentVersion\\ContentDeliveryManager";
static const wchar_t* ADV = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced";

static RegValue D(HKEY root, const std::wstring& path, const std::wstring& name, DWORD on, DWORD off) {
    RegValue v;
    v.root = root; v.path = path; v.name = name; v.on = on; v.off = off;
    return v;
}
static RegValue Ddel(HKEY root, const std::wstring& path, const std::wstring& name, DWORD on) {
    RegValue v = D(root, path, name, on, 0);
    v.offDeletes = true;
    return v;
}
static RegValue S(HKEY root, const std::wstring& path, const std::wstring& name, const std::wstring& on, const std::wstring& off) {
    RegValue v;
    v.root = root; v.path = path; v.name = name; v.isString = true; v.onStr = on; v.offStr = off;
    return v;
}

static int WindowsBuild() {
    static int b = -1;
    if (b < 0) {
        std::wstring s;
        b = RegReadString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"CurrentBuild", s) ? _wtoi(s.c_str()) : 0;
    }
    return b;
}

enum { LiveNone = 0, LiveExplorer, LiveMenuDelay, LiveMouse, LiveColors };

const std::vector<Tweak>& AllTweaks() {
    static std::vector<Tweak> list;
    if (!list.empty()) return list;
    std::vector<Tweak> all;
    auto add = [&](const wchar_t* group, const wchar_t* id, const wchar_t* name, const wchar_t* desc, std::vector<RegValue> values,
                   bool rec = false, AfterTweak after = AfterTweak::None, int live = LiveNone, int minBuild = 0) {
        Tweak t;
        t.group = group; t.id = id; t.name = name; t.description = desc; t.values = std::move(values);
        t.recommended = rec; t.after = after; t.live = live; t.minBuild = minBuild;
        for (auto& v : t.values) if (v.root == HKEY_LOCAL_MACHINE) t.admin = true;
        all.push_back(std::move(t));
    };
    HKEY CU = HKEY_CURRENT_USER, LM = HKEY_LOCAL_MACHINE;

    // ---- Privacy
    add(L"Privacy", L"adid", L"Turn off the advertising ID",
        L"Apps can't use an ID tied to you to show personalized ads.",
        {D(CU, L"Software\\Microsoft\\Windows\\CurrentVersion\\AdvertisingInfo", L"Enabled", 0, 1)}, true);
    add(L"Privacy", L"tailored", L"Turn off tailored experiences",
        L"Microsoft won't use your diagnostic data for tips, ads and recommendations.",
        {D(CU, L"Software\\Microsoft\\Windows\\CurrentVersion\\Privacy", L"TailoredExperiencesWithDiagnosticDataEnabled", 0, 1)}, true);
    add(L"Privacy", L"inking", L"Turn off inking and typing personalization",
        L"Stops Windows collecting what you type and write to build a personal dictionary.",
        {D(CU, L"Software\\Microsoft\\InputPersonalization", L"RestrictImplicitInkCollection", 1, 0),
         D(CU, L"Software\\Microsoft\\InputPersonalization", L"RestrictImplicitTextCollection", 1, 0),
         D(CU, L"Software\\Microsoft\\InputPersonalization\\TrainedDataStore", L"HarvestContacts", 0, 1),
         D(CU, L"Software\\Microsoft\\Personalization\\Settings", L"AcceptedPrivacyPolicy", 0, 1)}, true);
    add(L"Privacy", L"feedback", L"Never ask for feedback",
        L"No more \"How likely are you to recommend Windows?\" pop-ups.",
        {Ddel(CU, L"Software\\Microsoft\\Siuf\\Rules", L"NumberOfSIUFInPeriod", 0)}, true);
    add(L"Privacy", L"speech", L"Turn off online speech recognition",
        L"Your voice isn't sent to Microsoft. Voice typing (Windows+H) stops working.",
        {D(CU, L"Software\\Microsoft\\Speech_OneCore\\Settings\\OnlineSpeechPrivacy", L"HasAccepted", 0, 1)});
    add(L"Privacy", L"activity", L"Don't keep activity history",
        L"Windows stops recording which apps and files you use, and doesn't send it to Microsoft.",
        {Ddel(LM, L"SOFTWARE\\Policies\\Microsoft\\Windows\\System", L"PublishUserActivities", 0),
         Ddel(LM, L"SOFTWARE\\Policies\\Microsoft\\Windows\\System", L"UploadUserActivities", 0)}, true);
    add(L"Privacy", L"telemetry", L"Send only required diagnostic data",
        L"The minimum Windows allows. Settings will then say some options are managed by your organization.",
        {Ddel(LM, L"SOFTWARE\\Policies\\Microsoft\\Windows\\DataCollection", L"AllowTelemetry", 1)}, true);

    // ---- Ads and suggestions
    add(L"Ads and suggestions", L"startads", L"Hide app suggestions in Start",
        L"No more promoted apps in the Start menu.",
        {D(CU, CDM, L"SystemPaneSuggestionsEnabled", 0, 1), D(CU, CDM, L"SubscribedContent-338388Enabled", 0, 1)}, true);
    add(L"Ads and suggestions", L"settingsads", L"Hide suggested content in Settings",
        L"Removes the suggestions and offers shown inside the Settings app.",
        {D(CU, CDM, L"SubscribedContent-338393Enabled", 0, 1), D(CU, CDM, L"SubscribedContent-353694Enabled", 0, 1),
         D(CU, CDM, L"SubscribedContent-353696Enabled", 0, 1)}, true);
    add(L"Ads and suggestions", L"tips", L"Turn off tips and suggestion notifications",
        L"Windows stops sending \"get the most out of Windows\" notifications.",
        {D(CU, CDM, L"SubscribedContent-338389Enabled", 0, 1), D(CU, CDM, L"SoftLandingEnabled", 0, 1),
         D(CU, L"Software\\Microsoft\\Windows\\CurrentVersion\\UserProfileEngagement", L"ScoobeSystemSettingEnabled", 0, 1)}, true);
    add(L"Ads and suggestions", L"lockads", L"Turn off lock screen tips and ads",
        L"Your lock screen shows only your picture, no fun facts or promotions.",
        {D(CU, CDM, L"RotatingLockScreenOverlayEnabled", 0, 1), D(CU, CDM, L"SubscribedContent-338387Enabled", 0, 1)}, true);
    add(L"Ads and suggestions", L"autoinstall", L"Stop apps installing themselves",
        L"Windows stops quietly adding promoted apps and games for you.",
        {D(CU, CDM, L"SilentInstalledAppsEnabled", 0, 1), D(CU, CDM, L"PreInstalledAppsEnabled", 0, 1),
         D(CU, CDM, L"OemPreInstalledAppsEnabled", 0, 1)}, true);
    add(L"Ads and suggestions", L"websearch", L"Turn off web results in Start search",
        L"Searching from Start only finds things on your PC, and it's faster.",
        {Ddel(CU, L"Software\\Policies\\Microsoft\\Windows\\Explorer", L"DisableSearchBoxSuggestions", 1),
         Ddel(CU, L"Software\\Microsoft\\Windows\\CurrentVersion\\Search", L"BingSearchEnabled", 0)}, true, AfterTweak::Explorer);

    // ---- File Explorer and taskbar
    {
        RegValue v = D(CU, ADV, L"HideFileExt", 0, 1);
        add(L"File Explorer and taskbar", L"extensions", L"Show file name extensions",
            L"See \"report.pdf\" instead of \"report\", so you always know what a file is.", {v}, true, AfterTweak::None, LiveExplorer);
    }
    add(L"File Explorer and taskbar", L"hidden", L"Show hidden files and folders",
        L"Shows files Windows normally hides, like AppData.", {D(CU, ADV, L"Hidden", 1, 2)}, false, AfterTweak::None, LiveExplorer);
    add(L"File Explorer and taskbar", L"thispc", L"Open File Explorer to This PC",
        L"Starts on your drives instead of Home.", {Ddel(CU, ADV, L"LaunchTo", 1)});
    {
        RegValue v = S(CU, L"Software\\Classes\\CLSID\\{86ca1aa0-34aa-4e8b-a509-50c905bae2a2}\\InprocServer32", L"", L"", L"");
        v.offKey = L"Software\\Classes\\CLSID\\{86ca1aa0-34aa-4e8b-a509-50c905bae2a2}";
        add(L"File Explorer and taskbar", L"classicmenu", L"Classic right-click menu",
            L"Right-click shows every option right away, without \"Show more options\".", {v}, false, AfterTweak::Explorer, LiveNone, 22000);
    }
    add(L"File Explorer and taskbar", L"endtask", L"Add \"End task\" to taskbar right-click",
        L"Close a frozen app from its taskbar button.",
        {D(CU, ADV + std::wstring(L"\\TaskbarDeveloperSettings"), L"TaskbarEndTask", 1, 0)}, false, AfterTweak::None, LiveNone, 22631);

    // ---- Performance
    add(L"Performance", L"startupdelay", L"Start apps sooner after sign-in",
        L"Removes the few seconds Windows waits before starting your startup apps.",
        {Ddel(CU, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Serialize", L"StartupDelayInMSec", 0)});
    add(L"Performance", L"menudelay", L"Faster menus",
        L"Submenus open after 100 ms instead of 400 ms.",
        {S(CU, L"Control Panel\\Desktop", L"MenuShowDelay", L"100", L"400")}, false, AfterTweak::None, LiveMenuDelay);
    add(L"Performance", L"transparency", L"Turn off transparency effects",
        L"Solid taskbar and menus. A little lighter on older graphics.",
        {D(CU, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"EnableTransparency", 0, 1)}, false, AfterTweak::None, LiveColors);
    add(L"Performance", L"bgapps", L"Stop Store apps running in the background",
        L"Saves memory and battery. Those apps can't send notifications while closed.",
        {D(CU, L"Software\\Microsoft\\Windows\\CurrentVersion\\BackgroundAccessApplications", L"GlobalUserDisabled", 1, 0)});
    add(L"Performance", L"hags", L"Hardware-accelerated GPU scheduling",
        L"Lets the graphics card manage its own memory. Can lower latency in games. Needs a restart.",
        {D(LM, L"SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers", L"HwSchMode", 2, 1)}, false, AfterTweak::Restart, LiveNone, 19041);

    // ---- Gaming
    {
        RegValue v = D(CU, L"Software\\Microsoft\\GameBar", L"AutoGameModeEnabled", 1, 0);
        v.absentIsOn = true;
        add(L"Gaming", L"gamemode", L"Game Mode", L"Windows puts the game first and holds back updates and notifications while you play.", {v});
    }
    add(L"Gaming", L"gamedvr", L"Turn off background recording",
        L"Stops Xbox Game Bar quietly recording your last few minutes of play.",
        {D(CU, L"System\\GameConfigStore", L"GameDVR_Enabled", 0, 1),
         D(CU, L"Software\\Microsoft\\Windows\\CurrentVersion\\GameDVR", L"AppCaptureEnabled", 0, 1)});
    add(L"Gaming", L"mouseaccel", L"Turn off mouse acceleration",
        L"The pointer moves the same distance however fast you move the mouse (\"Enhance pointer precision\" off).",
        {S(CU, L"Control Panel\\Mouse", L"MouseSpeed", L"0", L"1"), S(CU, L"Control Panel\\Mouse", L"MouseThreshold1", L"0", L"6"),
         S(CU, L"Control Panel\\Mouse", L"MouseThreshold2", L"0", L"10")}, false, AfterTweak::None, LiveMouse);

    int build = WindowsBuild();
    for (auto& t : all) if (!t.minBuild || build >= t.minBuild || build == 0) list.push_back(std::move(t));
    return list;
}

namespace tweaks {

// ---------------------------------------------------------------- reading and writing values
struct Current { bool exists = false; bool isString = false; DWORD d = 0; std::wstring s; };

static Current Read(const RegValue& v) {
    Current c;
    const wchar_t* name = v.name.empty() ? nullptr : v.name.c_str();
    DWORD type = 0, size = 0;
    if (RegGetValueW(v.root, v.path.c_str(), name, RRF_RT_ANY | RRF_SUBKEY_WOW6464KEY | RRF_NOEXPAND, &type, nullptr, &size) != ERROR_SUCCESS) return c;
    c.exists = true;
    if (type == REG_DWORD) {
        DWORD d = 0, sz = sizeof(d);
        if (RegGetValueW(v.root, v.path.c_str(), name, RRF_RT_REG_DWORD | RRF_SUBKEY_WOW6464KEY, nullptr, &d, &sz) == ERROR_SUCCESS) c.d = d;
    } else if (type == REG_SZ || type == REG_EXPAND_SZ) {
        c.isString = true;
        std::wstring s;
        RegReadString(v.root, v.path, v.name, s);
        c.s = s;
    } else {
        c.exists = true;   // some other type: counts as "something else is there"
        c.isString = true;
        c.s = L"\x1";
    }
    return c;
}

static bool ValueIsOn(const RegValue& v) {
    Current c = Read(v);
    if (!c.exists) return v.absentIsOn;
    if (v.isString) return c.isString && c.s == v.onStr;
    if (c.isString) {
        // a number stored as text (some tools do that)
        return !c.s.empty() && c.s.find_first_not_of(L"0123456789") == std::wstring::npos && (DWORD)_wtoi(c.s.c_str()) == v.on;
    }
    return c.d == v.on;
}

bool IsOn(const Tweak& t) {
    for (auto& v : t.values) if (!ValueIsOn(v)) return false;
    return !t.values.empty();
}

static LONG WriteDword(HKEY root, const std::wstring& path, const std::wstring& name, DWORD d) {
    HKEY h;
    LONG r = RegCreateKeyExW(root, path.c_str(), 0, nullptr, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr, &h, nullptr);
    if (r != ERROR_SUCCESS) return r;
    r = RegSetValueExW(h, name.empty() ? nullptr : name.c_str(), 0, REG_DWORD, (const BYTE*)&d, sizeof(d));
    RegCloseKey(h);
    return r;
}

static LONG WriteString(HKEY root, const std::wstring& path, const std::wstring& name, const std::wstring& s) {
    HKEY h;
    LONG r = RegCreateKeyExW(root, path.c_str(), 0, nullptr, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr, &h, nullptr);
    if (r != ERROR_SUCCESS) return r;
    r = RegSetValueExW(h, name.empty() ? nullptr : name.c_str(), 0, REG_SZ, (const BYTE*)s.c_str(), (DWORD)((s.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(h);
    return r;
}

static LONG DeleteValue(HKEY root, const std::wstring& path, const std::wstring& name) {
    HKEY h;
    LONG r = RegOpenKeyExW(root, path.c_str(), 0, KEY_SET_VALUE | KEY_WOW64_64KEY, &h);
    if (r == ERROR_FILE_NOT_FOUND) return ERROR_SUCCESS;
    if (r != ERROR_SUCCESS) return r;
    r = RegDeleteValueW(h, name.empty() ? nullptr : name.c_str());
    RegCloseKey(h);
    return r == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : r;
}

static LONG DeleteKey(HKEY root, const std::wstring& path) {
    LONG r = RegDeleteTreeW(root, path.c_str());
    if (r == ERROR_SUCCESS || r == ERROR_FILE_NOT_FOUND) {
        RegDeleteKeyExW(root, path.c_str(), KEY_WOW64_64KEY, 0);
        return ERROR_SUCCESS;
    }
    return r;
}

// ---------------------------------------------------------------- backups
static std::mutex s_lock;
static json::Value s_backup;
static bool s_loaded = false;

static std::wstring BackupPath() { return DataDir() + L"\\tweaks-backup.json"; }

static void LoadBackups() {
    if (s_loaded) return;
    s_loaded = true;
    std::wstring text;
    if (ReadTextFile(BackupPath(), text)) json::Parser(text).Parse(s_backup);
    if (s_backup.type != json::Value::Object) s_backup = json::Value::MakeObject();
}

static void SaveBackups() {
    std::wstring out;
    json::Write(s_backup, out);
    if (!WriteTextFileAtomic(BackupPath(), out)) Log(L"tweaks: couldn't save the backup file");
}

bool HasBackup(const std::wstring& id) {
    std::lock_guard<std::mutex> g(s_lock);
    LoadBackups();
    return s_backup.Get(id) != nullptr;
}

// Telling every window about a change can wait on hung apps, so it happens on its own thread.
static void ApplyLiveNow(int live) {
    switch (live) {
        case LiveExplorer:
            SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
            SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"ShellState", SMTO_ABORTIFHUNG, 1000, nullptr);
            break;
        case LiveMenuDelay: {
            std::wstring s;
            DWORD ms = 400;
            if (RegReadString(HKEY_CURRENT_USER, L"Control Panel\\Desktop", L"MenuShowDelay", s)) ms = (DWORD)_wtoi(s.c_str());
            SystemParametersInfoW(SPI_SETMENUSHOWDELAY, ms, nullptr, SPIF_SENDCHANGE);
            break;
        }
        case LiveMouse: {
            std::wstring a, b, c;
            RegReadString(HKEY_CURRENT_USER, L"Control Panel\\Mouse", L"MouseThreshold1", a);
            RegReadString(HKEY_CURRENT_USER, L"Control Panel\\Mouse", L"MouseThreshold2", b);
            RegReadString(HKEY_CURRENT_USER, L"Control Panel\\Mouse", L"MouseSpeed", c);
            int m[3] = {_wtoi(a.c_str()), _wtoi(b.c_str()), _wtoi(c.c_str())};
            SystemParametersInfoW(SPI_SETMOUSE, 0, m, SPIF_SENDCHANGE);
            break;
        }
        case LiveColors:
            SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"ImmersiveColorSet", SMTO_ABORTIFHUNG, 1000, nullptr);
            break;
    }
}

static void ApplyLive(int live) {
    if (live == LiveNone) return;
    std::thread([live] { ApplyLiveNow(live); }).detach();
}

static std::wstring ErrorText(LONG r, const Tweak& t) {
    if (r == ERROR_ACCESS_DENIED)
        return t.admin ? L"\"" + t.name + L"\" needs administrator rights." : L"Windows blocked the change to \"" + t.name + L"\".";
    return L"Couldn't change \"" + t.name + L"\" (error " + std::to_wstring(r) + L").";
}

bool Set(const Tweak& t, bool on, std::wstring* error) {
    std::lock_guard<std::mutex> g(s_lock);
    LoadBackups();
    LONG r = ERROR_SUCCESS;
    if (on) {
        // remember what was there, the first time only
        if (!s_backup.Get(t.id)) {
            json::Value arr = json::Value::MakeArray();
            for (auto& v : t.values) {
                Current c = Read(v);
                json::Value e = json::Value::MakeObject();
                e.Set(L"exists", json::Value::Boolean(c.exists));
                e.Set(L"str", json::Value::Boolean(c.isString));
                e.Set(L"d", json::Value::Num(c.d));
                e.Set(L"s", json::Value::Str(c.s));
                if (!v.offKey.empty()) e.Set(L"key", json::Value::Boolean(RegKeyExists(v.root, v.offKey)));
                arr.arr.push_back(e);
            }
            s_backup.Set(t.id, arr);
            SaveBackups();
        }
        for (auto& v : t.values) {
            r = v.isString ? WriteString(v.root, v.path, v.name, v.onStr) : WriteDword(v.root, v.path, v.name, v.on);
            if (r != ERROR_SUCCESS) break;
        }
    } else {
        const json::Value* b = s_backup.Get(t.id);
        bool useBackup = b && b->type == json::Value::Array && b->arr.size() == t.values.size();
        for (size_t i = 0; i < t.values.size() && r == ERROR_SUCCESS; ++i) {
            const RegValue& v = t.values[i];
            if (useBackup) {
                const json::Value& e = b->arr[i];
                if (!v.offKey.empty() && !e.GetBool(L"key", false)) { r = DeleteKey(v.root, v.offKey); continue; }
                if (!e.GetBool(L"exists", false)) r = DeleteValue(v.root, v.path, v.name);
                else if (e.GetBool(L"str", false)) {
                    std::wstring s = e.GetStr(L"s");
                    if (s == L"\x1") continue;   // an unusual value type was there; leave the current one
                    r = WriteString(v.root, v.path, v.name, s);
                } else r = WriteDword(v.root, v.path, v.name, (DWORD)e.GetNum(L"d", 0));
            } else if (!v.offKey.empty()) {
                r = DeleteKey(v.root, v.offKey);
            } else if (v.offDeletes) {
                r = DeleteValue(v.root, v.path, v.name);
            } else {
                r = v.isString ? WriteString(v.root, v.path, v.name, v.offStr) : WriteDword(v.root, v.path, v.name, v.off);
            }
        }
        if (r == ERROR_SUCCESS && b) {
            // forget the backup now that it's been put back
            auto& o = s_backup.obj;
            o.erase(std::remove_if(o.begin(), o.end(), [&](const std::pair<std::wstring, json::Value>& kv) { return kv.first == t.id; }), o.end());
            SaveBackups();
        }
    }
    if (r != ERROR_SUCCESS) {
        if (error) *error = ErrorText(r, t);
        Log(L"tweak %ls -> %ls failed: %ld", t.id.c_str(), on ? L"on" : L"off", r);
        return false;
    }
    Log(L"tweak %ls -> %ls", t.id.c_str(), on ? L"on" : L"off");
    ApplyLive(t.live);
    return true;
}

// ---------------------------------------------------------------- Explorer
void RestartExplorerNow();
void RestartExplorer() {
    std::thread([] { RestartExplorerNow(); }).detach();
}

void RestartExplorerNow() {
    HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr);
    DWORD pid = 0;
    if (tray) GetWindowThreadProcessId(tray, &pid);
    HANDLE proc = pid ? OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, FALSE, pid) : nullptr;
    if (!IsElevated() && tray) {
        // the polite way: Explorer saves its state and exits; then start it again as you
        PostMessageW(tray, WM_USER + 436, 0, 0);
        bool exited = proc && WaitForSingleObject(proc, 6000) == WAIT_OBJECT_0;
        if (!exited && proc) { TerminateProcess(proc, 1); WaitForSingleObject(proc, 3000); }
        if (proc) CloseHandle(proc);
        Sleep(300);
        if (!FindWindowW(L"Shell_TrayWnd", nullptr)) {
            wchar_t win[MAX_PATH];
            GetWindowsDirectoryW(win, MAX_PATH);
            std::wstring exe = std::wstring(win) + L"\\explorer.exe";
            LaunchDetached(exe, L"");
        }
        Log(L"restarted Explorer");
        return;
    }
    // running as administrator: starting explorer.exe from here would give an elevated shell, so
    // end it and let Windows restart it as you (AutoRestartShell)
    if (proc) {
        TerminateProcess(proc, 1);
        WaitForSingleObject(proc, 3000);
        CloseHandle(proc);
    }
    Log(L"restarted Explorer (Windows restarts the shell itself)");
}

// ---------------------------------------------------------------- power plans
static const GUID kUltimate = {0xe9a42b02, 0xd5df, 0x448d, {0xaa, 0x00, 0x03, 0xf1, 0x47, 0x49, 0xeb, 0x61}};

static std::wstring PlanName(const GUID& g) {
    DWORD size = 0;
    if (PowerReadFriendlyName(nullptr, &g, nullptr, nullptr, nullptr, &size) != ERROR_SUCCESS || !size) return L"";
    std::vector<BYTE> buf(size + 2, 0);
    if (PowerReadFriendlyName(nullptr, &g, nullptr, nullptr, buf.data(), &size) != ERROR_SUCCESS) return L"";
    return (const wchar_t*)buf.data();
}

std::vector<PowerPlan> PowerPlans() {
    std::vector<PowerPlan> out;
    GUID* active = nullptr;
    GUID act{};
    bool haveActive = PowerGetActiveScheme(nullptr, &active) == ERROR_SUCCESS && active;
    if (haveActive) { act = *active; LocalFree(active); }
    for (ULONG i = 0; i < 64; ++i) {
        GUID g;
        DWORD size = sizeof(g);
        if (PowerEnumerate(nullptr, nullptr, nullptr, ACCESS_SCHEME, i, (UCHAR*)&g, &size) != ERROR_SUCCESS) break;
        PowerPlan p;
        p.guid = g;
        p.name = PlanName(g);
        if (p.name.empty()) continue;
        p.active = haveActive && IsEqualGUID(g, act);
        out.push_back(p);
    }
    return out;
}

bool SetPowerPlan(const GUID& g, std::wstring* error) {
    DWORD r = PowerSetActiveScheme(nullptr, &g);
    if (r != ERROR_SUCCESS) {
        if (error) *error = L"Windows didn't switch the power plan (error " + std::to_wstring(r) + L").";
        return false;
    }
    return true;
}

bool HasUltimatePlan() {
    std::wstring name = PlanName(kUltimate);
    for (auto& p : PowerPlans()) {
        if (IsEqualGUID(p.guid, kUltimate)) return true;
        if (!name.empty() && p.name == name) return true;
    }
    return false;
}

bool AddUltimatePlan(std::wstring* error) {
    GUID* dest = nullptr;
    DWORD r = PowerDuplicateScheme(nullptr, &kUltimate, &dest);
    if (r != ERROR_SUCCESS) {
        if (error) *error = r == ERROR_ACCESS_DENIED ? L"Adding a power plan needs administrator rights."
                                                     : L"This PC doesn't offer the Ultimate Performance plan (error " + std::to_wstring(r) + L").";
        return false;
    }
    if (dest) LocalFree(dest);
    return true;
}

} // namespace tweaks
