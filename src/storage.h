// Helm - what's using your disk space. Walks a drive (or folder) and keeps a tree of folder sizes.
//
// To keep memory small on big drives, folders under 1 MB are folded into their parent as soon as
// their size is known, and each folder remembers only its largest files.
#pragma once
#include "common.h"
#include <memory>

struct StorageFile {
    std::wstring name;
    uint64_t size = 0;
};

struct StorageNode {
    std::wstring name;                 // folder name ("C:\" for the root)
    uint64_t size = 0;                 // everything inside, all levels
    uint64_t files = 0;                // file count, all levels
    uint64_t smallBytes = 0;           // folders under 1 MB folded in here
    uint32_t smallCount = 0;
    uint64_t looseBytes = 0;           // files directly in this folder
    bool denied = false;               // couldn't be read
    StorageNode* parent = nullptr;
    std::vector<std::unique_ptr<StorageNode>> children;   // largest first
    std::vector<StorageFile> topFiles; // largest files directly in this folder (up to 25)
    std::wstring Path() const;         // full path
};

struct StorageTree {
    std::unique_ptr<StorageNode> root;
    std::vector<std::pair<std::wstring, uint64_t>> largest;   // biggest files anywhere (path, size), up to 60
    uint64_t deniedFolders = 0;
    uint64_t driveTotal = 0, driveFree = 0;
    double seconds = 0;
};

struct StorageProgress {
    bool done = false;
    bool cancelled = false;
    uint64_t bytes = 0, files = 0, folders = 0;
    std::wstring current;
    std::shared_ptr<StorageTree> tree;     // when done
};

namespace storage {

void Scan(HWND notify, const std::wstring& root);   // posts WM_HELM_STORAGE (progress, then done)
void Cancel();
bool Busy();

} // namespace storage
