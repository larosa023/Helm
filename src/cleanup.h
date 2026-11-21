// Helm - junk cleanup: temporary files, caches, crash dumps, old update downloads, the Recycle Bin.
//
// Safety rules:
//   - only known cache/temp folders, resolved from Windows' own known-folder paths; never a drive
//     root, a profile folder or the Windows folder itself
//   - links (junctions, symlinks) are never followed, and linked files are never deleted
//   - temporary files must be at least a day old, so nothing an installer is using right now goes
//   - a cache that belongs to an app that's open (a browser, Discord…) is skipped until it's closed
//   - files that are in use are skipped; nothing is forced
#pragma once
#include "common.h"
#include <atomic>

struct CleanCategory {
    std::wstring id, name, description;
    wchar_t glyph = 0;
    bool defaultOn = true;
    bool recycleBin = false;      // special: empties the Recycle Bin
    std::wstring note;            // extra caution shown under the description
};

struct CleanScanItem {
    std::wstring id;
    bool available = false;       // anything to look at on this PC (e.g. Firefox is installed)
    uint64_t bytes = 0;
    uint64_t files = 0;
    bool needsAdmin = false;      // some or all of it needs Helm to run as administrator
    bool adminOnly = false;       // all of it does
    std::vector<std::wstring> blockedBy;   // apps that must be closed first ("Chrome")
};

struct CleanScanResult {
    std::vector<CleanScanItem> items;   // same order as CleanCategories()
    bool cancelled = false;
};

struct CleanProgress {
    std::wstring current;         // category being cleaned
    uint64_t freed = 0;
    int percent = 0;
};

struct CleanDone {
    uint64_t freed = 0;
    uint64_t files = 0;
    uint64_t skipped = 0;         // files in use (left alone)
    bool cancelled = false;
};

const std::vector<CleanCategory>& CleanCategories();

namespace cleanup {

void Scan(HWND notify);                                   // posts WM_HELM_CLEAN_SCAN
void Clean(HWND notify, const std::vector<std::wstring>& ids);   // posts WM_HELM_CLEAN_PROGRESS / _DONE
void Cancel();
bool Busy();

} // namespace cleanup
