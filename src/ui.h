// Helm - main window
#pragma once
#include "common.h"
#include "selfupdate.h"

struct StartOptions {
    selfupdate::AfterUpdate after;
    std::wstring page;          // --page <id>
    bool elevatedRelaunch = false;
};

HWND UiCreate(HINSTANCE inst, int show, const StartOptions& opts);
