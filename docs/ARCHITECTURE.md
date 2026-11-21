# Helm architecture

## Stack

Native C++17, Win32 + Direct2D/DirectWrite, cross-compiled with Zig (`./build.sh`) or built with MSVC (CMake). It's the same stack as Hangar, Foyer, Strata and Breaker, and it shares their code for drawing, themes, JSON, networking and the self-updater.

One ~1 MB exe, no runtime. Live monitoring only runs while the Overview is on screen, and the window does no work when it's minimized.

## Threads

The **UI thread** owns all state and draws everything. Work that touches the disk, the network or other processes runs on background threads and reports back with `PostMessage` (`WM_HELM_*` in `common.h`); the message's `lParam` is a heap object the window takes over. The UI never waits on I/O, with two exceptions that are instant: registry writes for startup switches and tweaks.

| Module | Thread | Does |
|---|---|---|
| `sysinfo.cpp` | one-shot + a sampler thread | Specs (registry, SMBIOS, DXGI); once-a-second samples while the Overview is visible: CPU (all and per thread), memory, GPU (NVML on NVIDIA, else PDH), disks (PDH), network (`GetIfTable2`) |
| `cleanup.cpp` | one at a time | Scans and cleans the categories in `CleanCategories()` |
| `storage.cpp` | one at a time | Walks a drive into a size tree; folders under 1 MB are folded into their parent as soon as their size is known |
| `startup.cpp` | one-shot | Run keys and Startup folders, with `StartupApproved` state |
| `tweaks.cpp` | UI thread (+ a thread for broadcasts) | Registry tweaks with backups; power plans |
| `icons.cpp` | one worker, exits when idle | Program icons via WIC |
| `tools.cpp` | per tool | Repair tools; finding the other suite apps |
| `selfupdate.cpp` | one at a time | Helm's own updater |

When Helm exits, it ends the process directly (`ExitProcess`), so a scan that's still running stops cleanly instead of racing global destructors.

## Cleanup rules

See the top of `src/cleanup.h`. Each category is a list of targets (folder, file patterns, recursive or not, minimum age, needs admin, app that must be closed). `SafeTarget()` refuses drive roots, folders less than two levels deep, and the folders that hold everything else (profile, AppData, Windows, Program Files, ProgramData, Documents, Desktop, Downloads). The walker never follows or deletes reparse points.

## Tweaks

A tweak is a list of registry values with an "on" value and an "off" value (or "off" = delete the value, or delete a key). Turning one on saves what was there to `tweaks-backup.json` the first time; turning it off restores that exactly and forgets the backup. Tweaks under `HKEY_LOCAL_MACHINE` need administrator rights and are marked "Admin".

## Updates

The same design as Hangar's (see `src/selfupdate.h`): verified download into `updates\<version>\`, rename-and-swap of the running exe, a named "healthy" event from the new copy, automatic rollback if it doesn't arrive in 20 seconds, the previous two versions kept in `versions\` for manual rollback.

## Data

`%LOCALAPPDATA%\Helm\` (or `.\data` next to the exe in portable mode): `settings.json`, `tweaks-backup.json`, `helm.log`, `themes\`, `cache\`, `updates\`, `versions\`.

## Testing

CI builds the exe on Linux, then starts it on a Windows runner on every page, takes screenshots and quits it, before anything is released. Locally, the exe runs under Wine (start it with a clean environment: `env -i HOME=$HOME PATH=/usr/bin:/bin DISPLAY=:0 wine Helm.exe`). `HELM_API_BASE` and `HELM_INDEX_URL` point the updater at a local test server.
