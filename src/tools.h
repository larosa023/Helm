// Helm - repair tools (Windows' own, run in a window you can watch) and the rest of the suite
#pragma once
#include "common.h"

struct RepairTool {
    std::wstring id, name, description, duration;
    wchar_t glyph = 0;
    bool admin = false;
    bool console = true;          // runs in a console window you can watch; otherwise quietly in the background
    std::wstring command;         // for cmd.exe /c
};

const std::vector<RepairTool>& RepairTools();

struct SuiteApp {
    std::wstring id, name, description, repo;
    wchar_t glyph = 0;
    std::wstring exe;             // where it's installed ("" if not found)
};

namespace tools {

// Starts a console tool in its own window (asking for administrator rights when it needs them).
bool Run(const RepairTool& t, std::wstring* error);
// Runs a quiet tool and waits (call from a worker thread). True if it exited with 0.
bool RunQuiet(const RepairTool& t);

std::vector<SuiteApp> FindSuiteApps();     // reads the disk; call from a worker thread or rarely
std::wstring FindHangar();

} // namespace tools
