// Helm - program icons
#include "icons.h"
#include "util.h"
#include <wincodec.h>
#include <shellapi.h>
#include <shlobj.h>
#include <thread>
#include <mutex>
#include <deque>
#include <condition_variable>

namespace icons {

struct Job { HWND notify; std::wstring path; UINT px; };

static std::mutex s_lock;
static std::condition_variable s_cv;
static std::deque<Job> s_queue;
static bool s_running = false;

static bool Extract(const std::wstring& path, UINT px, IconResult& out) {
    HICON icon = nullptr;
    // SHDefExtractIcon picks the best size the file has; low word = the size we want
    if (SHDefExtractIconW(path.c_str(), 0, 0, &icon, nullptr, MAKELONG(px, 0)) != S_OK || !icon) {
        SHFILEINFOW sfi{};
        if (SHGetFileInfoW(path.c_str(), 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_LARGEICON) && sfi.hIcon) icon = sfi.hIcon;
    }
    if (!icon) return false;
    bool ok = false;
    IWICImagingFactory* wic = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) {
        IWICBitmap* bmp = nullptr;
        if (SUCCEEDED(wic->CreateBitmapFromHICON(icon, &bmp))) {
            IWICFormatConverter* conv = nullptr;
            if (SUCCEEDED(wic->CreateFormatConverter(&conv)) &&
                SUCCEEDED(conv->Initialize(bmp, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) {
                UINT w = 0, h = 0;
                conv->GetSize(&w, &h);
                if (w && h && w <= 512 && h <= 512) {
                    out.pixels.resize((size_t)w * h * 4);
                    if (SUCCEEDED(conv->CopyPixels(nullptr, w * 4, (UINT)out.pixels.size(), out.pixels.data()))) {
                        out.w = w;
                        out.h = h;
                        ok = true;
                    } else {
                        out.pixels.clear();
                    }
                }
            }
            if (conv) conv->Release();
            bmp->Release();
        }
        wic->Release();
    }
    DestroyIcon(icon);
    return ok;
}

void Request(HWND notify, const std::wstring& path, UINT px) {
    std::lock_guard<std::mutex> g(s_lock);
    s_queue.push_back({notify, path, px});
    if (s_running) { s_cv.notify_one(); return; }
    s_running = true;
    // one worker for all icons; it exits after a few idle seconds
    std::thread([] {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        for (;;) {
            Job job;
            {
                std::unique_lock<std::mutex> lk(s_lock);
                if (!s_cv.wait_for(lk, std::chrono::seconds(5), [] { return !s_queue.empty(); })) { s_running = false; break; }
                job = s_queue.front();
                s_queue.pop_front();
            }
            auto* r = new IconResult;
            r->key = Lower(job.path);
            if (FileExists(job.path)) Extract(job.path, job.px, *r);
            if (!PostMessageW(job.notify, WM_HELM_ICON, 0, (LPARAM)r)) delete r;
        }
        CoUninitialize();
    }).detach();
}

} // namespace icons
