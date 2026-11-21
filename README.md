# Helm

Keep your PC fast, clean and private, from one small window. Helm is part of the larosa023 suite of Windows apps.

**Status: v0.1.0.** Overview, Cleanup, Storage, Startup apps, Tweaks and Tools, with 14 themes and a built-in updater.

## Run it

Download `Helm.exe` from [Releases](https://github.com/larosa023/Helm/releases) and double-click it. It's one ~1 MB file with nothing to install, and it runs without administrator rights. Windows may show SmartScreen the first time because the exe isn't signed yet: **More info → Run anyway**.

Some things (Windows' own temp folder, settings for everyone on the PC, repair tools) need administrator rights. Helm says so where it matters and can restart itself as administrator: **Settings → Administrator**, or the button on any page that needs it.

Portable mode: put an empty file named `portable` next to `Helm.exe`, and its data goes in a `data` folder beside it.

## What's in it

| Page | What it does |
|---|---|
| **Overview** | What needs attention (low disk space, junk, startup apps, privacy settings, pending restarts), live CPU, memory, GPU, disk and network with history, per-thread load, your PC's parts (copy them with one click), and every drive |
| **Cleanup** | Temporary files, Windows temp, thumbnail cache, crash dumps, old update downloads and logs, browser caches (Chrome, Edge, Brave, Vivaldi, Opera, Firefox), Discord/Spotify/Steam/Teams caches, shader caches and the Recycle Bin. You see the size of each before anything is removed |
| **Storage** | Which folders and files use the most space on any drive or folder. Click into folders, jump back with the breadcrumb, open anything in Explorer, or see the largest files on the drive |
| **Startup apps** | Everything that starts with Windows, with switches that work exactly like Task Manager's |
| **Tweaks** | 26 privacy, ad, Explorer, performance and gaming settings, each one switch. **Turn on recommended** does the privacy and ad ones at once. Also: power plans, including adding Ultimate Performance |
| **Tools** | Restore point, system file check, DISM repair, component store cleanup, disk check, network reset, DNS flush, icon refresh, Store cache reset, Explorer restart, and quick links to Ember, Restock, Pitstop, Rigcard, Breaker and Reticle |
| **Themes** | The same 14 themes and theme files as Hangar, plus accent colors |

**Keyboard:** Ctrl+1 to Ctrl+6 switch pages · Ctrl+, opens Settings · F5 refreshes the page · Backspace or Esc goes up a folder in Storage · arrows, Page Up/Down, Home and End scroll.

## Safety

- **Cleanup only touches known cache and temp folders.** It never follows links (junctions, symlinks), never deletes a linked file, and never works on a drive root, your profile, or the Windows folder itself. Temporary files must be at least a day old (update downloads 10 days, logs 30), so nothing an installer is using right now goes. A cache whose app is open (a browser, Discord, Spotify…) is skipped until you close it. Files in use are left alone.
- **Every tweak can be undone.** Before Helm changes a setting it saves what was there, and turning the switch off puts exactly that back.
- **Startup switches don't delete anything.** They use the same "StartupApproved" setting as Task Manager.
- **Storage is read-only.** It shows sizes and opens Explorer; it never deletes.
- **Repair tools are Windows' own** (sfc, DISM, chkdsk, netsh), run in a window you can watch.

## Updates

Helm checks GitHub for new releases in the background (Stable, or Beta in **Settings → Helm updates**), downloads the new `Helm.exe`, and checks its SHA-256 against both GitHub's digest and the release's `SHA256SUMS.txt`. It installs the update when you click **Restart to update**, or by itself when you're not using Helm. You stay on the page you were on.

If a new version doesn't start, Helm puts the old one back and skips that version. The last two versions are kept, so **Settings → Helm updates → Roll back** can take you back.

Helm also reads Hangar's release index, so update checks don't count against GitHub's hourly limit.

## Build

```bash
./build.sh                         # Linux/macOS, needs Zig: pip install ziglang
cmake -S . -B build -A x64 && cmake --build build --config Release   # Windows, VS 2022
```

Native C++17 with Win32 and Direct2D, like Hangar, Foyer, Strata and Breaker. A release is published automatically when `HELM_VERSION` in `src/common.h` changes on `main`, after the exe has been started on a real Windows machine in CI. Versions containing a `-` are published as pre-releases (the Beta channel).

## Docs

- [Architecture](docs/ARCHITECTURE.md)
- [Themes and the theme file format](docs/THEMES.md)
