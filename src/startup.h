// Helm - apps that start with Windows.
//
// Helm turns entries on and off exactly the way Task Manager does (the StartupApproved keys), so
// nothing is deleted, every change can be undone, and Task Manager shows the same state.
#pragma once
#include "common.h"

struct StartupEntry {
    std::wstring key;            // stable id: "<source>|<name>"
    std::wstring name;           // value or shortcut name
    std::wstring command;        // what runs
    std::wstring exe;            // the program, when it could be worked out
    std::wstring publisher, description;
    std::wstring source;         // "Registry", "Registry (all users)", "Startup folder"…
    int sourceId = 0;            // internal
    bool enabled = true;
    bool allUsers = false;       // changing it needs administrator rights
    bool missing = false;        // the program isn't there any more
};

struct StartupList {
    std::vector<StartupEntry> entries;   // sorted by name
};

namespace startup {

void Load(HWND notify);                              // background; posts WM_HELM_STARTUP
// Turns one entry on or off. Runs on the calling thread (a registry write). False + message on failure.
bool SetEnabled(const StartupEntry& e, bool enabled, std::wstring* error);

} // namespace startup
