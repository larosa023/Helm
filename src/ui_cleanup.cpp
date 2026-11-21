// Helm - Cleanup page
#include "ui_internal.h"
#include "cleanup.h"
#include "config.h"
#include "util.h"
#include <ctime>
#include <algorithm>

static CleanScanResult g_scan;
static bool g_haveScan = false;
static bool g_scanning = false;
static bool g_cleaning = false;
static bool g_quick = false;             // started from the Overview
static CleanProgress g_progress;
static CleanDone g_done;
static bool g_showDone = false;
static bool g_stopping = false;

static const CleanScanItem* Item(const std::wstring& id) {
    for (auto& i : g_scan.items) if (i.id == id) return &i;
    return nullptr;
}

static bool Picked(const CleanCategory& c) {
    auto it = g_cfg.cleanPick.find(c.id);
    return it != g_cfg.cleanPick.end() ? it->second : c.defaultOn;
}

static void SetPicked(const CleanCategory& c, bool on) {
    if (on == c.defaultOn) g_cfg.cleanPick.erase(c.id);
    else g_cfg.cleanPick[c.id] = on;
    ConfigSave();
}

// Can Helm clean this right now (it's there, nothing blocks it, and Helm has the rights)?
static bool Cleanable(const CleanScanItem* it) {
    return it && it->available && it->blockedBy.empty() && !(it->adminOnly && !IsElevated()) && it->bytes > 0;
}

static std::vector<std::wstring> PickedIds() {
    std::vector<std::wstring> ids;
    for (auto& c : CleanCategories())
        if (Picked(c) && Cleanable(Item(c.id))) ids.push_back(c.id);
    return ids;
}

static uint64_t PickedBytes() {
    uint64_t b = 0;
    for (auto& c : CleanCategories())
        if (Picked(c)) if (const CleanScanItem* it = Item(c.id); Cleanable(it)) b += it->bytes;
    return b;
}

bool CleanupJunk(uint64_t* bytes) {
    if (!g_haveScan) return false;
    *bytes = PickedBytes();
    return true;
}

bool CleanupBusy() { return g_scanning || g_cleaning || cleanup::Busy(); }

void CleanupStartScan() {
    if (CleanupBusy()) return;
    g_scanning = true;
    cleanup::Scan(g_hwnd);
    Invalidate();
}

void CleanupOnScan(CleanScanResult* r) {
    g_scanning = false;
    if (!r->cancelled) { g_scan = *r; g_haveScan = true; }
}

void CleanupOnProgress(CleanProgress* p) { g_progress = *p; }

void CleanupOnDone(CleanDone* d) {
    g_cleaning = false;
    g_done = *d;
    g_showDone = true;
    g_cfg.totalFreed += (double)d->freed;
    g_cfg.lastCleanAt = (long long)time(nullptr);
    ConfigSave();
    std::wstring msg = d->freed ? L"Freed " + FormatBytes(d->freed) + L"." : L"Nothing needed removing.";
    if (d->cancelled) msg = L"Stopped. Freed " + FormatBytes(d->freed) + L" before that.";
    if (g_quick || g_page != Page::Cleanup) Toast(msg);
    g_quick = false;
    CleanupStartScan();   // show what's left
}

static void StartClean(bool quick) {
    if (CleanupBusy()) return;
    auto ids = PickedIds();
    if (ids.empty()) { Toast(L"Nothing selected can be cleaned right now.", true); return; }
    if (std::find(ids.begin(), ids.end(), L"recycle") != ids.end()) {
        if (MessageBoxW(g_hwnd, L"Empty the Recycle Bin too?\n\nFiles in it can't be restored afterwards.", L"Helm",
                        MB_OKCANCEL | MB_ICONQUESTION) != IDOK) return;
    }
    g_cleaning = true;
    g_stopping = false;
    g_quick = quick;
    g_showDone = false;
    g_progress = CleanProgress{};
    cleanup::Clean(g_hwnd, ids);
    StartAnim();
}

void CleanupQuick() { StartClean(true); }

// ---------------------------------------------------------------- page
static std::wstring JoinNames(const std::vector<std::wstring>& v) {
    std::wstring s;
    for (size_t i = 0; i < v.size(); ++i) s += (i ? (i + 1 == v.size() ? L" and " : L", ") : L"") + v[i];
    return s;
}

void DrawCleanup(float x0) {
    PageBox L = PageLayout(x0, 960);
    float left = L.left, right = L.right;
    float y = 24 - g_scroll;
    uint64_t sel = PickedBytes();
    bool anyPicked = false;
    for (auto& c : CleanCategories()) if (Picked(c)) if (const CleanScanItem* it = Item(c.id); it && it->available) anyPicked = true;
    std::wstring sub = !g_haveScan ? (g_scanning ? L"Looking for junk…" : L"")
                                   : sel ? FormatBytes(sel) + L" selected to remove" : anyPicked ? L"Nothing to remove right now" : L"Nothing selected";
    float ty = y;
    y = PageTitle(left, right, y, L"Cleanup", sub);
    {
        bool busy = CleanupBusy();
        float bx = ButtonRight(right, ty + 6, sel ? L"Clean " + FormatBytes(sel) : L"Clean", icon::Broom, Btn::Primary, Hit::ClClean, -1, 0,
                               !busy && sel > 0);
        ButtonRight(bx - 8, ty + 6, L"Look again", icon::Refresh, Btn::Secondary, Hit::ClRescan, -1, 0, !busy);
    }

    bool needAdmin = false;
    for (auto& i : g_scan.items) needAdmin |= i.needsAdmin;
    if (needAdmin && !IsElevated()) {
        y += Banner(left, right, y, icon::Admin, g_t.info, L"Windows' own temp files, update leftovers, logs and crash dumps need administrator rights.",
                    L"Restart as administrator", Hit::Elevate, -1, icon::Admin) + 14;
    }

    if (g_cleaning) {
        D2D1_RECT_F r = D2D1::RectF(left, y, right, y + 96);
        Card(r);
        Text(g_stopping ? L"Stopping…" : g_progress.current.empty() ? L"Cleaning…" : L"Cleaning " + g_progress.current + L"…", 14, D2D1::RectF(left + 20, y + 16, right - 140, y + 38), g_t.text, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        Text(L"Freed " + FormatBytes(g_progress.freed) + L" so far", 12.5f, D2D1::RectF(left + 20, y + 38, right - 140, y + 58), g_t.subtext);
        float v = Anim(AKey(3000), (float)g_progress.percent, 0.2f);
        ProgressBar(D2D1::RectF(left + 20, y + 70, right - 20, y + 76), g_progress.percent > 0 ? v : -1, g_t.accent);
        ButtonRight(right - 20, y + 14, L"Stop", 0, Btn::Secondary, Hit::ClCancel, -1, 0, !g_stopping);
        y += 96 + 16;
    } else if (g_showDone) {
        std::wstring t = g_done.cancelled ? L"Stopped. Freed " + FormatBytes(g_done.freed) + L"."
                                          : L"Freed " + FormatBytes(g_done.freed) + L" (" + std::to_wstring(g_done.files) + (g_done.files == 1 ? L" file)." : L" files).");
        if (g_done.skipped) t += L" " + std::to_wstring(g_done.skipped) + (g_done.skipped == 1 ? L" file was" : L" files were") + L" in use and left alone.";
        y += Banner(left, right, y, icon::Check, g_t.good, t, L"OK", Hit::ClDismiss) + 16;
    }

    if (!g_haveScan) {
        D2D1_RECT_F r = D2D1::RectF(left, y, right, y + 120);
        Card(r);
        Text(g_scanning ? L"Looking through temp folders and caches…" : L"Helm hasn't looked for junk yet.", 14, D2D1::RectF(left + 20, y + 30, right - 20, y + 54),
             g_t.text, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
        if (g_scanning) ProgressBar(D2D1::RectF(left + (right - left) * 0.3f, y + 70, right - (right - left) * 0.3f, y + 76), -1, g_t.accent);
        else Button(D2D1::RectF((left + right) / 2 - 70, y + 66, (left + right) / 2 + 70, y + 98), L"Look now", icon::Search, Btn::Primary, Hit::ClRescan);
        y += 140;
        g_contentH = y + g_scroll;
        return;
    }

    // select all / none
    {
        float bx = Segmented(left, y, {L"Select all", L"Select none"}, -1, Hit::ClAll);
        (void)bx;
        y += 46;
    }

    const auto& cats = CleanCategories();
    float rowH = 68;
    std::vector<int> shown;
    for (int i = 0; i < (int)cats.size(); ++i) {
        const CleanScanItem* it = Item(cats[i].id);
        if (it && it->available) shown.push_back(i);
    }
    float totalH = 8;
    for (int i : shown) totalH += rowH + (cats[i].note.empty() ? 0 : 18);
    D2D1_RECT_F card = D2D1::RectF(left, y, right, y + totalH);
    Card(card);
    float ry = y + 4;
    bool busy = CleanupBusy();
    for (size_t k = 0; k < shown.size(); ++k) {
        int i = shown[k];
        const CleanCategory& c = cats[i];
        const CleanScanItem* it = Item(c.id);
        float h = rowH + (c.note.empty() ? 0 : 18);
        if (Visible(ry, ry + h)) {
            if (k) Divider(left + 18, right - 18, ry);
            bool adminBlocked = it->adminOnly && !IsElevated();
            bool blocked = !it->blockedBy.empty();
            bool enabled = !busy && !adminBlocked;
            bool picked = Picked(c);
            bool hot = enabled && IsHover(Hit::ClPick, i);
            D2D1_RECT_F row = D2D1::RectF(left + 4, ry + 2, right - 4, ry + h - 2);
            float hv = Anim(AKey(3100, i), hot ? 1.0f : 0.0f, 0.3f);
            if (hv > 0.01f) g_c.FillRound(row, 8, Alpha(g_t.surfaceHover, hv));
            Checkbox(left + 20, ry + 23, picked && !adminBlocked, enabled, hot);
            g_c.FillRound(D2D1::RectF(left + 56, ry + 16, left + 90, ry + 50), 8, Alpha(g_t.accent, 0.12f));
            Glyph(c.glyph, 15, D2D1::RectF(left + 56, ry + 16, left + 90, ry + 50), g_t.accent);
            float textR = right - 190;
            Text(c.name, 14, D2D1::RectF(left + 104, ry + 13, textR, ry + 33), adminBlocked ? g_t.subtext : g_t.text, DWRITE_FONT_WEIGHT_SEMI_BOLD);
            Text(c.description, 12.5f, D2D1::RectF(left + 104, ry + 33, right - 20, ry + 51), g_t.subtext);
            if (!c.note.empty()) {
                Glyph(icon::Info, 11, D2D1::RectF(left + 104, ry + 52, left + 118, ry + 68), g_t.warn);
                Text(c.note, 12, D2D1::RectF(left + 122, ry + 51, right - 20, ry + 69), g_t.warn);
            }
            // right side: size or why it can't be cleaned
            if (adminBlocked) {
                Pill(right - 20, ry + 14, L"Needs administrator", g_t.info, true);
            } else {
                std::wstring size = it->bytes ? FormatBytes(it->bytes) : L"Nothing to remove";
                Text(size, it->bytes ? 15 : 12.5f, D2D1::RectF(right - 200, ry + 12, right - 20, ry + 34), it->bytes ? g_t.text : g_t.faint,
                     it->bytes ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING);
                if (blocked) {
                    Pill(right - 20 - MeasureWidth(size, Format(it->bytes ? 15.0f : 12.5f, it->bytes ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL)) - 10,
                         ry + 13, L"Close " + JoinNames(it->blockedBy) + L" first", g_t.warn, true);
                } else if (it->needsAdmin && !IsElevated()) {
                    Text(L"more as administrator", 11.5f, D2D1::RectF(right - 220, ry + 33, right - 20, ry + 51), g_t.faint, DWRITE_FONT_WEIGHT_NORMAL,
                         DWRITE_TEXT_ALIGNMENT_TRAILING);
                }
            }
            AddHot(row, Hit::ClPick, i, 0, enabled);
        }
        ry += h;
    }
    y += totalH + 22;
    std::wstring foot = L"Helm only removes files that are safe to delete. Files in use are skipped.";
    if (g_cfg.totalFreed > 0) foot = L"Helm has freed " + FormatBytes((uint64_t)g_cfg.totalFreed) + L" so far. " + foot;
    TextWrap(foot, 12, D2D1::RectF(left + 2, y, right, y + 40), g_t.faint);
    y += 50;
    g_contentH = y + g_scroll;
}

bool ClickCleanup(const HotRect& h) {
    switch (h.kind) {
        case Hit::ClPick: {
            const auto& cats = CleanCategories();
            if (h.index < 0 || h.index >= (int)cats.size() || CleanupBusy()) return true;
            SetPicked(cats[h.index], !Picked(cats[h.index]));
            return true;
        }
        case Hit::ClAll:
            if (CleanupBusy()) return true;
            for (auto& c : CleanCategories()) {
                const CleanScanItem* it = Item(c.id);
                bool want = h.arg == 0;
                // "Select all" leaves the Recycle Bin and shader caches to you
                if (want && (c.recycleBin || !c.note.empty())) continue;
                if (it && it->available) SetPicked(c, want);
            }
            return true;
        case Hit::ClClean: StartClean(false); return true;
        case Hit::ClRescan: CleanupStartScan(); return true;
        case Hit::ClCancel: cleanup::Cancel(); g_stopping = true; return true;
        case Hit::ClDismiss: g_showDone = false; return true;
        default: return false;
    }
}
