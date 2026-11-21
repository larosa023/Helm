// Helm - Overview: how the PC is doing, what it's made of, what's worth doing
#include "ui_internal.h"
#include "sysinfo.h"
#include "config.h"
#include "util.h"
#include <deque>
#include <algorithm>
#include <cmath>
#include <ctime>

static Specs g_specs;
static bool g_haveSpecs = false;
static Sample g_now;
static bool g_haveSample = false;
static DriveList g_drives;
static bool g_haveDrives = false;
static HealthInfo g_health;
static bool g_haveHealth = false;

static const size_t kHistory = 60;
static std::deque<float> g_hCpu, g_hMem, g_hGpu, g_hDisk, g_hDown, g_hUp;

const DriveList& Drives() { return g_drives; }

void OverviewEnter() {
    sysinfo::RefreshDrives(g_hwnd);
    sysinfo::RefreshHealth(g_hwnd);
    TweaksEnter();       // suggestions count privacy settings that are still off
    StartupReload();
}

void OverviewOnSpecs(Specs* s) { g_specs = *s; g_haveSpecs = true; }

static void Push(std::deque<float>& h, double v) {
    h.push_back(v < 0 ? -1.0f : (float)v);
    while (h.size() > kHistory) h.pop_front();
}

void OverviewOnSample(Sample* s) {
    g_now = *s;
    g_haveSample = true;
    Push(g_hCpu, s->cpu);
    Push(g_hMem, s->memTotal ? 100.0 * s->memUsed / s->memTotal : -1);
    Push(g_hGpu, s->gpu);
    Push(g_hDisk, s->diskActive);
    Push(g_hDown, s->netDown);
    Push(g_hUp, s->netUp);
}

void OverviewOnDrives(DriveList* d) { g_drives = *d; g_haveDrives = true; }
void OverviewOnHealth(HealthInfo* h) { g_health = *h; g_haveHealth = true; }

// ---------------------------------------------------------------- suggestions
enum class Act { None, Cleanup, Storage, Startup, Tweaks };

struct Suggestion {
    int severity;                // 2 problem, 1 warning, 0 tip
    wchar_t glyph;
    D2D1_COLOR_F color;
    std::wstring title, detail, button;
    Act act = Act::None;
};

static const DriveInfo* SystemDrive() {
    for (auto& d : g_drives.drives) if (d.system) return &d;
    return nullptr;
}

static std::vector<Suggestion> Suggestions() {
    std::vector<Suggestion> out;
    if (const DriveInfo* d = SystemDrive()) {
        double pct = d->total ? (double)d->free / d->total : 1;
        if (pct < 0.10 || d->free < 15ull * 1024 * 1024 * 1024)
            out.push_back({2, icon::Drive, g_t.bad, L"Your system drive is almost full",
                           FormatBytes(d->free) + L" free of " + FormatBytes(d->total) + L". Windows slows down and can't update when it runs out.",
                           L"See what's using it", Act::Storage});
    }
    uint64_t junk = 0;
    if (CleanupJunk(&junk) && junk >= 500ull * 1024 * 1024)
        out.push_back({1, icon::Broom, g_t.warn, FormatBytes(junk) + L" of junk can be removed", L"Temporary files, caches and old reports.", L"Review", Act::Cleanup});
    if (g_haveHealth && g_health.pendingReboot)
        out.push_back({1, icon::Update, g_t.warn, L"Restart to finish installing updates", L"Windows has updates waiting for a restart.", L"", Act::None});
    int en = 0, total = 0;
    if (StartupCounts(&en, &total) && en >= 10)
        out.push_back({0, icon::Lightning, g_t.info, std::to_wstring(en) + L" apps start with Windows",
                       L"Each one makes signing in slower. Turn off the ones you don't need right away.", L"Review", Act::Startup});
    int off = TweaksRecommendedOff();
    if (off >= 3)
        out.push_back({0, icon::Shield, g_t.info, std::to_wstring(off) + L" privacy and ad settings could be tighter",
                       L"Ads, tracking and suggestions Windows turns on by default.", L"Review", Act::Tweaks});
    if (g_haveSample && g_now.uptime > 7ull * 86400)
        out.push_back({0, icon::Clock, g_t.info, L"Your PC has been on for " + std::to_wstring(g_now.uptime / 86400) + L" days",
                       L"A restart now and then frees memory and finishes updates.", L"", Act::None});
    if (g_haveHealth && g_health.recycleBin >= 2ull * 1024 * 1024 * 1024)
        out.push_back({0, icon::Delete, g_t.info, L"The Recycle Bin holds " + FormatBytes(g_health.recycleBin),
                       L"Empty it from Cleanup if you don't need those files.", L"Review", Act::Cleanup});
    std::stable_sort(out.begin(), out.end(), [](const Suggestion& a, const Suggestion& b) { return a.severity > b.severity; });
    return out;
}

// ---------------------------------------------------------------- drawing helpers
static void Sparkline(const D2D1_RECT_F& r, const std::deque<float>& h, float maxV, D2D1_COLOR_F color) {
    if (h.size() < 2 || maxV <= 0) return;
    bool any = false;
    for (float v : h) any |= v >= 0;
    if (!any) return;   // no readings for this one (e.g. no GPU counters)
    ID2D1PathGeometry* line = nullptr;
    ID2D1PathGeometry* area = nullptr;
    d2d->CreatePathGeometry(&line);
    d2d->CreatePathGeometry(&area);
    float w = r.right - r.left, ht = r.bottom - r.top;
    float step = w / (float)(kHistory - 1);
    float x0 = r.right - step * (float)(h.size() - 1);
    auto Y = [&](float v) { return r.bottom - std::clamp(v < 0 ? 0.0f : v / maxV, 0.0f, 1.0f) * ht; };
    ID2D1GeometrySink* s = nullptr;
    if (line && SUCCEEDED(line->Open(&s))) {
        s->BeginFigure(D2D1::Point2F(x0, Y(h[0])), D2D1_FIGURE_BEGIN_HOLLOW);
        for (size_t i = 1; i < h.size(); ++i) s->AddLine(D2D1::Point2F(x0 + step * i, Y(h[i])));
        s->EndFigure(D2D1_FIGURE_END_OPEN);
        s->Close();
        s->Release();
    }
    if (area && SUCCEEDED(area->Open(&s))) {
        s->BeginFigure(D2D1::Point2F(x0, r.bottom), D2D1_FIGURE_BEGIN_FILLED);
        for (size_t i = 0; i < h.size(); ++i) s->AddLine(D2D1::Point2F(x0 + step * i, Y(h[i])));
        s->AddLine(D2D1::Point2F(r.right, r.bottom));
        s->EndFigure(D2D1_FIGURE_END_CLOSED);
        s->Close();
        s->Release();
    }
    if (area) { g_c.br->SetColor(Alpha(color, 0.13f)); g_rt->FillGeometry(area, g_c.br); area->Release(); }
    if (line) { g_c.br->SetColor(color); g_rt->DrawGeometry(line, g_c.br, 1.6f); line->Release(); }
}

static float Max(const std::deque<float>& h, float floor) {
    float m = floor;
    for (float v : h) m = std::max(m, v);
    return m;
}

static std::wstring Pct(double v) {
    if (v < 0) return L"—";
    wchar_t b[16];
    swprintf(b, 16, L"%.0f%%", v);
    return b;
}

static D2D1_COLOR_F LoadColor(double pct) {
    if (pct >= 90) return g_t.bad;
    if (pct >= 70) return g_t.warn;
    return g_t.accent;
}

struct TileData {
    wchar_t glyph;
    std::wstring title, big, unit;
    std::vector<std::wstring> lines;
    const std::deque<float>* hist;
    const std::deque<float>* hist2;     // a second line (network upload)
    float maxV;
    double pct;                          // -1: no bar
    D2D1_COLOR_F color;
};

static void Tile(const D2D1_RECT_F& r, const TileData& d, int id) {
    Card(r);
    float x = r.left + 18, right = r.right - 18;
    Glyph(d.glyph, 14, D2D1::RectF(x - 2, r.top + 14, x + 20, r.top + 36), g_t.subtext);
    Text(d.title, 13, D2D1::RectF(x + 24, r.top + 14, right, r.top + 36), g_t.subtext, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    auto* bf = Format(30, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    float bw = MeasureWidth(d.big, bf);
    g_c.Text(d.big, bf, D2D1::RectF(x, r.top + 40, right, r.top + 80), g_t.text);
    if (!d.unit.empty()) Text(d.unit, 13, D2D1::RectF(x + bw + 6, r.top + 52, right, r.top + 76), g_t.subtext);
    float ly = r.top + 82;
    for (auto& l : d.lines) {
        Text(l, 12, D2D1::RectF(x, ly, right, ly + 18), g_t.subtext);
        ly += 18;
    }
    D2D1_RECT_F sp = D2D1::RectF(r.left + 1, r.bottom - 44, r.right - 1, r.bottom - 10);
    g_rt->PushAxisAlignedClip(D2D1::RectF(r.left, r.top, r.right, r.bottom), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    if (d.hist2) Sparkline(sp, *d.hist2, d.maxV, g_t.info);
    if (d.hist) Sparkline(sp, *d.hist, d.maxV, d.color);
    g_rt->PopAxisAlignedClip();
    if (d.pct >= 0) {
        float v = Anim(AKey(2000, id), (float)d.pct, 0.18f);
        ProgressBar(D2D1::RectF(x, r.bottom - 8, right, r.bottom - 4), v, d.color);
    }
}

static std::wstring Gb(uint64_t b) {
    wchar_t s[32];
    swprintf(s, 32, L"%.1f", b / (1024.0 * 1024 * 1024));
    return s;
}

static std::wstring CopySpecsText() {
    std::wstring t = g_specs.computer + L"\r\n";
    t += L"Windows: " + g_specs.os + L" (" + g_specs.osDetail + L")\r\n";
    t += L"Processor: " + g_specs.cpu + L" (" + std::to_wstring(g_specs.cores) + L" cores, " + std::to_wstring(g_specs.threads) + L" threads)\r\n";
    for (auto& g : g_specs.gpus) t += L"Graphics: " + g.name + (g.vram ? L" (" + FormatBytes(g.vram) + L")" : L"") + L"\r\n";
    t += L"Memory: " + FormatBytes(g_specs.ram) + (g_specs.ramDetail.empty() ? L"" : L" (" + g_specs.ramDetail + L")") + L"\r\n";
    if (!g_specs.board.empty()) t += L"Motherboard: " + g_specs.board + L"\r\n";
    if (!g_specs.display.empty()) t += L"Display: " + g_specs.display + L"\r\n";
    return t;
}

static void CopyToClipboard(const std::wstring& s) {
    if (!OpenClipboard(g_hwnd)) { Toast(L"Couldn't use the clipboard.", true); return; }
    EmptyClipboard();
    size_t bytes = (s.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        if (void* p = GlobalLock(h)) {
            memcpy(p, s.c_str(), bytes);
            GlobalUnlock(h);
            if (!SetClipboardData(CF_UNICODETEXT, h)) GlobalFree(h);
        } else GlobalFree(h);
    }
    CloseClipboard();
    Toast(L"Your PC's specs are on the clipboard.");
}

// ---------------------------------------------------------------- page
static std::vector<Suggestion> g_shown;   // what was drawn (clicks refer to it)

void DrawOverview(float x0) {
    PageBox L = PageLayout(x0, 1120);
    float left = L.left, right = L.right;
    float y = 24 - g_scroll;
    std::wstring hello;
    {
        time_t t = time(nullptr);
        tm lt{};
        localtime_s(&lt, &t);
        hello = lt.tm_hour < 5 ? L"Good evening" : lt.tm_hour < 12 ? L"Good morning" : lt.tm_hour < 18 ? L"Good afternoon" : L"Good evening";
    }
    std::wstring sub = g_haveSpecs ? g_specs.computer + L" · " + g_specs.os + (g_specs.osDetail.empty() ? L"" : L" " + g_specs.osDetail) : L"";
    y = PageTitle(left, right, y, hello, sub);

    // ---- health + quick clean
    g_shown = Suggestions();
    {
        float qcW = std::min(330.0f, (right - left) * 0.36f);
        float hx = right - qcW;
        float rowsH = g_shown.empty() ? 0 : (float)g_shown.size() * 62;
        float h = std::max(170.0f, 76 + rowsH + 10);
        D2D1_RECT_F card = D2D1::RectF(left, y, hx - 14, y + h);
        Card(card);
        bool good = g_shown.empty();
        D2D1_COLOR_F hc = good ? g_t.good : g_shown[0].severity == 2 ? g_t.bad : g_shown[0].severity == 1 ? g_t.warn : g_t.info;
        g_c.Circle(card.left + 38, card.top + 40, 18, Alpha(hc, 0.16f));
        Glyph(good ? icon::Check : icon::Info, 16, D2D1::RectF(card.left + 20, card.top + 22, card.left + 56, card.top + 58), hc);
        std::wstring head = good ? L"Everything looks good" : (g_shown.size() == 1 ? L"1 thing worth a look" : std::to_wstring(g_shown.size()) + L" things worth a look");
        Text(head, 18, D2D1::RectF(card.left + 68, card.top + 18, card.right - 16, card.top + 42), g_t.text, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        Text(good ? L"Helm keeps an eye on space, junk, startup apps and privacy settings." : L"Suggestions, most important first.", 12.5f,
             D2D1::RectF(card.left + 68, card.top + 42, card.right - 16, card.top + 62), g_t.subtext);
        float ry = card.top + 76;
        for (size_t i = 0; i < g_shown.size(); ++i) {
            const Suggestion& s = g_shown[i];
            Divider(card.left + 18, card.right - 18, ry - 4);
            g_c.FillRound(D2D1::RectF(card.left + 20, ry + 10, card.left + 52, ry + 42), 8, Alpha(s.color, 0.14f));
            Glyph(s.glyph, 14, D2D1::RectF(card.left + 20, ry + 10, card.left + 52, ry + 42), s.color);
            float tr = card.right - 18 - (s.button.empty() ? 0 : ButtonWidth(s.button, 0) + 12);
            Text(s.title, 13.5f, D2D1::RectF(card.left + 66, ry + 8, tr, ry + 28), g_t.text, DWRITE_FONT_WEIGHT_SEMI_BOLD);
            Text(s.detail, 12, D2D1::RectF(card.left + 66, ry + 28, tr, ry + 46), g_t.subtext);
            if (!s.button.empty()) ButtonRight(card.right - 18, ry + 10, s.button, 0, Btn::Secondary, Hit::OvAction, (int)i);
            ry += 62;
        }

        D2D1_RECT_F qc = D2D1::RectF(hx, y, right, y + h);
        Card(qc);
        Text(L"QUICK CLEAN", 11.5f, D2D1::RectF(qc.left + 20, qc.top + 18, qc.right - 16, qc.top + 36), g_t.subtext, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        uint64_t junk = 0;
        bool known = CleanupJunk(&junk);
        std::wstring big = CleanupBusy() ? L"Working…" : known ? FormatBytes(junk) : L"—";
        Text(big, 28, D2D1::RectF(qc.left + 20, qc.top + 38, qc.right - 16, qc.top + 76), g_t.text, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        Text(CleanupBusy() ? L"Looking through caches and temp files" : known ? L"can be removed safely" : L"Helm hasn't looked yet", 12.5f,
             D2D1::RectF(qc.left + 20, qc.top + 76, qc.right - 16, qc.top + 94), g_t.subtext);
        float by = qc.bottom - 52;
        bool can = !CleanupBusy() && known && junk > 0;
        float bw = ButtonWidth(known || CleanupBusy() ? L"Clean now" : L"Look for junk", icon::Broom);
        Button(D2D1::RectF(qc.left + 20, by, qc.left + 20 + bw, by + 32), known || CleanupBusy() ? L"Clean now" : L"Look for junk", icon::Broom,
               Btn::Primary, Hit::OvQuickClean, -1, 0, can || (!known && !CleanupBusy()));
        if (g_cfg.totalFreed > 0)
            Text(L"Helm has freed " + FormatBytes((uint64_t)g_cfg.totalFreed) + L" so far", 11.5f, D2D1::RectF(qc.left + 20, qc.top + 96, qc.right - 16, qc.top + 114), g_t.faint);
        y += h + 30;
    }

    // ---- live
    y = SectionTitle(left, right, y, L"RIGHT NOW");
    {
        std::vector<TileData> tiles;
        const Sample& s = g_now;
        bool have = g_haveSample;
        {
            TileData t{icon::Cpu, L"Processor", have ? Pct(s.cpu) : L"—", L"", {}, &g_hCpu, nullptr, 100, have ? s.cpu : -1, LoadColor(s.cpu)};
            std::wstring l1;
            if (s.cpuMhz > 0) { wchar_t b[32]; swprintf(b, 32, L"%.2f GHz", s.cpuMhz / 1000.0); l1 = b; }
            if (have && s.processes) l1 += (l1.empty() ? L"" : L" · ") + std::to_wstring(s.processes) + L" processes";
            t.lines.push_back(l1);
            tiles.push_back(t);
        }
        {
            double pct = s.memTotal ? 100.0 * s.memUsed / s.memTotal : -1;
            TileData t{icon::Memory, L"Memory", have ? Gb(s.memUsed) : L"—", have ? L"of " + Gb(s.memTotal) + L" GB" : L"", {}, &g_hMem, nullptr, 100, pct, LoadColor(pct)};
            t.lines.push_back(have ? Pct(pct) + L" in use" : L"");
            tiles.push_back(t);
        }
        {
            TileData t{icon::Game, L"Graphics", have ? Pct(s.gpu) : L"—", L"", {}, &g_hGpu, nullptr, 100, s.gpu, LoadColor(s.gpu)};
            std::wstring l1;
            if (s.gpuTemp >= 0) { wchar_t b[32]; swprintf(b, 32, L"%.0f °C", s.gpuTemp); l1 = b; }
            if (s.gpuPower >= 0) { wchar_t b[32]; swprintf(b, 32, L"%.0f W", s.gpuPower); l1 += (l1.empty() ? L"" : L" · ") + std::wstring(b); }
            if (s.vramUsed) {
                uint64_t total = s.vramTotal ? s.vramTotal : (g_specs.gpus.empty() ? 0 : g_specs.gpus[0].vram);
                l1 += (l1.empty() ? L"" : L" · ") + Gb(s.vramUsed) + (total ? L" / " + Gb(total) : L"") + L" GB";
            }
            if (l1.empty() && !g_specs.gpus.empty()) l1 = g_specs.gpus[0].name;
            t.lines.push_back(l1);
            tiles.push_back(t);
        }
        {
            TileData t{icon::Drive, L"Disks", have ? Pct(s.diskActive) : L"—", have && s.diskActive >= 0 ? L"busy" : L"", {}, &g_hDisk, nullptr, 100,
                       s.diskActive, LoadColor(s.diskActive)};
            t.lines.push_back(s.diskRead >= 0 ? L"Read " + FormatRate(s.diskRead) + L" · Write " + FormatRate(s.diskWrite) : L"");
            tiles.push_back(t);
        }
        {
            float m = std::max(Max(g_hDown, 64 * 1024), Max(g_hUp, 64 * 1024));
            TileData t{icon::Network, L"Network", have && s.netDown >= 0 ? FormatRate(s.netDown) : L"—", have && s.netDown >= 0 ? L"down" : L"", {},
                       &g_hDown, &g_hUp, m, -1, g_t.accent};
            t.lines.push_back(have && s.netUp >= 0 ? L"Up " + FormatRate(s.netUp) : L"");
            tiles.push_back(t);
        }
        float gap = 14, minW = 190;
        float avail = right - left;
        int cols = std::clamp((int)((avail + gap) / (minW + gap)), 1, (int)tiles.size());
        float tw = (avail - gap * (cols - 1)) / cols, th = 172;
        for (size_t i = 0; i < tiles.size(); ++i) {
            int c = (int)(i % cols), row = (int)(i / cols);
            D2D1_RECT_F r = D2D1::RectF(left + c * (tw + gap), y + row * (th + gap), left + c * (tw + gap) + tw, y + row * (th + gap) + th);
            if (Visible(r.top, r.bottom)) Tile(r, tiles[i], (int)i);
        }
        int rows = ((int)tiles.size() + cols - 1) / cols;
        y += rows * (th + gap) + 4;

        // per-core load
        if (!s.cores.empty()) {
            int n = (int)s.cores.size();
            float h = 104;
            D2D1_RECT_F card = D2D1::RectF(left, y, right, y + h);
            if (Visible(card.top, card.bottom)) {
                Card(card);
                Text(L"Each thread", 13, D2D1::RectF(left + 18, y + 12, right - 18, y + 32), g_t.subtext, DWRITE_FONT_WEIGHT_SEMI_BOLD);
                float bx = left + 18, bw = (right - left - 36) / n;
                float barW = std::max(2.0f, bw - std::min(6.0f, bw * 0.3f));
                float top = y + 40, bottom = y + h - 16;
                for (int k = 0; k < n; ++k) {
                    float v = Anim(AKey(2100, k), s.cores[k], 0.2f);
                    float x = bx + k * bw;
                    g_c.FillRound(D2D1::RectF(x, top, x + barW, bottom), 3, g_t.surfaceStrong);
                    float hh = (bottom - top) * std::clamp(v / 100.0f, 0.0f, 1.0f);
                    if (hh > 1) g_c.FillRound(D2D1::RectF(x, bottom - hh, x + barW, bottom), 3, LoadColor(v));
                }
            }
            y += h + 30;
        } else {
            y += 26;
        }
    }

    // ---- your PC
    y = SectionTitle(left, right, y, L"YOUR PC");
    {
        std::vector<std::pair<std::wstring, std::wstring>> rows;
        if (g_haveSpecs) {
            rows.push_back({L"Windows", g_specs.os + (g_specs.osDetail.empty() ? L"" : L" · " + g_specs.osDetail)});
            rows.push_back({L"Processor", g_specs.cpu + (g_specs.cores ? L" · " + std::to_wstring(g_specs.cores) + L" cores, " + std::to_wstring(g_specs.threads) + L" threads" : L"")});
            for (auto& gp : g_specs.gpus) rows.push_back({L"Graphics", gp.name + (gp.vram ? L" · " + FormatBytes(gp.vram) : L"")});
            rows.push_back({L"Memory", FormatBytes(g_specs.ram) + (g_specs.ramDetail.empty() ? L"" : L" · " + g_specs.ramDetail)});
            if (!g_specs.board.empty()) rows.push_back({L"Motherboard", g_specs.board + (g_specs.bios.empty() ? L"" : L" · " + g_specs.bios)});
            if (!g_specs.display.empty()) rows.push_back({L"Display", g_specs.display});
            if (g_haveSample) rows.push_back({L"Up for", FormatDuration(g_now.uptime)});
        }
        float h = std::max(60.0f, rows.size() * 34.0f + 20);
        D2D1_RECT_F card = D2D1::RectF(left, y, right, y + h);
        if (Visible(card.top, card.bottom)) {
            Card(card);
            if (rows.empty()) Text(L"Reading…", 13, D2D1::RectF(left + 18, y, right, y + h), g_t.subtext);
            float ry = y + 10;
            for (size_t i = 0; i < rows.size(); ++i) {
                if (i) Divider(left + 18, right - 18, ry);
                Text(rows[i].first, 13, D2D1::RectF(left + 18, ry, left + 140, ry + 34), g_t.subtext);
                Text(rows[i].second, 13, D2D1::RectF(left + 140, ry, right - 18, ry + 34), g_t.text);
                ry += 34;
            }
            if (!rows.empty()) ButtonRight(right - 14, y + 12, L"Copy", icon::Document, Btn::Subtle, Hit::OvCopySpecs);
        }
        y += h + 30;
    }

    // ---- drives
    if (g_haveDrives && !g_drives.drives.empty()) {
        y = SectionTitle(left, right, y, L"DRIVES");
        float rowH = 64;
        float h = g_drives.drives.size() * rowH + 8;
        D2D1_RECT_F card = D2D1::RectF(left, y, right, y + h);
        if (Visible(card.top, card.bottom)) {
            Card(card);
            float ry = y + 4;
            for (size_t i = 0; i < g_drives.drives.size(); ++i) {
                const DriveInfo& d = g_drives.drives[i];
                if (i) Divider(left + 18, right - 18, ry);
                Glyph(icon::Drive, 18, D2D1::RectF(left + 16, ry, left + 48, ry + rowH), g_t.subtext);
                std::wstring name = d.root.substr(0, 2) + (d.label.empty() ? (d.removable ? L" Removable drive" : L" Local disk") : L" " + d.label);
                float used = d.total ? 100.0f * (float)(d.total - d.free) / (float)d.total : 0;
                float br = right - 18 - ButtonWidth(L"Analyze", 0) - 16;
                Text(name, 13.5f, D2D1::RectF(left + 58, ry + 10, br - 170, ry + 30), g_t.text, DWRITE_FONT_WEIGHT_SEMI_BOLD);
                Text(FormatBytes(d.free) + L" free of " + FormatBytes(d.total), 12.5f, D2D1::RectF(br - 260, ry + 10, br - 8, ry + 30), g_t.subtext,
                     DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING);
                float v = Anim(AKey(2200, (int)i), used, 0.18f);
                ProgressBar(D2D1::RectF(left + 58, ry + 38, br - 8, ry + 44), v, used >= 90 ? g_t.bad : used >= 80 ? g_t.warn : g_t.accent);
                ButtonRight(right - 18, ry + 16, L"Analyze", 0, Btn::Secondary, Hit::OvDrive, (int)i, 0, !StorageBusy());
                ry += rowH;
            }
        }
        y += h + 30;
    }
    g_contentH = y + g_scroll;
}

bool ClickOverview(const HotRect& h) {
    switch (h.kind) {
        case Hit::OvAction: {
            if (h.index < 0 || h.index >= (int)g_shown.size()) return true;
            switch (g_shown[h.index].act) {
                case Act::Cleanup: Navigate(Page::Cleanup); break;
                case Act::Startup: Navigate(Page::Startup); break;
                case Act::Tweaks: Navigate(Page::Tweaks); break;
                case Act::Storage:
                    if (const DriveInfo* d = SystemDrive()) StorageScanDrive(d->root);
                    Navigate(Page::Storage);
                    break;
                default: break;
            }
            return true;
        }
        case Hit::OvQuickClean: {
            uint64_t junk = 0;
            if (!CleanupJunk(&junk)) CleanupStartScan();
            else CleanupQuick();
            return true;
        }
        case Hit::OvDrive:
            if (h.index >= 0 && h.index < (int)g_drives.drives.size()) {
                StorageScanDrive(g_drives.drives[h.index].root);
                Navigate(Page::Storage);
            }
            return true;
        case Hit::OvCopySpecs: CopyToClipboard(CopySpecsText()); return true;
        default: return false;
    }
}
