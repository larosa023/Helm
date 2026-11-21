// Helm - Storage page: what's using disk space
#include "ui_internal.h"
#include "storage.h"
#include "sysinfo.h"
#include "util.h"
#include <shobjidl.h>
#include <memory>
#include <algorithm>

static std::shared_ptr<StorageTree> g_tree;
static StorageNode* g_cur = nullptr;
static bool g_scanning = false;
static StorageProgress g_prog;
static std::wstring g_scanRoot;
static int g_tab = 0;                    // 0 folders and files, 1 largest files
static std::vector<StorageNode*> g_crumbs;

bool StorageBusy() { return g_scanning || storage::Busy(); }

void StorageScanDrive(const std::wstring& root) {
    if (StorageBusy()) {
        if (!IEquals(root, g_scanRoot)) Toast(L"Helm is still looking through " + g_scanRoot + L".", true);
        return;
    }
    g_scanRoot = root;
    g_scanning = true;
    g_prog = StorageProgress{};
    g_crumbs.clear();
    g_tree.reset();
    g_cur = nullptr;
    g_tab = 0;
    g_scrollTarget = 0;
    storage::Scan(g_hwnd, root);
    StartAnim();
}

void StorageOnProgress(StorageProgress* p) {
    if (!p->done) { g_prog = *p; return; }
    g_scanning = false;
    g_prog = *p;
    if (p->cancelled || !p->tree) return;
    g_tree = p->tree;
    g_cur = g_tree->root.get();
    g_scroll = g_scrollTarget = 0;
}

static void Enter(StorageNode* n) {
    if (!n) return;
    g_cur = n;
    g_scroll = g_scrollTarget = 0;
    StartAnim();
}

static void Up() {
    if (g_cur && g_cur->parent) Enter(g_cur->parent);
}

bool KeyStorage(WPARAM vk) {
    if (vk == VK_BACK || vk == VK_ESCAPE || vk == VK_BROWSER_BACK) {
        if (g_cur && g_cur->parent) { Up(); return true; }
    }
    return false;
}

static std::wstring PickFolder() {
    std::wstring out;
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return out;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dlg->SetTitle(L"Pick a folder to analyze");
    if (SUCCEEDED(dlg->Show(g_hwnd))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            PWSTR p = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) { out = p; CoTaskMemFree(p); }
            item->Release();
        }
    }
    dlg->Release();
    return out;
}

static std::wstring Percent(uint64_t part, uint64_t whole) {
    if (!whole) return L"";
    double p = 100.0 * part / whole;
    wchar_t b[16];
    if (p < 0.1) return L"<0.1%";
    swprintf(b, 16, p < 10 ? L"%.1f%%" : L"%.0f%%", p);
    return b;
}

static std::wstring Count(uint64_t n) {
    std::wstring s = std::to_wstring(n);
    for (int i = (int)s.size() - 3; i > 0; i -= 3) s.insert(i, L",");
    return s;
}

// "…\Users\Anthony\AppData\Local\Temp" when it's too long
static std::wstring TailPath(const std::wstring& p, size_t max) {
    if (p.size() <= max) return p;
    size_t cut = p.size() - max;
    size_t slash = p.find(L'\\', cut);
    return L"…" + p.substr(slash == std::wstring::npos ? cut : slash);
}

static void Row(float left, float right, float y, float h, int index, Hit kind, wchar_t glyph, const std::wstring& name, const std::wstring& detail,
                uint64_t size, uint64_t whole, bool clickable, bool openButton, int openArg) {
    bool hot = clickable && (IsHover(kind, index) || IsHover(Hit::StOpen, index, openArg));
    float hv = Anim(AKey(4000 + (int)kind, index, openArg), hot ? 1.0f : 0.0f, 0.3f);
    D2D1_RECT_F row = D2D1::RectF(left + 4, y + 2, right - 4, y + h - 2);
    if (hv > 0.01f) g_c.FillRound(row, 8, Alpha(g_t.surfaceHover, hv));
    Glyph(glyph, 15, D2D1::RectF(left + 14, y, left + 44, y + h), glyph == icon::Folder ? g_t.accent : g_t.subtext);
    float sizeW = 90, pctW = 56, barW = std::min(200.0f, (right - left) * 0.22f);
    float xSize = right - 16 - 40 - sizeW;   // the open button's column is always kept, so rows line up
    float xBar = xSize - 16 - barW;
    float xPct = xBar - 8 - pctW;
    Text(name, 13.5f, D2D1::RectF(left + 52, y + (detail.empty() ? 0 : 6), xPct - 8, detail.empty() ? y + h : y + 26), g_t.text);
    if (!detail.empty()) Text(detail, 11.5f, D2D1::RectF(left + 52, y + 25, xPct - 8, y + h - 6), g_t.subtext);
    Text(Percent(size, whole), 12, D2D1::RectF(xPct, y, xPct + pctW, y + h), g_t.subtext, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING);
    float frac = whole ? (float)((double)size / whole) : 0;
    g_c.FillRound(D2D1::RectF(xBar, y + h / 2 - 3, xBar + barW, y + h / 2 + 3), 3, g_t.surfaceStrong);
    if (frac > 0) g_c.FillRound(D2D1::RectF(xBar, y + h / 2 - 3, xBar + std::max(3.0f, barW * std::min(1.0f, frac)), y + h / 2 + 3), 3,
                                glyph == icon::Folder ? g_t.accent : g_t.info);
    Text(FormatBytes(size), 13, D2D1::RectF(xSize, y, xSize + sizeW, y + h), g_t.text, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_TRAILING);
    if (clickable) AddHot(row, kind, index);
    if (openButton) {
        D2D1_RECT_F b = D2D1::RectF(right - 46, y + (h - 30) / 2, right - 14, y + (h + 30) / 2);
        if (hv > 0.01f || IsHover(Hit::StOpen, index, openArg)) Button(b, L"", icon::OpenNew, Btn::Subtle, Hit::StOpen, index, openArg);
        else AddHot(b, Hit::StOpen, index, openArg);
    }
}

void DrawStorage(float x0) {
    PageBox L = PageLayout(x0, 1040);
    float left = L.left, right = L.right;
    float y = 24 - g_scroll;
    const auto& drives = Drives().drives;
    std::wstring sub;
    if (g_tree && g_tree->root) {
        sub = g_tree->root->name + L" · " + FormatBytes(g_tree->root->size) + L" in " + Count(g_tree->root->files) + L" files";
        if (g_tree->driveTotal) sub += L" · " + FormatBytes(g_tree->driveFree) + L" free of " + FormatBytes(g_tree->driveTotal);
    } else if (!g_scanning) {
        sub = L"See which folders and files take up the most space.";
    } else {
        sub = L"Looking through " + g_scanRoot;
    }
    float ty = y;
    y = PageTitle(left, right, y, L"Storage", sub);
    {
        float bx = ButtonRight(right, ty + 6, L"Folder…", icon::Folder, Btn::Secondary, Hit::StScan, -2, 0, !g_scanning);
        // one chip per drive
        for (int i = (int)drives.size() - 1; i >= 0; --i) {
            std::wstring l = drives[i].root.substr(0, 2);
            bool cur = g_tree && g_tree->root && IEquals(g_tree->root->name, drives[i].root);
            bx = ButtonRight(bx - 8, ty + 6, l, icon::Drive, cur ? Btn::Primary : Btn::Secondary, Hit::StDrive, i, 0, !g_scanning);
        }
    }

    // ---- scanning
    if (g_scanning) {
        D2D1_RECT_F r = D2D1::RectF(left, y, right, y + 128);
        Card(r);
        Text(L"Looking through " + g_scanRoot + L"…", 15, D2D1::RectF(left + 20, y + 16, right - 140, y + 40), g_t.text, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        Text(Count(g_prog.files) + L" files · " + FormatBytes(g_prog.bytes) + L" so far", 13, D2D1::RectF(left + 20, y + 42, right - 140, y + 62), g_t.subtext);
        Text(TailPath(g_prog.current, 110), 12, D2D1::RectF(left + 20, y + 66, right - 20, y + 86), g_t.faint);
        ProgressBar(D2D1::RectF(left + 20, y + 102, right - 20, y + 108), -1, g_t.accent);
        ButtonRight(right - 20, y + 16, L"Stop", 0, Btn::Secondary, Hit::StCancel);
        y += 148;
        g_contentH = y + g_scroll;
        return;
    }

    // ---- nothing scanned yet
    if (!g_tree || !g_cur) {
        if (g_prog.cancelled) { y += Banner(left, right, y, icon::Info, g_t.info, L"Stopped.") + 16; }
        float rowH = 64;
        float h = std::max(1.0f, (float)drives.size()) * rowH + 8;
        D2D1_RECT_F card = D2D1::RectF(left, y, right, y + h);
        Card(card);
        if (drives.empty()) Text(L"Reading drives…", 13, D2D1::RectF(left + 20, y, right, y + h), g_t.subtext);
        float ry = y + 4;
        for (size_t i = 0; i < drives.size(); ++i) {
            const DriveInfo& d = drives[i];
            if (i) Divider(left + 18, right - 18, ry);
            Glyph(icon::Drive, 18, D2D1::RectF(left + 16, ry, left + 48, ry + rowH), g_t.subtext);
            std::wstring name = d.root.substr(0, 2) + (d.label.empty() ? L" Local disk" : L" " + d.label);
            float used = d.total ? 100.0f * (float)(d.total - d.free) / (float)d.total : 0;
            float br = right - 18 - ButtonWidth(L"Analyze", 0) - 16;
            Text(name, 13.5f, D2D1::RectF(left + 58, ry + 10, br - 170, ry + 30), g_t.text, DWRITE_FONT_WEIGHT_SEMI_BOLD);
            Text(FormatBytes(d.total - d.free) + L" used of " + FormatBytes(d.total), 12.5f, D2D1::RectF(br - 260, ry + 10, br - 8, ry + 30), g_t.subtext,
                 DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING);
            ProgressBar(D2D1::RectF(left + 58, ry + 38, br - 8, ry + 44), used, used >= 90 ? g_t.bad : used >= 80 ? g_t.warn : g_t.accent);
            ButtonRight(right - 18, ry + 16, L"Analyze", 0, Btn::Primary, Hit::StDrive, (int)i);
            ry += rowH;
        }
        y += h + 20;
        TextWrap(L"Helm reads folder sizes only. It doesn't change or delete anything here; open a folder in Explorer to tidy it up.", 12,
                 D2D1::RectF(left + 2, y, right, y + 40), g_t.faint);
        y += 50;
        g_contentH = y + g_scroll;
        return;
    }

    // ---- results: breadcrumb
    g_crumbs.clear();
    for (StorageNode* n = g_cur; n; n = n->parent) g_crumbs.insert(g_crumbs.begin(), n);
    {
        float cx = left;
        Button(D2D1::RectF(cx, y, cx + 34, y + 32), L"", icon::Up, Btn::Secondary, Hit::StUp, -1, 0, g_cur->parent != nullptr);
        cx += 44;
        float maxX = right - 190;
        // show the last crumbs that fit
        size_t first = 0;
        float need = 0;
        for (size_t i = g_crumbs.size(); i-- > 0;) {
            need += MeasureWidth(g_crumbs[i]->name, Format(13)) + 34;
            if (cx + need > maxX && i + 1 < g_crumbs.size()) { first = i + 1; break; }
        }
        if (first > 0) { Text(L"…", 13, D2D1::RectF(cx, y, cx + 16, y + 32), g_t.subtext); cx += 20; }
        for (size_t i = first; i < g_crumbs.size(); ++i) {
            bool last = i + 1 == g_crumbs.size();
            std::wstring n = g_crumbs[i]->name;
            float w = std::min(MeasureWidth(n, Format(13, last ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL)) + 16, maxX - cx);
            if (w < 20) break;
            D2D1_RECT_F r = D2D1::RectF(cx, y, cx + w, y + 32);
            if (!last && IsHover(Hit::StCrumb, (int)i)) g_c.FillRound(r, 6, g_t.surfaceHover);
            Text(n, 13, D2D1::RectF(r.left + 8, r.top, r.right - 4, r.bottom), last ? g_t.text : g_t.subtext,
                 last ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL);
            if (!last) {
                AddHot(r, Hit::StCrumb, (int)i);
                Glyph(icon::ChevronRight, 9, D2D1::RectF(cx + w, y, cx + w + 16, y + 32), g_t.faint);
            }
            cx += w + 18;
        }
        Segmented(right, y, {L"Folders", L"Largest files"}, g_tab, Hit::StTab, true);
        y += 46;
    }

    float rowH = 46;
    if (g_tab == 0) {
        StorageNode* cur = g_cur;
        uint64_t whole = std::max<uint64_t>(1, cur->size);
        // summary line
        Text(FormatBytes(cur->size) + L" in " + Count(cur->files) + L" files", 13, D2D1::RectF(left + 2, y, right - 160, y + 24), g_t.subtext);
        ButtonRight(right, y - 4, L"Open in Explorer", icon::OpenNew, Btn::Subtle, Hit::StOpen, -1, 2);
        y += 32;
        size_t nRows = cur->children.size() + (cur->smallCount ? 1 : 0) + (cur->looseBytes ? 1 : 0) + cur->topFiles.size();
        float h = std::max<float>(rowH, nRows * rowH) + 8;
        D2D1_RECT_F card = D2D1::RectF(left, y, right, y + h);
        Card(card);
        float ry = y + 4;
        if (nRows == 0) Text(cur->denied ? L"Windows didn't let Helm look inside this folder." : L"This folder is empty.", 13,
                             D2D1::RectF(left + 20, ry, right - 20, ry + rowH), g_t.subtext);
        for (size_t i = 0; i < cur->children.size(); ++i, ry += rowH) {
            if (!Visible(ry, ry + rowH)) continue;
            StorageNode* c = cur->children[i].get();
            std::wstring detail = c->denied ? L"Couldn't be read" : Count(c->files) + (c->files == 1 ? L" file" : L" files");
            Row(left, right, ry, rowH, (int)i, Hit::StFolder, icon::Folder, c->name, detail, c->size, whole, true, true, 0);
        }
        if (cur->smallCount) {
            if (Visible(ry, ry + rowH))
                Row(left, right, ry, rowH, -1, Hit::None, icon::Folder, std::to_wstring(cur->smallCount) + (cur->smallCount == 1 ? L" small folder" : L" small folders"),
                    L"Each under 1 MB", cur->smallBytes, whole, false, false, 0);
            ry += rowH;
        }
        if (cur->looseBytes) {
            if (Visible(ry, ry + rowH))
                Row(left, right, ry, rowH, -1, Hit::None, icon::Document, L"Files directly in this folder", L"", cur->looseBytes, whole, false, false, 0);
            ry += rowH;
        }
        for (size_t i = 0; i < cur->topFiles.size(); ++i, ry += rowH) {
            if (!Visible(ry, ry + rowH)) continue;
            Row(left + 24, right, ry, rowH, (int)i, Hit::StFile, icon::Document, cur->topFiles[i].name, L"", cur->topFiles[i].size, whole, true, false, 0);
        }
        y += h + 16;
    } else {
        auto& big = g_tree->largest;
        uint64_t whole = std::max<uint64_t>(1, g_tree->root->size);
        Text(L"The biggest files on " + g_tree->root->name + L". Click one to see it in Explorer.", 13, D2D1::RectF(left + 2, y, right, y + 24), g_t.subtext);
        y += 32;
        float h = std::max<float>(rowH, big.size() * (rowH + 4)) + 8;
        D2D1_RECT_F card = D2D1::RectF(left, y, right, y + h);
        Card(card);
        float ry = y + 4;
        if (big.empty()) Text(L"No files over 1 MB.", 13, D2D1::RectF(left + 20, ry, right - 20, ry + rowH), g_t.subtext);
        for (size_t i = 0; i < big.size(); ++i, ry += rowH + 4) {
            if (!Visible(ry, ry + rowH + 4)) continue;
            Row(left, right, ry, rowH + 4, (int)i, Hit::StBig, icon::Document, PathName(big[i].first), TailPath(PathParent(big[i].first), 90),
                big[i].second, whole, true, false, 0);
        }
        y += h + 16;
    }
    if (g_tree->deniedFolders) {
        std::wstring t = std::to_wstring(g_tree->deniedFolders) + (g_tree->deniedFolders == 1 ? L" folder" : L" folders") +
                         L" couldn't be read, so sizes may be a little low.";
        if (!IsElevated()) y += Banner(left, right, y, icon::Lock, g_t.info, t + L" Running as administrator reads more.", L"Restart as administrator", Hit::Elevate, -1, icon::Admin) + 12;
        else { Text(t, 12, D2D1::RectF(left + 2, y, right, y + 20), g_t.faint); y += 28; }
    }
    std::wstring took = g_tree->seconds < 1 ? L"under a second" : std::to_wstring((int)(g_tree->seconds + 0.5)) + L" s";
    TextWrap(L"Looked through " + Count(g_tree->root->files) + L" files in " + took +
             L". Sizes are file sizes; Windows may count space a little differently.", 12, D2D1::RectF(left + 2, y, right, y + 40), g_t.faint);
    y += 50;
    g_contentH = y + g_scroll;
}

bool ClickStorage(const HotRect& h) {
    switch (h.kind) {
        case Hit::StDrive: {
            const auto& drives = Drives().drives;
            if (h.index >= 0 && h.index < (int)drives.size()) StorageScanDrive(drives[h.index].root);
            return true;
        }
        case Hit::StScan: {
            std::wstring p = PickFolder();
            if (!p.empty()) StorageScanDrive(p);
            return true;
        }
        case Hit::StCancel: storage::Cancel(); return true;
        case Hit::StUp: Up(); return true;
        case Hit::StCrumb:
            if (h.index >= 0 && h.index < (int)g_crumbs.size()) Enter(g_crumbs[h.index]);
            return true;
        case Hit::StTab: g_tab = h.arg == 1 ? 1 : 0; g_scroll = g_scrollTarget = 0; return true;
        case Hit::StFolder:
            if (g_cur && h.index >= 0 && h.index < (int)g_cur->children.size()) Enter(g_cur->children[h.index].get());
            return true;
        case Hit::StOpen:
            if (!g_cur) return true;
            if (h.arg == 2) OpenFolder(g_cur->Path());
            else if (h.index >= 0 && h.index < (int)g_cur->children.size()) OpenFolder(g_cur->children[h.index]->Path());
            return true;
        case Hit::StFile:
            if (g_cur && h.index >= 0 && h.index < (int)g_cur->topFiles.size()) {
                std::wstring dir = g_cur->Path();
                if (!dir.empty() && dir.back() != L'\\') dir += L'\\';
                ShowInFolder(dir + g_cur->topFiles[h.index].name);
            }
            return true;
        case Hit::StBig:
            if (g_tree && h.index >= 0 && h.index < (int)g_tree->largest.size()) ShowInFolder(g_tree->largest[h.index].first);
            return true;
        default: return false;
    }
}
