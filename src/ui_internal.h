// Helm - what the window's pieces share: drawing state, hit testing, widgets, navigation.
// Everything here lives on the UI thread.
#pragma once
#include "common.h"
#include "gfx.h"
#include "selfupdate.h"
#include <functional>

using namespace gfx;

enum class Page { Overview, Cleanup, Storage, Startup, Tweaks, Tools, Themes, Settings, Count };

enum class Hit {
    None, Nav, ToastClose, ScrollThumb, ScrollTrack, WhatsNewClose, Elevate, SelfRestart, Modal,
    // overview
    OvAction, OvQuickClean, OvDrive, OvCopySpecs,
    // cleanup
    ClPick, ClClean, ClRescan, ClCancel, ClAll, ClNone, ClDismiss, ClCloseApp,
    // storage
    StDrive, StScan, StCancel, StFolder, StUp, StCrumb, StOpen, StTab, StFile, StBig, StClose,
    // startup
    SuToggle, SuOpen, SuRefresh,
    // tweaks
    TwToggle, TwRecommended, TwRestorePoint, TwRestartExplorer, TwPower, TwAddUltimate, TwDismiss,
    // tools
    ToRun, ToExplorer, ToSuite,
    // themes
    ThemePick, AccentPick, ThemeExport, ThemeFolder, ThemeReload, ThemeBrowse,
    // settings
    SelfCheck, SelfChannel, SelfAuto, SelfRollback, SetHours, SetAutoScan, SetOpenData, SetOpenLog, SetRepo,
};

struct HotRect {
    D2D1_RECT_F r;
    Hit kind;
    int index;
    int arg;
    bool enabled;
};

// ---------------------------------------------------------------- shared state (ui.cpp)
extern HWND g_hwnd;
extern ID2D1HwndRenderTarget* g_rt;
extern Theme g_t;
extern Canvas g_c;
extern float g_W, g_H, g_scale;
extern float g_scroll, g_scrollTarget, g_contentH, g_viewH;
extern Page g_page;
extern HotRect g_hover, g_pressed;
extern bool g_animations;

static const float kSidebar = 232;
static const float kPad = 32;

void Invalidate();
void StartAnim();
void RepaintNow();                    // after a list changed under the mouse: rebuild hit areas right away
void Navigate(Page p);
void Toast(const std::wstring& text, bool bad = false);
void OpenUrl(const std::wstring& url);
void LaunchAsUser(const std::wstring& target);      // opens a file, app or URL as you, even from an elevated Helm
void OpenFolder(const std::wstring& path);          // opens a folder in Explorer
void ShowInFolder(const std::wstring& file);        // opens Explorer with the file selected
bool Busy();                                        // cleaning or scanning (Helm shouldn't restart now)
void RelaunchElevated();                            // asks for admin rights and restarts Helm on this page
std::wstring PageId(Page p);
Page PageFromId(const std::wstring& id);
void ThemeChanged();                                // after g_t changed: window frame, render target

// Helm's own updates
extern SelfUpdateState g_self;
extern std::vector<selfupdate::Previous> g_prevVersions;
void CheckSelf(bool force);
void ApplySelf(const std::wstring& exe, const std::wstring& version, bool userAsked);

// ---------------------------------------------------------------- hit testing and animation
void AddHot(const D2D1_RECT_F& r, Hit kind, int index = -1, int arg = 0, bool enabled = true);
bool IsHover(Hit kind, int index = -1, int arg = 0);
bool IsPressed(Hit kind, int index = -1, int arg = 0);
// A value that eases toward `target` over a few frames. `key` identifies it between frames.
float Anim(uint64_t key, float target, float rate = 0.22f);
inline uint64_t AKey(int a, int b = 0, int c = 0) { return ((uint64_t)(uint32_t)a << 40) ^ ((uint64_t)(uint32_t)b << 20) ^ (uint64_t)(uint32_t)c; }

// ---------------------------------------------------------------- widgets
enum class Btn { Primary, Secondary, Subtle, Danger };
float ButtonWidth(const std::wstring& label, wchar_t glyph);
void Button(const D2D1_RECT_F& r, const std::wstring& label, wchar_t glyph, Btn style, Hit kind, int index = -1, int arg = 0, bool enabled = true);
// right-aligned button ending at `right`; returns its left edge
float ButtonRight(float right, float top, const std::wstring& label, wchar_t glyph, Btn style, Hit kind, int index = -1, int arg = 0, bool enabled = true);
float Pill(float x, float y, const std::wstring& text, D2D1_COLOR_F color, bool rightAlign = false);
void ProgressBar(const D2D1_RECT_F& r, float pct, D2D1_COLOR_F color);
void Toggle(float x, float y, bool on, Hit kind, int index, int arg = 0, bool enabled = true);   // 40 × 22
void Checkbox(float x, float y, bool checked, bool enabled, bool hot);                          // 20 × 20
float Segmented(float x, float y, const std::vector<std::wstring>& opts, int sel, Hit kind, bool rightAlign = false);
void Glyph(wchar_t g, float size, const D2D1_RECT_F& r, D2D1_COLOR_F c);
void Text(const std::wstring& s, float size, const D2D1_RECT_F& r, D2D1_COLOR_F c, DWRITE_FONT_WEIGHT w = DWRITE_FONT_WEIGHT_NORMAL,
          DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING);
void TextWrap(const std::wstring& s, float size, const D2D1_RECT_F& r, D2D1_COLOR_F c);
float WrapHeight(const std::wstring& s, float size, float width);
void Card(const D2D1_RECT_F& r, bool hover = false);
void Divider(float left, float right, float y);
// A message strip with a glyph, text and an optional button. Returns its height.
float Banner(float left, float right, float y, wchar_t glyph, D2D1_COLOR_F color, const std::wstring& text,
             const std::wstring& button = L"", Hit kind = Hit::None, int index = -1, wchar_t buttonGlyph = 0);
// Page title (+ subtitle); returns the y below it
float PageTitle(float left, float right, float y, const std::wstring& title, const std::wstring& sub);
float SectionTitle(float left, float right, float y, const std::wstring& text);
// Program icon (from the icon cache); draws `fallback` until it's loaded or when there's none
void ProgramIcon(const std::wstring& path, const D2D1_RECT_F& r, wchar_t fallback);
// Content area of the current page: [left, right] and whether rows at y..y+h are on screen
struct PageBox { float left, right; };
PageBox PageLayout(float x0, float maxWidth);
inline bool Visible(float top, float bottom) { return bottom > 0 && top < g_H; }

// ---------------------------------------------------------------- pages
// each page: Draw, Click (true if it handled the hit), and notifications
void DrawOverview(float x0);
bool ClickOverview(const HotRect& h);
void OverviewEnter();
void OverviewOnSpecs(struct Specs* s);
void OverviewOnSample(struct Sample* s);
void OverviewOnDrives(struct DriveList* d);
void OverviewOnHealth(struct HealthInfo* h);
const struct DriveList& Drives();

void DrawCleanup(float x0);
bool ClickCleanup(const HotRect& h);
void CleanupOnScan(struct CleanScanResult* r);
void CleanupOnProgress(struct CleanProgress* p);
void CleanupOnDone(struct CleanDone* d);
void CleanupQuick();                     // clean the default picks (Overview's "Clean now")
bool CleanupJunk(uint64_t* bytes);       // false until the first scan finishes
bool CleanupBusy();
void CleanupStartScan();

void DrawStorage(float x0);
bool ClickStorage(const HotRect& h);
bool KeyStorage(WPARAM vk);
void StorageOnProgress(struct StorageProgress* p);
void StorageScanDrive(const std::wstring& root);
bool StorageBusy();

void DrawStartup(float x0);
bool ClickStartup(const HotRect& h);
void StartupOnList(struct StartupList* l);
void StartupReload();
bool StartupCounts(int* enabled, int* total);   // false until loaded

void DrawTweaks(float x0);
bool ClickTweaks(const HotRect& h);
void TweaksEnter();
int TweaksRecommendedOff();

void DrawTools(float x0);
bool ClickTools(const HotRect& h);
void ToolsEnter();
void ToolsOnResult(int index, bool ok);
void ToolsOnSuite(std::vector<struct SuiteApp>* apps);

void DrawThemes(float x0);
bool ClickThemes(const HotRect& h);
void ThemesInit();
void ThemesDiscardDevice();             // the render target is going away
void ApplyTheme();

void DrawSettings(float x0);
bool ClickSettings(const HotRect& h);
void SettingsEnter();
