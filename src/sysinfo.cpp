// Helm - system information and live monitoring
#include "sysinfo.h"
#include "util.h"
#include <dxgi.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <psapi.h>
#include <shellapi.h>
#include <winternl.h>
#include <winsock2.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <algorithm>
#include <ctime>
#include <cmath>

namespace sysinfo {

// ---------------------------------------------------------------- helpers
static std::wstring Squash(std::wstring s) {
    // trim, and collapse runs of spaces ("AMD Ryzen 9 7900X 12-Core Processor          ")
    std::wstring out;
    bool sp = false;
    for (wchar_t c : s) {
        if (c == L' ' || c == L'\t') { sp = !out.empty(); continue; }
        if (sp) { out += L' '; sp = false; }
        out += c;
    }
    return out;
}

static bool Junk(const std::wstring& s) {
    std::wstring l = Lower(Trim(s));
    static const wchar_t* junk[] = {L"", L"to be filled by o.e.m.", L"system product name", L"system manufacturer", L"default string",
                                    L"not applicable", L"none", L"n/a", L"o.e.m.", L"oem", L"x.x", L"base board product name"};
    for (auto j : junk) if (l == j) return true;
    return false;
}

static std::wstring ShortVendor(const std::wstring& v0) {
    std::wstring v = Trim(v0), l = Lower(v);
    if (IStartsWith(l, L"asustek")) return L"ASUS";
    if (IStartsWith(l, L"micro-star")) return L"MSI";
    if (IStartsWith(l, L"gigabyte")) return L"Gigabyte";
    if (IStartsWith(l, L"asrock")) return L"ASRock";
    if (IStartsWith(l, L"hewlett") || l == L"hp") return L"HP";
    if (IStartsWith(l, L"lenovo")) return L"Lenovo";
    if (IStartsWith(l, L"dell")) return L"Dell";
    if (IStartsWith(l, L"microsoft")) return L"Microsoft";
    if (IStartsWith(l, L"biostar")) return L"Biostar";
    if (IStartsWith(l, L"nzxt")) return L"NZXT";
    if (IStartsWith(l, L"evga")) return L"EVGA";
    // "Something Co., Ltd." -> "Something"
    for (const wchar_t* tail : {L" co., ltd.", L" co.,ltd.", L" corporation", L" inc.", L" inc", L" ltd.", L" corp."}) {
        size_t n = wcslen(tail);
        if (l.size() > n && l.compare(l.size() - n, n, tail) == 0) return Trim(v.substr(0, v.size() - n));
    }
    return v;
}

static const wchar_t* kBiosKey = L"HARDWARE\\DESCRIPTION\\System\\BIOS";
static const wchar_t* kCpuKey = L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0";
static const wchar_t* kNtKey = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";

// SMBIOS type 17 (memory devices): type, speed and module count
static void ReadMemoryModules(std::wstring& detail) {
    UINT size = GetSystemFirmwareTable('RSMB', 0, nullptr, 0);
    if (!size || size > 4 * 1024 * 1024) return;
    std::vector<BYTE> buf(size);
    if (GetSystemFirmwareTable('RSMB', 0, buf.data(), size) != size || size < 8) return;
    DWORD len = *(DWORD*)(buf.data() + 4);
    const BYTE* p = buf.data() + 8;
    const BYTE* end = p + std::min<size_t>(len, size - 8);
    int modules = 0, speed = 0;
    BYTE type = 0;
    while (p + 4 <= end) {
        BYTE t = p[0], l = p[1];
        if (l < 4 || p + l > end) break;
        if (t == 127) break;   // end of table
        if (t == 17 && l >= 0x15) {
            WORD sz = *(const WORD*)(p + 0x0C);
            bool present = sz != 0 && sz != 0xFFFF;
            if (present) {
                ++modules;
                if (!type && l > 0x12) type = p[0x12];
                int s = *(const WORD*)(p + 0x15);
                if (l >= 0x22) {
                    int configured = *(const WORD*)(p + 0x20);
                    if (configured && configured != 0xFFFF) s = configured;
                }
                if (s == 0xFFFF) s = 0;
                if (s > speed) speed = s;
            }
        }
        // skip the formatted area, then the string set (ends with two NULs)
        const BYTE* q = p + l;
        while (q + 1 < end && !(q[0] == 0 && q[1] == 0)) ++q;
        p = q + 2;
    }
    std::wstring t;
    switch (type) {
        case 0x18: t = L"DDR3"; break;
        case 0x1A: t = L"DDR4"; break;
        case 0x1E: t = L"LPDDR4"; break;
        case 0x22: t = L"DDR5"; break;
        case 0x23: t = L"LPDDR5"; break;
    }
    std::vector<std::wstring> parts;
    if (!t.empty()) parts.push_back(t);
    if (speed > 0) parts.push_back(std::to_wstring(speed) + L" MT/s");
    if (modules > 0) parts.push_back(std::to_wstring(modules) + (modules == 1 ? L" module" : L" modules"));
    for (auto& s : parts) detail += (detail.empty() ? L"" : L" · ") + s;
}

static void ReadGpus(std::vector<GpuInfo>& out) {
    IDXGIFactory1* f = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&f)) || !f) return;
    IDXGIAdapter1* a = nullptr;
    for (UINT i = 0; f->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND && i < 16; ++i) {
        DXGI_ADAPTER_DESC1 d{};
        if (a && SUCCEEDED(a->GetDesc1(&d))) {
            bool software = (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) || d.VendorId == 0x1414;   // Microsoft Basic Render Driver
            if (!software) {
                GpuInfo g;
                g.name = Squash(d.Description);
                g.vram = d.DedicatedVideoMemory;
                out.push_back(g);
            }
        }
        if (a) { a->Release(); a = nullptr; }
    }
    f->Release();
}

void LoadSpecs(HWND notify) {
    std::thread([notify] {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        auto* s = new Specs;
        wchar_t buf[256];
        DWORD n = 256;
        if (GetComputerNameExW(ComputerNameDnsHostname, buf, &n)) s->computer = buf;
        n = 256;
        if (GetUserNameW(buf, &n)) s->user = buf;

        // Windows
        std::wstring product, display, build;
        RegReadString(HKEY_LOCAL_MACHINE, kNtKey, L"ProductName", product);
        if (!RegReadString(HKEY_LOCAL_MACHINE, kNtKey, L"DisplayVersion", display)) RegReadString(HKEY_LOCAL_MACHINE, kNtKey, L"ReleaseId", display);
        RegReadString(HKEY_LOCAL_MACHINE, kNtKey, L"CurrentBuild", build);
        DWORD ubr = 0;
        RegReadDword(HKEY_LOCAL_MACHINE, kNtKey, L"UBR", ubr);
        // Windows 11 still says "Windows 10" in ProductName
        if (_wtoi(build.c_str()) >= 22000 && product.find(L"Windows 10") != std::wstring::npos)
            product.replace(product.find(L"Windows 10"), 10, L"Windows 11");
        s->os = product.empty() ? L"Windows" : product;
        std::wstring b = build.empty() ? L"" : L"build " + build + (ubr ? L"." + std::to_wstring(ubr) : L"");
        s->osDetail = display.empty() ? b : (b.empty() ? display : display + L" · " + b);

        // CPU
        RegReadString(HKEY_LOCAL_MACHINE, kCpuKey, L"ProcessorNameString", s->cpu);
        s->cpu = Squash(s->cpu);
        DWORD mhz = 0;
        if (RegReadDword(HKEY_LOCAL_MACHINE, kCpuKey, L"~MHz", mhz)) s->baseMhz = mhz;
        DWORD len = 0;
        GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &len);
        if (len) {
            std::vector<BYTE> lp(len);
            if (GetLogicalProcessorInformationEx(RelationProcessorCore, (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)lp.data(), &len)) {
                for (DWORD off = 0; off + sizeof(DWORD) * 2 <= len;) {
                    auto* e = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)(lp.data() + off);
                    if (!e->Size) break;
                    if (e->Relationship == RelationProcessorCore) ++s->cores;
                    off += e->Size;
                }
            }
        }
        s->threads = (int)GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);

        // memory
        ULONGLONG kb = 0;
        if (GetPhysicallyInstalledSystemMemory(&kb) && kb) s->ram = kb * 1024;
        else {
            MEMORYSTATUSEX ms{sizeof(ms)};
            if (GlobalMemoryStatusEx(&ms)) s->ram = ms.ullTotalPhys;
        }
        ReadMemoryModules(s->ramDetail);

        ReadGpus(s->gpus);

        // motherboard / model
        std::wstring bbMan, bbProd, sysMan, sysProd, biosVer, biosDate;
        RegReadString(HKEY_LOCAL_MACHINE, kBiosKey, L"BaseBoardManufacturer", bbMan);
        RegReadString(HKEY_LOCAL_MACHINE, kBiosKey, L"BaseBoardProduct", bbProd);
        RegReadString(HKEY_LOCAL_MACHINE, kBiosKey, L"SystemManufacturer", sysMan);
        RegReadString(HKEY_LOCAL_MACHINE, kBiosKey, L"SystemProductName", sysProd);
        RegReadString(HKEY_LOCAL_MACHINE, kBiosKey, L"BIOSVersion", biosVer);
        RegReadString(HKEY_LOCAL_MACHINE, kBiosKey, L"BIOSReleaseDate", biosDate);
        bool laptopish = !Junk(sysMan) && !Junk(sysProd) &&
                         (IContains(sysMan, L"dell") || IContains(sysMan, L"hp") || IContains(sysMan, L"hewlett") || IContains(sysMan, L"lenovo") ||
                          IContains(sysMan, L"microsoft") || IContains(sysMan, L"razer") || IContains(sysMan, L"framework") || IContains(sysMan, L"acer") ||
                          IContains(sysMan, L"samsung") || IContains(sysMan, L"apple"));
        if (laptopish) s->board = ShortVendor(sysMan) + L" " + Squash(sysProd);
        else if (!Junk(bbProd)) s->board = (Junk(bbMan) ? L"" : ShortVendor(bbMan) + L" ") + Squash(bbProd);
        else if (!Junk(sysProd)) s->board = (Junk(sysMan) ? L"" : ShortVendor(sysMan) + L" ") + Squash(sysProd);
        if (!Junk(biosVer)) s->bios = L"BIOS " + Squash(biosVer) + (biosDate.empty() ? L"" : L" (" + biosDate + L")");

        // primary display
        DEVMODEW dm{};
        dm.dmSize = sizeof(dm);
        if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm)) {
            wchar_t d[96];
            swprintf(d, 96, L"%lu × %lu", dm.dmPelsWidth, dm.dmPelsHeight);
            s->display = d;
            if (dm.dmDisplayFrequency > 1) s->display += L" · " + std::to_wstring(dm.dmDisplayFrequency) + L" Hz";
            int mons = GetSystemMetrics(SM_CMONITORS);
            if (mons > 1) s->display += L" · " + std::to_wstring(mons) + L" displays";
        }
        s->bootTime = (long long)time(nullptr) - (long long)(GetTickCount64() / 1000);
        CoUninitialize();
        if (!PostMessageW(notify, WM_HELM_SPECS, 0, (LPARAM)s)) delete s;
    }).detach();
}

// ---------------------------------------------------------------- NVIDIA (NVML), loaded only if the driver has it
namespace nvml {
typedef int (*InitFn)();
typedef int (*CountFn)(unsigned*);
typedef int (*HandleFn)(unsigned, void**);
struct Util { unsigned gpu, memory; };
typedef int (*UtilFn)(void*, Util*);
typedef int (*TempFn)(void*, int, unsigned*);
struct Mem { unsigned long long total, free, used; };
typedef int (*MemFn)(void*, Mem*);
typedef int (*PowerFn)(void*, unsigned*);
typedef int (*ClockFn)(void*, int, unsigned*);

static bool s_ok = false;
static void* s_dev = nullptr;
static UtilFn s_util;
static TempFn s_temp;
static MemFn s_mem;
static PowerFn s_power;
static ClockFn s_clock;

static void Init() {
    HMODULE m = LoadLibraryExW(L"nvml.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!m) {
        std::wstring alt = ExpandEnv(L"%ProgramW6432%\\NVIDIA Corporation\\NVSMI\\nvml.dll");
        m = LoadLibraryExW(alt.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    }
    if (!m) return;
    auto init = (InitFn)GetProcAddress(m, "nvmlInit_v2");
    auto count = (CountFn)GetProcAddress(m, "nvmlDeviceGetCount_v2");
    auto handle = (HandleFn)GetProcAddress(m, "nvmlDeviceGetHandleByIndex_v2");
    s_util = (UtilFn)GetProcAddress(m, "nvmlDeviceGetUtilizationRates");
    s_temp = (TempFn)GetProcAddress(m, "nvmlDeviceGetTemperature");
    s_mem = (MemFn)GetProcAddress(m, "nvmlDeviceGetMemoryInfo");
    s_power = (PowerFn)GetProcAddress(m, "nvmlDeviceGetPowerUsage");
    s_clock = (ClockFn)GetProcAddress(m, "nvmlDeviceGetClockInfo");
    if (!init || !count || !handle || !s_util) return;
    if (init() != 0) return;
    unsigned n = 0;
    if (count(&n) != 0 || n == 0) return;
    if (handle(0, &s_dev) != 0 || !s_dev) return;
    s_ok = true;
    Log(L"monitor: NVIDIA GPU stats available");
}

static void Read(Sample& s) {
    if (!s_ok) return;
    Util u{};
    if (s_util(s_dev, &u) == 0) s.gpu = u.gpu;
    unsigned v = 0;
    if (s_temp && s_temp(s_dev, 0 /*NVML_TEMPERATURE_GPU*/, &v) == 0) s.gpuTemp = v;
    Mem mem{};
    if (s_mem && s_mem(s_dev, &mem) == 0) { s.vramUsed = mem.used; s.vramTotal = mem.total; }
    if (s_power && s_power(s_dev, &v) == 0) s.gpuPower = v / 1000.0;
    if (s_clock && s_clock(s_dev, 0 /*NVML_CLOCK_GRAPHICS*/, &v) == 0) s.gpuClock = v;
}
} // namespace nvml

// ---------------------------------------------------------------- sampler
typedef LONG(NTAPI* NtQsiFn)(ULONG, PVOID, ULONG, PULONG);
struct ProcPerf { LARGE_INTEGER idle, kernel, user, dpc, interrupt; ULONG interruptCount; };

class Sampler {
public:
    void Init() {
        nvml::Init();
        qsi_ = (NtQsiFn)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQuerySystemInformation");
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        ncpu_ = std::clamp<int>((int)si.dwNumberOfProcessors, 1, 64);
        DWORD mhz = 0;
        if (RegReadDword(HKEY_LOCAL_MACHINE, kCpuKey, L"~MHz", mhz)) baseMhz_ = mhz;
        OpenPdh();
    }

    // Takes a sample. The first call only sets the baseline (out may be null).
    void Take(Sample* out) {
        ULONGLONG now = GetTickCount64();
        double dt = last_ ? (now - last_) / 1000.0 : 0;
        last_ = now;
        Sample s;

        // CPU, all cores
        FILETIME fi, fk, fu;
        if (GetSystemTimes(&fi, &fk, &fu)) {
            uint64_t i = Ft(fi), k = Ft(fk), u = Ft(fu);
            uint64_t di = i - idle_, dk = k - kern_, du = u - user_;
            if (idle_ && dk + du > 0) s.cpu = std::clamp(100.0 * (1.0 - (double)di / (double)(dk + du)), 0.0, 100.0);
            idle_ = i; kern_ = k; user_ = u;
        }
        // CPU, per core
        if (qsi_) {
            std::vector<ProcPerf> pp(ncpu_);
            ULONG got = 0;
            if (qsi_(8 /*SystemProcessorPerformanceInformation*/, pp.data(), (ULONG)(pp.size() * sizeof(ProcPerf)), &got) == 0) {
                int n = std::min<int>(ncpu_, (int)(got / sizeof(ProcPerf)));
                if ((int)prev_.size() == n) {
                    s.cores.resize(n);
                    for (int c = 0; c < n; ++c) {
                        int64_t di = pp[c].idle.QuadPart - prev_[c].idle.QuadPart;
                        int64_t dt2 = (pp[c].kernel.QuadPart - prev_[c].kernel.QuadPart) + (pp[c].user.QuadPart - prev_[c].user.QuadPart);
                        s.cores[c] = dt2 > 0 ? (float)std::clamp(100.0 * (1.0 - (double)di / (double)dt2), 0.0, 100.0) : 0.0f;
                    }
                }
                prev_.assign(pp.begin(), pp.begin() + n);
            }
        }
        // memory
        MEMORYSTATUSEX ms{sizeof(ms)};
        if (GlobalMemoryStatusEx(&ms)) { s.memTotal = ms.ullTotalPhys; s.memUsed = ms.ullTotalPhys - ms.ullAvailPhys; }
        PERFORMANCE_INFORMATION pi{sizeof(pi)};
        if (GetPerformanceInfo(&pi, sizeof(pi))) {
            s.commitUsed = (uint64_t)pi.CommitTotal * pi.PageSize;
            s.commitLimit = (uint64_t)pi.CommitLimit * pi.PageSize;
            s.processes = (int)pi.ProcessCount;
            s.threads = (int)pi.ThreadCount;
        }
        // network
        MIB_IF_TABLE2* table = nullptr;
        if (GetIfTable2(&table) == NO_ERROR && table) {
            uint64_t in = 0, outB = 0;
            for (ULONG k = 0; k < table->NumEntries; ++k) {
                const MIB_IF_ROW2& r = table->Table[k];
                if (r.Type == IF_TYPE_SOFTWARE_LOOPBACK || r.OperStatus != IfOperStatusUp) continue;
                if (!r.InterfaceAndOperStatusFlags.HardwareInterface || r.InterfaceAndOperStatusFlags.FilterInterface) continue;
                in += r.InOctets;
                outB += r.OutOctets;
            }
            FreeMibTable(table);
            if (dt > 0 && haveNet_ && in >= netIn_ && outB >= netOut_) {
                s.netDown = (in - netIn_) / dt;
                s.netUp = (outB - netOut_) / dt;
            }
            netIn_ = in; netOut_ = outB; haveNet_ = true;
        }
        // PDH: clock, disks, GPU (when there's no NVML)
        if (query_) {
            if (++pdhAge_ > 30) { ReopenGpuCounters(); pdhAge_ = 0; }
            if (PdhCollectQueryData(query_) == ERROR_SUCCESS && pdhPrimed_) {
                double v;
                if (perf_ && Value(perf_, v) && baseMhz_ > 0) s.cpuMhz = baseMhz_ * v / 100.0;
                if (diskR_ && Value(diskR_, v)) s.diskRead = v;
                if (diskW_ && Value(diskW_, v)) s.diskWrite = v;
                if (diskIdle_ && Value(diskIdle_, v)) s.diskActive = std::clamp(100.0 - v, 0.0, 100.0);
                if (gpu3d_ && Sum(gpu3d_, v)) s.gpu = std::min(100.0, v);
                if (gpuMem_ && Sum(gpuMem_, v)) s.vramUsed = (uint64_t)v;
            }
            pdhPrimed_ = true;
        }
        nvml::Read(s);   // NVIDIA's own numbers win
        s.uptime = GetTickCount64() / 1000;
        if (out) *out = std::move(s);
    }

private:
    static uint64_t Ft(const FILETIME& f) { return ((uint64_t)f.dwHighDateTime << 32) | f.dwLowDateTime; }

    void OpenPdh() {
        if (PdhOpenQueryW(nullptr, 0, &query_) != ERROR_SUCCESS) { query_ = nullptr; return; }
        auto add = [&](const wchar_t* path, PDH_HCOUNTER& c) {
            if (PdhAddEnglishCounterW(query_, path, 0, &c) != ERROR_SUCCESS) c = nullptr;
        };
        add(L"\\Processor Information(_Total)\\% Processor Performance", perf_);
        add(L"\\PhysicalDisk(_Total)\\Disk Read Bytes/sec", diskR_);
        add(L"\\PhysicalDisk(_Total)\\Disk Write Bytes/sec", diskW_);
        add(L"\\PhysicalDisk(_Total)\\% Idle Time", diskIdle_);
        ReopenGpuCounters();
    }

    // GPU engine instances come and go with processes; re-adding the wildcard counters picks up new ones
    void ReopenGpuCounters() {
        if (nvml::s_ok || !query_) return;
        if (gpu3d_) { PdhRemoveCounter(gpu3d_); gpu3d_ = nullptr; }
        if (gpuMem_) { PdhRemoveCounter(gpuMem_); gpuMem_ = nullptr; }
        if (PdhAddEnglishCounterW(query_, L"\\GPU Engine(*engtype_3D)\\Utilization Percentage", 0, &gpu3d_) != ERROR_SUCCESS) gpu3d_ = nullptr;
        if (PdhAddEnglishCounterW(query_, L"\\GPU Adapter Memory(*)\\Dedicated Usage", 0, &gpuMem_) != ERROR_SUCCESS) gpuMem_ = nullptr;
        pdhPrimed_ = false;   // rate counters need two collections
    }

    static bool Value(PDH_HCOUNTER c, double& v) {
        PDH_FMT_COUNTERVALUE fv;
        if (PdhGetFormattedCounterValue(c, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, nullptr, &fv) != ERROR_SUCCESS) return false;
        if (fv.CStatus != PDH_CSTATUS_VALID_DATA && fv.CStatus != PDH_CSTATUS_NEW_DATA) return false;
        v = fv.doubleValue;
        return std::isfinite(v);
    }

    static bool Sum(PDH_HCOUNTER c, double& v) {
        DWORD bytes = 0, count = 0;
        PDH_STATUS st = PdhGetFormattedCounterArrayW(c, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &bytes, &count, nullptr);
        if (st != (PDH_STATUS)PDH_MORE_DATA || !bytes) return false;
        std::vector<BYTE> buf(bytes);
        auto* items = (PDH_FMT_COUNTERVALUE_ITEM_W*)buf.data();
        if (PdhGetFormattedCounterArrayW(c, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &bytes, &count, items) != ERROR_SUCCESS) return false;
        double total = 0;
        for (DWORD k = 0; k < count; ++k)
            if (items[k].FmtValue.CStatus == PDH_CSTATUS_VALID_DATA && std::isfinite(items[k].FmtValue.doubleValue)) total += items[k].FmtValue.doubleValue;
        v = total;
        return true;
    }

    NtQsiFn qsi_ = nullptr;
    int ncpu_ = 1;
    double baseMhz_ = 0;
    ULONGLONG last_ = 0;
    uint64_t idle_ = 0, kern_ = 0, user_ = 0;
    std::vector<ProcPerf> prev_;
    uint64_t netIn_ = 0, netOut_ = 0;
    bool haveNet_ = false;
    PDH_HQUERY query_ = nullptr;
    PDH_HCOUNTER perf_ = nullptr, diskR_ = nullptr, diskW_ = nullptr, diskIdle_ = nullptr, gpu3d_ = nullptr, gpuMem_ = nullptr;
    bool pdhPrimed_ = false;
    int pdhAge_ = 0;
};

static std::mutex s_lock;
static std::condition_variable s_cv;
static bool s_active = false, s_stop = false, s_started = false;
static HWND s_notify = nullptr;

void StartMonitor(HWND notify) {
    {
        std::lock_guard<std::mutex> g(s_lock);
        if (s_started) return;
        s_started = true;
        s_notify = notify;
    }
    std::thread([] {
        Sampler sampler;
        bool inited = false;
        for (;;) {
            {
                std::unique_lock<std::mutex> lk(s_lock);
                s_cv.wait(lk, [] { return s_active || s_stop; });
                if (s_stop) return;
            }
            if (!inited) { sampler.Init(); inited = true; }
            sampler.Take(nullptr);   // baseline after being idle
            for (;;) {
                {
                    std::unique_lock<std::mutex> lk(s_lock);
                    s_cv.wait_for(lk, std::chrono::milliseconds(1000), [] { return !s_active || s_stop; });
                    if (s_stop) return;
                    if (!s_active) break;
                }
                auto* s = new Sample;
                sampler.Take(s);
                if (!PostMessageW(s_notify, WM_HELM_SAMPLE, 0, (LPARAM)s)) delete s;
            }
        }
    }).detach();
}

void SetActive(bool active) {
    std::lock_guard<std::mutex> g(s_lock);
    if (s_active == active) return;
    s_active = active;
    s_cv.notify_all();
}

void StopMonitor() {
    std::lock_guard<std::mutex> g(s_lock);
    s_stop = true;
    s_cv.notify_all();
}

// ---------------------------------------------------------------- drives and health
void RefreshDrives(HWND notify) {
    std::thread([notify] {
        auto* list = new DriveList;
        wchar_t sysDir[MAX_PATH] = L"C:\\";
        GetWindowsDirectoryW(sysDir, MAX_PATH);
        wchar_t buf[512];
        DWORD n = GetLogicalDriveStringsW(511, buf);
        if (n && n < 512) {
            for (const wchar_t* p = buf; *p; p += wcslen(p) + 1) {
                UINT type = GetDriveTypeW(p);
                if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE) continue;
                DriveInfo d;
                d.root = p;
                d.removable = type == DRIVE_REMOVABLE;
                ULARGE_INTEGER freeToCaller, total, freeAll;
                if (!GetDiskFreeSpaceExW(p, &freeToCaller, &total, &freeAll) || total.QuadPart == 0) continue;   // e.g. a card reader with no card
                d.total = total.QuadPart;
                d.free = freeAll.QuadPart;
                wchar_t label[MAX_PATH + 1] = {}, fs[MAX_PATH + 1] = {};
                if (GetVolumeInformationW(p, label, MAX_PATH, nullptr, nullptr, nullptr, fs, MAX_PATH)) { d.label = label; d.fs = fs; }
                d.system = towupper(p[0]) == towupper(sysDir[0]);
                list->drives.push_back(d);
            }
        }
        if (!PostMessageW(notify, WM_HELM_DRIVES, 0, (LPARAM)list)) delete list;
    }).detach();
}

void RefreshHealth(HWND notify) {
    std::thread([notify] {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        auto* h = new HealthInfo;
        h->pendingReboot = RegKeyExists(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\WindowsUpdate\\Auto Update\\RebootRequired") ||
                           RegKeyExists(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Component Based Servicing\\RebootPending");
        SHQUERYRBINFO qi{};
        qi.cbSize = sizeof(qi);
        if (SUCCEEDED(SHQueryRecycleBinW(nullptr, &qi))) { h->recycleBin = (uint64_t)qi.i64Size; h->recycleItems = qi.i64NumItems; }
        CoUninitialize();
        if (!PostMessageW(notify, WM_HELM_HEALTH, 0, (LPARAM)h)) delete h;
    }).detach();
}

} // namespace sysinfo
