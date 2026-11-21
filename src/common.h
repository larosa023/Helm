// Helm - keep your PC fast, clean and private
#pragma once
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>
#include <vector>
#include <cstdint>

#ifndef HELM_VERSION
#define HELM_VERSION L"0.1.0"
#endif
#define HELM_REPO_OWNER L"larosa023"
#define HELM_REPO_NAME L"Helm"
#define HELM_WINDOW_CLASS L"Helm.larosa023.Main"

// Messages posted from worker threads to the main window. lParam owns a heap object that the
// window procedure takes over and deletes.
enum : UINT {
    WM_HELM_ACTIVATE = WM_APP + 1,   // another Helm.exe asked us to come to the front
    WM_HELM_SELF,                    // lParam: SelfUpdateState*
    WM_HELM_SPECS,                   // lParam: Specs*
    WM_HELM_SAMPLE,                  // lParam: Sample*
    WM_HELM_CLEAN_SCAN,              // lParam: CleanScanResult*
    WM_HELM_CLEAN_PROGRESS,          // lParam: CleanProgress*
    WM_HELM_CLEAN_DONE,              // lParam: CleanDone*
    WM_HELM_STORAGE,                 // lParam: StorageProgress* (progress and the finished tree)
    WM_HELM_STARTUP,                 // lParam: StartupList*
    WM_HELM_ICON,                    // lParam: IconResult*
    WM_HELM_TOOL,                    // wParam: tool index, lParam: 1 if it worked (quiet repair tools)
    WM_HELM_SUITE,                   // lParam: std::vector<SuiteApp>*
    WM_HELM_DRIVES,                  // lParam: DriveList*
    WM_HELM_HEALTH,                  // lParam: HealthInfo*
};
