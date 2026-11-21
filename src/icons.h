// Helm - program icons, extracted in the background as premultiplied BGRA pixels
#pragma once
#include "common.h"

struct IconResult {
    std::wstring key;              // what was asked for (lowercase path)
    UINT w = 0, h = 0;
    std::vector<uint8_t> pixels;   // premultiplied BGRA, empty if there's no icon
};

namespace icons {

// Posts WM_HELM_ICON with IconResult* (also when there's no icon, so the caller can stop waiting).
void Request(HWND notify, const std::wstring& path, UINT px);

} // namespace icons
