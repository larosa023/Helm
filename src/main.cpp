// Helm - keep your PC fast, clean and private
//
//   Helm.exe                      open Helm (or bring the running one to the front)
//   Helm.exe --page <id>          open on a page: overview, cleanup, storage, startup, tweaks, tools, themes, settings
//   Helm.exe --quit               close a running Helm (used by Hangar before it updates Helm)
//   Helm.exe --version            exit with code 0 straight away (smoke tests)
#include "common.h"
#include "ui.h"
#include "util.h"
#include "config.h"
#include "net.h"
#include "selfupdate.h"
#include <objbase.h>
#include <shellapi.h>
#include <shobjidl.h>

static HANDLE g_mutex;
static const wchar_t* kMutexName = L"Local\\Helm.larosa023.SingleInstance";

void ReleaseInstanceMutex() {
    if (g_mutex) { ReleaseMutex(g_mutex); CloseHandle(g_mutex); g_mutex = nullptr; }
}
void ReacquireInstanceMutex() {
    if (!g_mutex) g_mutex = CreateMutexW(nullptr, TRUE, kMutexName);
}

static std::wstring ArgValue(const wchar_t* cmdLine, const wchar_t* name) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(cmdLine && *cmdLine ? cmdLine : L"x", &argc);
    std::wstring v;
    if (!argv) return v;
    for (int i = 0; i + 1 < argc; ++i) if (wcscmp(argv[i], name) == 0) v = argv[i + 1];
    LocalFree(argv);
    return v;
}

static bool HasArg(const wchar_t* cmdLine, const wchar_t* name) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(cmdLine && *cmdLine ? cmdLine : L"x", &argc);
    bool found = false;
    if (!argv) return false;
    for (int i = 0; i < argc; ++i) if (wcscmp(argv[i], name) == 0) found = true;
    LocalFree(argv);
    return found;
}

// Waits up to 5 s for the copy that's handing over (after an update, or before restarting as
// administrator) to let go of the single-instance lock.
static bool TakeOverMutex() {
    for (int i = 0; i < 50; ++i) {
        HANDLE m = CreateMutexW(nullptr, TRUE, kMutexName);
        DWORD e = GetLastError();
        if (m && e != ERROR_ALREADY_EXISTS) { g_mutex = m; return true; }
        if (m) CloseHandle(m);
        Sleep(100);
    }
    return false;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    const wchar_t* cmd = GetCommandLineW();
    if (HasArg(cmd, L"--version")) return 0;
    if (HasArg(cmd, L"--quit")) {
        // ask a running Helm to close the normal way, and wait for it
        if (HWND w = FindWindowW(HELM_WINDOW_CLASS, nullptr)) {
            DWORD pid = 0;
            GetWindowThreadProcessId(w, &pid);
            HANDLE p = OpenProcess(SYNCHRONIZE, FALSE, pid);
            PostMessageW(w, WM_CLOSE, 0, 0);
            if (p) { WaitForSingleObject(p, 8000); CloseHandle(p); }
        }
        return 0;
    }
    // no "insert a disk" dialogs when Helm looks at an empty card reader
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);

    StartOptions opts;
    opts.after = selfupdate::ParseArgs(cmd);
    opts.page = ArgValue(cmd, L"--page");
    opts.elevatedRelaunch = HasArg(cmd, L"--elevated");

    g_mutex = CreateMutexW(nullptr, TRUE, kMutexName);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (g_mutex) CloseHandle(g_mutex);
        g_mutex = nullptr;
        if (opts.after.active || opts.elevatedRelaunch) {
            if (!TakeOverMutex()) return 1;   // the old copy didn't let go; it's still running and in charge
        } else {
            if (HWND existing = FindWindowW(HELM_WINDOW_CLASS, nullptr)) {
                DWORD pid = 0;
                GetWindowThreadProcessId(existing, &pid);
                AllowSetForegroundWindow(pid);
                PostMessageW(existing, WM_HELM_ACTIVATE, 0, 0);
            }
            return 0;
        }
    }

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    SetCurrentProcessExplicitAppUserModelID(L"larosa023.Helm");
    LogInit();
    Log(L"==== Helm %ls starting (data: %ls%ls%ls)", HELM_VERSION, DataDir().c_str(), IsPortable() ? L", portable" : L"",
        IsElevated() ? L", administrator" : L"");
    ConfigLoad();
    net::Init();

    HWND hwnd = UiCreate(inst, show, opts);
    if (!hwnd) {
        MessageBoxW(nullptr, L"Helm couldn't start. Details are in helm.log.", L"Helm", MB_ICONERROR);
        ExitProcess(1);
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    Log(L"Helm exiting");
    if (g_mutex) CloseHandle(g_mutex);
    // Background threads (a scan, the monitor) may still be running; ending the process here stops
    // them cleanly instead of tearing down globals underneath them.
    ExitProcess(0);
}
