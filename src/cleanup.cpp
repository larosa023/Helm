// Helm - junk cleanup (see cleanup.h for the safety rules)
#include "cleanup.h"
#include "util.h"
#include "gfx.h"
#include <shlobj.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <thread>
#include <set>
#include <algorithm>

const std::vector<CleanCategory>& CleanCategories() {
    static std::vector<CleanCategory> list;
    if (!list.empty()) return list;
    auto add = [&](const wchar_t* id, const wchar_t* name, const wchar_t* desc, wchar_t glyph, bool on, const wchar_t* note = L"") {
        CleanCategory c;
        c.id = id; c.name = name; c.description = desc; c.glyph = glyph; c.defaultOn = on; c.note = note;
        list.push_back(c);
    };
    using namespace gfx;
    add(L"temp", L"Temporary files", L"Leftovers apps put in your Temp folder. Only files older than a day.", icon::Document, true);
    add(L"wintemp", L"Windows temporary files", L"Windows' own Temp folder. Only files older than a day.", icon::Windows, true);
    add(L"thumbs", L"Thumbnail cache", L"Picture previews File Explorer has saved. It rebuilds them as you browse.", icon::Folder, true);
    add(L"crash", L"Crash dumps and error reports", L"Reports saved after apps or Windows crashed.", icon::Warning, true);
    add(L"wupdate", L"Old Windows Update downloads", L"Update files Windows already installed (older than 10 days).", icon::Update, true);
    add(L"winlogs", L"Old Windows logs", L"Setup and servicing logs older than 30 days.", icon::Document, true);
    add(L"chrome", L"Chrome cache", L"Saved copies of websites. Your logins, history and bookmarks stay.", icon::Globe, true);
    add(L"edge", L"Edge cache", L"Saved copies of websites. Your logins, history and favorites stay.", icon::Globe, true);
    add(L"brave", L"Brave cache", L"Saved copies of websites. Your logins, history and bookmarks stay.", icon::Globe, true);
    add(L"vivaldi", L"Vivaldi cache", L"Saved copies of websites. Your logins, history and bookmarks stay.", icon::Globe, true);
    add(L"opera", L"Opera cache", L"Saved copies of websites. Your logins, history and bookmarks stay.", icon::Globe, true);
    add(L"firefox", L"Firefox cache", L"Saved copies of websites. Your logins, history and bookmarks stay.", icon::Globe, true);
    add(L"apps", L"App caches", L"Web caches kept by Discord, Spotify, Steam and Teams. Your accounts and downloads stay.", icon::Apps, true);
    add(L"shaders", L"Shader cache", L"Compiled graphics shaders. Games rebuild them on first launch.", icon::Game, false,
        L"Games may stutter briefly the first time you play after clearing this.");
    CleanCategory rb;
    rb.id = L"recycle"; rb.name = L"Recycle Bin"; rb.description = L"Files you deleted. Once emptied, they can't be restored.";
    rb.glyph = icon::Delete; rb.defaultOn = false; rb.recycleBin = true;
    list.push_back(rb);
    return list;
}

namespace cleanup {

struct Target {
    std::wstring dir;
    std::wstring patterns = L"*";      // ';'-separated
    bool recursive = true;
    int minAgeHours = 0;
    bool admin = false;
    std::wstring proc;                  // lowercase exe that must not be running
    std::wstring app;                   // its friendly name
};

static std::atomic<bool> s_busy{false};
static std::atomic<bool> s_cancel{false};

bool Busy() { return s_busy; }
void Cancel() { s_cancel = true; }

// ---------------------------------------------------------------- where things are
static std::wstring StripSlash(std::wstring p) {
    while (p.size() > 3 && (p.back() == L'\\' || p.back() == L'/')) p.pop_back();
    return p;
}

static std::wstring WinDir() {
    wchar_t b[MAX_PATH];
    UINT n = GetWindowsDirectoryW(b, MAX_PATH);
    return (n && n < MAX_PATH) ? std::wstring(b) : L"";
}

// A folder Helm may clean inside: absolute, at least two levels deep, and not one of the folders
// that hold everything else.
static bool SafeTarget(const std::wstring& d0) {
    std::wstring d = StripSlash(d0);
    if (d.size() < 6 || !iswalpha(d[0]) || d[1] != L':' || d[2] != L'\\') return false;
    if (d.find(L"\\..") != std::wstring::npos || d.find(L"\\.\\") != std::wstring::npos) return false;
    if (std::count(d.begin(), d.end(), L'\\') < 2) return false;
    static std::vector<std::wstring> guarded;
    if (guarded.empty()) {
        for (const GUID* g : {&FOLDERID_Profile, &FOLDERID_LocalAppData, &FOLDERID_RoamingAppData, &FOLDERID_ProgramData, &FOLDERID_Windows,
                              &FOLDERID_ProgramFiles, &FOLDERID_ProgramFilesX86, &FOLDERID_UserProfiles, &FOLDERID_Documents,
                              &FOLDERID_Desktop, &FOLDERID_Downloads, &FOLDERID_System})
            guarded.push_back(StripSlash(KnownFolder(*g)));
    }
    for (auto& g : guarded) if (!g.empty() && IEquals(g, d)) return false;
    return true;
}

static void ChromiumProfiles(const std::wstring& userData, const std::wstring& proc, const std::wstring& app, std::vector<Target>& out) {
    if (!DirExists(userData)) return;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW((userData + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchLimitToDirectories, nullptr, 0);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::wstring n = fd.cFileName;
            if (n == L"." || n == L".." || !(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
            std::wstring prof = userData + L"\\" + n;
            if (!FileExists(prof + L"\\Preferences")) continue;   // a real profile folder
            for (const wchar_t* sub : {L"Cache", L"Code Cache", L"GPUCache", L"DawnGraphiteCache", L"DawnWebGPUCache"})
                out.push_back({prof + L"\\" + sub, L"*", true, 0, false, proc, app});
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    for (const wchar_t* sub : {L"ShaderCache", L"GrShaderCache", L"GraphiteDawnCache"})
        out.push_back({userData + L"\\" + sub, L"*", true, 0, false, proc, app});
}

static std::vector<Target> TargetsFor(const std::wstring& id) {
    std::vector<Target> t;
    std::wstring local = StripSlash(KnownFolder(FOLDERID_LocalAppData));
    std::wstring roaming = StripSlash(KnownFolder(FOLDERID_RoamingAppData));
    std::wstring progData = StripSlash(KnownFolder(FOLDERID_ProgramData));
    std::wstring win = StripSlash(WinDir());
    if (local.empty() || win.empty()) return t;   // something is very wrong; clean nothing
    if (id == L"temp") {
        wchar_t b[MAX_PATH + 1];
        DWORD n = GetTempPathW(MAX_PATH, b);
        std::wstring tmp = (n && n <= MAX_PATH) ? StripSlash(b) : L"";
        // TEMP is often in 8.3 form (C:\Users\ANTHON~1\...): compare long paths so nothing counts twice
        wchar_t lb[1024];
        DWORD ln = tmp.empty() ? 0 : GetLongPathNameW(tmp.c_str(), lb, 1024);
        if (ln && ln < 1024) tmp = StripSlash(lb);
        // a TEMP that points at the Windows folder belongs to "wintemp"
        if (!tmp.empty() && !IEquals(tmp, win + L"\\Temp")) t.push_back({tmp, L"*", true, 24});
        std::wstring lt = local + L"\\Temp";
        if (!IEquals(lt, tmp)) t.push_back({lt, L"*", true, 24});
    } else if (id == L"wintemp") {
        t.push_back({win + L"\\Temp", L"*", true, 24, true});
    } else if (id == L"thumbs") {
        t.push_back({local + L"\\Microsoft\\Windows\\Explorer", L"thumbcache_*.db", false});
    } else if (id == L"crash") {
        t.push_back({local + L"\\CrashDumps", L"*", true});
        t.push_back({local + L"\\Microsoft\\Windows\\WER\\ReportArchive", L"*", true});
        t.push_back({local + L"\\Microsoft\\Windows\\WER\\ReportQueue", L"*", true});
        if (!progData.empty()) {
            t.push_back({progData + L"\\Microsoft\\Windows\\WER\\ReportArchive", L"*", true, 0, true});
            t.push_back({progData + L"\\Microsoft\\Windows\\WER\\ReportQueue", L"*", true, 0, true});
        }
        t.push_back({win + L"\\Minidump", L"*.dmp", false, 0, true});
        t.push_back({win + L"\\LiveKernelReports", L"*.dmp", true, 0, true});
        t.push_back({win, L"MEMORY.DMP", false, 0, true});
    } else if (id == L"wupdate") {
        t.push_back({win + L"\\SoftwareDistribution\\Download", L"*", true, 24 * 10, true});
    } else if (id == L"winlogs") {
        t.push_back({win + L"\\Logs\\CBS", L"*.log;*.cab", false, 24 * 30, true});
        t.push_back({win + L"\\Logs\\DISM", L"*.log", false, 24 * 30, true});
        t.push_back({win + L"\\Logs\\WindowsUpdate", L"*.etl", false, 24 * 30, true});
        t.push_back({win + L"\\Panther", L"*.log", false, 24 * 30, true});
    } else if (id == L"chrome") {
        ChromiumProfiles(local + L"\\Google\\Chrome\\User Data", L"chrome.exe", L"Chrome", t);
    } else if (id == L"edge") {
        ChromiumProfiles(local + L"\\Microsoft\\Edge\\User Data", L"msedge.exe", L"Edge", t);
    } else if (id == L"brave") {
        ChromiumProfiles(local + L"\\BraveSoftware\\Brave-Browser\\User Data", L"brave.exe", L"Brave", t);
    } else if (id == L"vivaldi") {
        ChromiumProfiles(local + L"\\Vivaldi\\User Data", L"vivaldi.exe", L"Vivaldi", t);
    } else if (id == L"opera") {
        for (const wchar_t* ed : {L"Opera Stable", L"Opera GX Stable"}) {
            std::wstring base = local + L"\\Opera Software\\" + ed;
            for (const wchar_t* sub : {L"Cache", L"Code Cache", L"GPUCache"}) t.push_back({base + L"\\" + sub, L"*", true, 0, false, L"opera.exe", L"Opera"});
        }
    } else if (id == L"firefox") {
        std::wstring profiles = local + L"\\Mozilla\\Firefox\\Profiles";
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileExW((profiles + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchLimitToDirectories, nullptr, 0);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                std::wstring n = fd.cFileName;
                if (n == L"." || n == L".." || !(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
                t.push_back({profiles + L"\\" + n + L"\\cache2", L"*", true, 0, false, L"firefox.exe", L"Firefox"});
                t.push_back({profiles + L"\\" + n + L"\\startupCache", L"*", true, 0, false, L"firefox.exe", L"Firefox"});
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
    } else if (id == L"apps") {
        for (const wchar_t* sub : {L"Cache", L"Code Cache", L"GPUCache"}) {
            t.push_back({roaming + L"\\discord\\" + sub, L"*", true, 0, false, L"discord.exe", L"Discord"});
            t.push_back({roaming + L"\\Microsoft\\Teams\\" + sub, L"*", true, 0, false, L"teams.exe", L"Teams"});
        }
        t.push_back({local + L"\\Spotify\\Data", L"*", true, 0, false, L"spotify.exe", L"Spotify"});
        t.push_back({local + L"\\Spotify\\Storage", L"*", true, 0, false, L"spotify.exe", L"Spotify"});
        t.push_back({local + L"\\Steam\\htmlcache", L"*", true, 0, false, L"steam.exe", L"Steam"});
        t.push_back({local + L"\\Steam\\widevine", L"*.tmp", true, 0, false, L"steam.exe", L"Steam"});
    } else if (id == L"shaders") {
        t.push_back({local + L"\\D3DSCache", L"*", true});
        t.push_back({local + L"\\NVIDIA\\DXCache", L"*", true});
        t.push_back({local + L"\\NVIDIA\\GLCache", L"*", true});
        t.push_back({local + L"\\AMD\\DxCache", L"*", true});
        t.push_back({local + L"\\AMD\\DxcCache", L"*", true});
        t.push_back({local + L"\\AMD\\VkCache", L"*", true});
        t.push_back({local + L"Low\\Intel\\ShaderCache", L"*", true});
    }
    t.erase(std::remove_if(t.begin(), t.end(), [](const Target& x) { return !SafeTarget(x.dir); }), t.end());
    return t;
}

static std::set<std::wstring> RunningExes() {
    std::set<std::wstring> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;
    PROCESSENTRY32W pe{sizeof(pe)};
    if (Process32FirstW(snap, &pe)) {
        do out.insert(Lower(pe.szExeFile)); while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return out;
}

// ---------------------------------------------------------------- walking
struct Stats { uint64_t bytes = 0, files = 0, skipped = 0; };

static std::wstring Long(const std::wstring& p) { return IStartsWith(p, L"\\\\?\\") ? p : L"\\\\?\\" + p; }

static bool MatchAny(const std::wstring& patterns, const wchar_t* name) {
    size_t i = 0;
    while (i <= patterns.size()) {
        size_t e = patterns.find(L';', i);
        if (e == std::wstring::npos) e = patterns.size();
        std::wstring p = Trim(patterns.substr(i, e - i));
        if (!p.empty() && WildMatch(p, name)) return true;
        i = e + 1;
    }
    return false;
}

static bool OlderThan(const FILETIME& ft, const FILETIME& cutoff) { return CompareFileTime(&ft, &cutoff) < 0; }

static void Walk(const std::wstring& dir, const Target& t, const FILETIME& cutoff, bool del, Stats& st, int depth,
                 const std::function<void()>& tick) {
    if (s_cancel || depth > 48) return;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW((dir + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (s_cancel) break;
        const wchar_t* n = fd.cFileName;
        if (!wcscmp(n, L".") || !wcscmp(n, L"..")) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;   // never follow or delete links
        std::wstring path = dir + L"\\" + n;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!t.recursive) continue;
            Walk(path, t, cutoff, del, st, depth + 1, tick);
            // remove folders that are now empty, unless something made them recently
            if (del && !s_cancel && (!t.minAgeHours || OlderThan(fd.ftLastWriteTime, cutoff))) RemoveDirectoryW(path.c_str());
            continue;
        }
        if (!MatchAny(t.patterns, n)) continue;
        if (t.minAgeHours && !OlderThan(fd.ftLastWriteTime, cutoff)) continue;
        uint64_t size = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        if (!del) { st.bytes += size; st.files++; continue; }
        bool ok = DeleteFileW(path.c_str()) != 0;
        if (!ok && GetLastError() == ERROR_ACCESS_DENIED && (fd.dwFileAttributes & FILE_ATTRIBUTE_READONLY)) {
            SetFileAttributesW(path.c_str(), fd.dwFileAttributes & ~FILE_ATTRIBUTE_READONLY);
            ok = DeleteFileW(path.c_str()) != 0;
            if (!ok) SetFileAttributesW(path.c_str(), fd.dwFileAttributes);
        }
        if (ok) { st.bytes += size; st.files++; }
        else st.skipped++;
        if (tick) tick();
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static FILETIME Cutoff(int hours) {
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER u;
    u.LowPart = now.dwLowDateTime;
    u.HighPart = now.dwHighDateTime;
    u.QuadPart -= (ULONGLONG)hours * 3600ull * 10000000ull;
    FILETIME f;
    f.dwLowDateTime = u.LowPart;
    f.dwHighDateTime = u.HighPart;
    return f;
}

// ---------------------------------------------------------------- scan
void Scan(HWND notify) {
    if (s_busy.exchange(true)) return;
    s_cancel = false;
    std::thread([notify] {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        auto* res = new CleanScanResult;
        auto running = RunningExes();
        bool admin = IsElevated();
        for (auto& c : CleanCategories()) {
            CleanScanItem item;
            item.id = c.id;
            if (c.recycleBin) {
                SHQUERYRBINFO qi{};
                qi.cbSize = sizeof(qi);
                if (SUCCEEDED(SHQueryRecycleBinW(nullptr, &qi))) {
                    item.available = true;
                    item.bytes = (uint64_t)qi.i64Size;
                    item.files = (uint64_t)qi.i64NumItems;
                }
                res->items.push_back(item);
                continue;
            }
            auto targets = TargetsFor(c.id);
            bool anyUser = false, anyAdmin = false;
            for (auto& t : targets) {
                if (!DirExists(t.dir)) continue;
                item.available = true;
                if (t.admin) anyAdmin = true; else anyUser = true;
                if (t.admin && !admin) { item.needsAdmin = true; continue; }
                if (!t.proc.empty() && running.count(t.proc)) {
                    if (std::find(item.blockedBy.begin(), item.blockedBy.end(), t.app) == item.blockedBy.end()) item.blockedBy.push_back(t.app);
                }
                Stats st;
                Walk(Long(t.dir), t, Cutoff(t.minAgeHours), false, st, 0, nullptr);
                item.bytes += st.bytes;
                item.files += st.files;
            }
            item.adminOnly = anyAdmin && !anyUser;
            res->items.push_back(item);
            if (s_cancel) break;
        }
        res->cancelled = s_cancel;
        CoUninitialize();
        s_busy = false;
        if (!PostMessageW(notify, WM_HELM_CLEAN_SCAN, 0, (LPARAM)res)) delete res;
    }).detach();
}

// ---------------------------------------------------------------- clean
void Clean(HWND notify, const std::vector<std::wstring>& ids) {
    if (s_busy.exchange(true)) return;
    s_cancel = false;
    std::thread([notify, ids] {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        auto running = RunningExes();
        bool admin = IsElevated();
        auto* done = new CleanDone;
        DWORD lastPost = 0;
        int index = 0;
        for (auto& id : ids) {
            const CleanCategory* cat = nullptr;
            for (auto& c : CleanCategories()) if (c.id == id) cat = &c;
            if (!cat || s_cancel) { ++index; continue; }
            auto post = [&](bool force) {
                DWORD now = GetTickCount();
                if (!force && now - lastPost < 100) return;
                lastPost = now;
                auto* p = new CleanProgress;
                p->current = cat->name;
                p->freed = done->freed;
                p->percent = (int)(index * 100 / std::max<size_t>(1, ids.size()));
                if (!PostMessageW(notify, WM_HELM_CLEAN_PROGRESS, 0, (LPARAM)p)) delete p;
            };
            post(true);
            if (cat->recycleBin) {
                SHQUERYRBINFO qi{};
                qi.cbSize = sizeof(qi);
                uint64_t before = SUCCEEDED(SHQueryRecycleBinW(nullptr, &qi)) ? (uint64_t)qi.i64Size : 0;
                uint64_t items = (uint64_t)qi.i64NumItems;
                if (before && SUCCEEDED(SHEmptyRecycleBinW(nullptr, nullptr, SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND))) {
                    done->freed += before;
                    done->files += items;
                }
            } else {
                for (auto& t : TargetsFor(id)) {
                    if (s_cancel) break;
                    if (t.admin && !admin) continue;
                    if (!t.proc.empty() && running.count(t.proc)) continue;   // that app is open
                    if (!DirExists(t.dir)) continue;
                    Stats st;
                    uint64_t base = done->freed;
                    Walk(Long(t.dir), t, Cutoff(t.minAgeHours), true, st, 0, [&] { done->freed = base + st.bytes; post(false); });
                    done->freed = base + st.bytes;
                    done->files += st.files;
                    done->skipped += st.skipped;
                }
            }
            ++index;
        }
        done->cancelled = s_cancel;
        Log(L"cleanup: freed %ls in %llu files (%llu in use)%ls", FormatBytes(done->freed).c_str(), (unsigned long long)done->files,
            (unsigned long long)done->skipped, done->cancelled ? L", cancelled" : L"");
        CoUninitialize();
        s_busy = false;
        if (!PostMessageW(notify, WM_HELM_CLEAN_DONE, 0, (LPARAM)done)) delete done;
    }).detach();
}

} // namespace cleanup
