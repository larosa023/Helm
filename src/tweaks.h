// Helm - privacy, Windows, performance and gaming settings, each one switch you can turn back off.
//
// Every tweak is a handful of registry values. Before Helm changes one, it saves what was there
// (DataDir()\tweaks-backup.json); turning the tweak off puts exactly that back. Without a backup
// (say, it was already on), it puts back Windows' default.
#pragma once
#include "common.h"

struct RegValue {
    HKEY root;
    std::wstring path, name;          // name "" = the key's default value
    bool isString = false;
    DWORD on = 0, off = 0;
    std::wstring onStr, offStr;
    bool offDeletes = false;          // "off" = remove the value (Windows' default is no value)
    std::wstring offKey;              // "off" = remove this whole key instead (it contains `path`)
    bool absentIsOn = false;          // no value at all counts as on (e.g. Game Mode)
};

enum class AfterTweak { None, Explorer, SignOut, Restart };

struct Tweak {
    std::wstring id, group, name, description;
    std::vector<RegValue> values;
    bool admin = false;               // writes under HKEY_LOCAL_MACHINE
    bool recommended = false;         // part of "Apply recommended"
    AfterTweak after = AfterTweak::None;
    int minBuild = 0;                 // Windows build that has the feature
    int live = 0;                     // tells Windows right away (see tweaks.cpp)
};

const std::vector<Tweak>& AllTweaks();   // only the ones this Windows build supports

namespace tweaks {

bool IsOn(const Tweak& t);
bool Set(const Tweak& t, bool on, std::wstring* error);
bool HasBackup(const std::wstring& id);

// Restarts File Explorer gracefully (so taskbar/menu changes show up). Returns right away; the
// restart happens in the background.
void RestartExplorer();

// Power plans
struct PowerPlan { GUID guid; std::wstring name; bool active = false; };
std::vector<PowerPlan> PowerPlans();
bool SetPowerPlan(const GUID& g, std::wstring* error);
bool HasUltimatePlan();
bool AddUltimatePlan(std::wstring* error);

} // namespace tweaks
