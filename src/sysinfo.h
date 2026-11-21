// Helm - what's in the PC (read once) and what it's doing (sampled once a second while you look)
#pragma once
#include "common.h"

struct GpuInfo {
    std::wstring name;
    uint64_t vram = 0;            // dedicated video memory, bytes
};

struct Specs {
    std::wstring computer, user;
    std::wstring os;              // "Windows 11 Pro"
    std::wstring osDetail;        // "24H2 · build 26100.2314"
    std::wstring cpu;             // "AMD Ryzen 9 7900X 12-Core Processor"
    int cores = 0, threads = 0;
    double baseMhz = 0;
    uint64_t ram = 0;             // installed, bytes
    std::wstring ramDetail;       // "DDR5 · 6000 MT/s · 2 modules"
    std::vector<GpuInfo> gpus;
    std::wstring board;           // "ASUS ROG STRIX X670E-E GAMING WIFI"
    std::wstring bios;            // "BIOS 2204 (2024-03-12)"
    std::wstring display;         // "2560 × 1440 · 165 Hz"
    long long bootTime = 0;       // unix seconds
};

struct Sample {
    double cpu = -1;              // percent, all cores
    std::vector<float> cores;     // percent per logical processor
    double cpuMhz = -1;           // effective clock
    uint64_t memTotal = 0, memUsed = 0;
    uint64_t commitUsed = 0, commitLimit = 0;
    int processes = 0, threads = 0;
    double gpu = -1;              // percent
    double gpuTemp = -1;          // °C (NVIDIA only)
    double gpuPower = -1;         // watts (NVIDIA only)
    double gpuClock = -1;         // MHz (NVIDIA only)
    uint64_t vramUsed = 0, vramTotal = 0;
    double diskRead = -1, diskWrite = -1;   // bytes/s, all disks
    double diskActive = -1;       // percent busy
    double netDown = -1, netUp = -1;        // bytes/s
    uint64_t uptime = 0;          // seconds
};

struct DriveInfo {
    std::wstring root;            // "C:\"
    std::wstring label;           // "Windows" (may be empty)
    std::wstring fs;              // "NTFS"
    uint64_t total = 0, free = 0;
    bool system = false;
    bool removable = false;
};
struct DriveList { std::vector<DriveInfo> drives; };

struct HealthInfo {
    bool pendingReboot = false;
    uint64_t recycleBin = 0;      // bytes in the Recycle Bin (all drives)
    long long recycleItems = 0;
};

namespace sysinfo {

void LoadSpecs(HWND notify);                 // background; posts WM_HELM_SPECS with Specs*
void StartMonitor(HWND notify);              // starts the sampler thread (idle until SetActive)
void SetActive(bool active);                 // sample once a second while the Overview is on screen
void StopMonitor();
void RefreshDrives(HWND notify);             // background; posts WM_HELM_DRIVES with DriveList*
void RefreshHealth(HWND notify);             // background; posts WM_HELM_HEALTH with HealthInfo*

} // namespace sysinfo
