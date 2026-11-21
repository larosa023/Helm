// Helm - settings (DataDir()\settings.json)
#pragma once
#include "common.h"
#include <map>

struct Config {
    std::wstring theme = L"dark";       // a built-in theme id (dark, light, system, midnight…) or custom:<id>
    std::wstring accent = L"theme";     // theme | windows | #RRGGBB
    int windowX = CW_USEDEFAULT, windowY = CW_USEDEFAULT, windowW = 0, windowH = 0;
    bool maximized = false;
    std::wstring page = L"overview";    // last page
    // Helm's own updates
    bool helmBeta = false;
    bool helmAuto = true;               // install Helm updates by itself when it won't interrupt
    int checkHours = 6;
    std::wstring skipVersion;           // versions that failed to start or were rolled back (comma-separated)
    // Cleanup
    std::map<std::wstring, bool> cleanPick;   // category id -> checked (only the ones you changed)
    double totalFreed = 0;              // bytes Helm has freed, all time
    long long lastCleanAt = 0;          // unix seconds
    bool autoScan = true;               // look for junk when Helm starts
};

extern Config g_cfg;
void ConfigLoad();
void ConfigSave();
bool IsSkippedVersion(const std::wstring& v);
void SkipVersion(const std::wstring& v);
void PruneSkippedVersions(const std::wstring& current);   // forget skips at or below the running version
