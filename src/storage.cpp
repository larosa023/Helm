// Helm - disk space analysis
#include "storage.h"
#include "util.h"
#include <thread>
#include <atomic>
#include <algorithm>
#include <queue>

std::wstring StorageNode::Path() const {
    if (!parent) return name;
    std::wstring p = parent->Path();
    if (!p.empty() && p.back() != L'\\') p += L'\\';
    return p + name;
}

namespace storage {

static std::atomic<bool> s_busy{false};
static std::atomic<bool> s_cancel{false};

bool Busy() { return s_busy; }
void Cancel() { s_cancel = true; }

static const uint64_t kFoldBelow = 1024ull * 1024;     // folders smaller than this are folded into the parent
static const size_t kTopFiles = 25;
static const size_t kLargest = 60;

struct Walker {
    HWND notify;
    std::atomic<bool>& cancel;
    uint64_t bytes = 0, files = 0, folders = 0, denied = 0;
    DWORD lastPost = 0;
    // min-heap of the largest files anywhere
    typedef std::pair<uint64_t, std::wstring> Big;
    std::priority_queue<Big, std::vector<Big>, std::greater<Big>> largest;

    void Post(const std::wstring& current) {
        DWORD now = GetTickCount();
        if (now - lastPost < 120) return;
        lastPost = now;
        auto* p = new StorageProgress;
        p->bytes = bytes;
        p->files = files;
        p->folders = folders;
        p->current = current;
        if (!PostMessageW(notify, WM_HELM_STORAGE, 0, (LPARAM)p)) delete p;
    }

    // `path` is the plain path; `lp` the same with the \\?\ prefix for long paths
    void Walk(StorageNode* node, const std::wstring& path, int depth) {
        if (cancel || depth > 200) return;
        ++folders;
        Post(path);
        // long-path form: \\?\C:\... for drives, \\?\UNC\server\share\... for network folders
        std::wstring lp = IStartsWith(path, L"\\\\?\\") ? path
                        : IStartsWith(path, L"\\\\") ? L"\\\\?\\UNC\\" + path.substr(2)
                        : L"\\\\?\\" + path;
        if (lp.back() != L'\\') lp += L'\\';
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileExW((lp + L"*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
        if (h == INVALID_HANDLE_VALUE) {
            DWORD e = GetLastError();
            if (e == ERROR_ACCESS_DENIED) { node->denied = true; ++denied; }
            return;
        }
        std::wstring base = path;
        if (base.back() != L'\\') base += L'\\';
        do {
            if (cancel) break;
            const wchar_t* n = fd.cFileName;
            if (!wcscmp(n, L".") || !wcscmp(n, L"..")) continue;
            // links (junctions like "Documents and Settings", symlinks, OneDrive's reparse folders that
            // aren't real directories) would count things twice or wander off the drive
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    // cloud placeholders are reparse files too; count their local size only if they have one
                    if (!(fd.dwFileAttributes & (FILE_ATTRIBUTE_OFFLINE | 0x00400000 /*RECALL_ON_DATA_ACCESS*/ | 0x00040000 /*RECALL_ON_OPEN*/))) {
                        uint64_t sz = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
                        AddFile(node, base, n, sz);
                    }
                }
                continue;
            }
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                auto child = std::make_unique<StorageNode>();
                child->name = n;
                child->parent = node;
                StorageNode* c = child.get();
                node->children.push_back(std::move(child));
                Walk(c, base + n, depth + 1);
                continue;
            }
            uint64_t sz = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
            AddFile(node, base, n, sz);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        Finish(node);
    }

    void AddFile(StorageNode* node, const std::wstring& base, const wchar_t* name, uint64_t sz) {
        node->looseBytes += sz;
        node->files++;
        bytes += sz;
        files++;
        if (sz >= 1024 * 1024) {
            auto& tf = node->topFiles;
            if (tf.size() < kTopFiles || sz > tf.back().size) {
                StorageFile f{name, sz};
                auto it = std::upper_bound(tf.begin(), tf.end(), f, [](const StorageFile& a, const StorageFile& b) { return a.size > b.size; });
                tf.insert(it, f);
                if (tf.size() > kTopFiles) tf.pop_back();
            }
            if (largest.size() < kLargest) largest.emplace(sz, base + name);
            else if (sz > largest.top().first) { largest.pop(); largest.emplace(sz, base + name); }
        }
    }

    // sizes are final: add up, sort, fold small folders
    void Finish(StorageNode* node) {
        uint64_t size = node->looseBytes, count = node->files;
        for (auto& c : node->children) { size += c->size; count += c->files; }
        // node->files so far counted only loose files; make it the total
        node->size = size;
        node->files = count;
        std::vector<std::unique_ptr<StorageNode>> keep;
        for (auto& c : node->children) {
            if (c->size < kFoldBelow && !c->denied) { node->smallBytes += c->size; node->smallCount++; }
            else keep.push_back(std::move(c));
        }
        std::sort(keep.begin(), keep.end(), [](const std::unique_ptr<StorageNode>& a, const std::unique_ptr<StorageNode>& b) { return a->size > b->size; });
        node->children = std::move(keep);
        node->children.shrink_to_fit();
        node->topFiles.shrink_to_fit();
    }
};

void Scan(HWND notify, const std::wstring& root0) {
    if (s_busy.exchange(true)) return;
    s_cancel = false;
    std::wstring root = root0;
    std::thread([notify, root] {
        ULONGLONG t0 = GetTickCount64();
        auto tree = std::make_shared<StorageTree>();
        tree->root = std::make_unique<StorageNode>();
        tree->root->name = root;
        ULARGE_INTEGER fc, total, fr;
        if (GetDiskFreeSpaceExW(root.c_str(), &fc, &total, &fr)) { tree->driveTotal = total.QuadPart; tree->driveFree = fr.QuadPart; }
        Walker w{notify, s_cancel};
        std::wstring start = root;
        if (start.size() > 3 && start.back() == L'\\') start.pop_back();
        w.Walk(tree->root.get(), start, 0);
        while (!w.largest.empty()) { tree->largest.emplace_back(w.largest.top().second, w.largest.top().first); w.largest.pop(); }
        std::reverse(tree->largest.begin(), tree->largest.end());
        tree->deniedFolders = w.denied;
        tree->seconds = (GetTickCount64() - t0) / 1000.0;
        auto* p = new StorageProgress;
        p->done = true;
        p->cancelled = s_cancel;
        p->bytes = w.bytes;
        p->files = w.files;
        p->folders = w.folders;
        if (!s_cancel) p->tree = tree;
        Log(L"storage: %ls scanned in %.1f s: %ls in %llu files, %llu folders (%llu unreadable)%ls", root.c_str(), tree->seconds,
            FormatBytes(w.bytes).c_str(), (unsigned long long)w.files, (unsigned long long)w.folders, (unsigned long long)w.denied,
            s_cancel ? L", cancelled" : L"");
        s_busy = false;
        if (!PostMessageW(notify, WM_HELM_STORAGE, 0, (LPARAM)p)) delete p;
    }).detach();
}

} // namespace storage
