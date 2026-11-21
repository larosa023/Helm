// Helm - GitHub releases: parsing, the suite's release index, checksum files
#pragma once
#include "common.h"

struct Asset {
    std::wstring name, url, sha256;   // sha256 from GitHub's asset digest (may be empty)
    uint64_t size = 0;
};

struct ReleaseInfo {
    std::wstring tag, version, title, notes, published, page;
    bool prerelease = false;
    std::vector<Asset> assets;
};

// Parses a /repos/{owner}/{repo}/releases array. Skips drafts and tags that aren't versions.
// Sorted newest version first.
bool ParseReleases(const std::string& body, std::vector<ReleaseInfo>& out, std::wstring* error);
const Asset* FindAsset(const ReleaseInfo& r, const std::wstring& name);

// Hangar's release index (releases.json on the Hangar repo's `index` branch) lists the releases of
// every suite app in one file that isn't subject to GitHub's API limit. Gives the releases array
// for one repo as JSON text. False if the index doesn't cover the repo or is stale.
bool IndexReleases(const std::wstring& repo, long long maxAgeSecs, std::string& body);

// Finds an asset's hash in a SHA256SUMS-style file ("<hex>  <name>" lines). "" if absent.
std::wstring ExpectedHashFromSums(const std::string& sums, const std::wstring& assetName);
