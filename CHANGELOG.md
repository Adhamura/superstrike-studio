# Changelog

## 1.1.1

- **One file per system in the releases, no more archives.** Windows gets
  `SuperStrike-Studio-<version>-windows-x64.exe` (just run it, no zip to unpack) and Linux gets
  `SuperStrike-Studio-<version>-x86_64.AppImage` (GTK 4 and libadwaita inside, runs on any x86_64
  distro). The Windows exe unpacks itself once to `%LOCALAPPDATA%\SuperStrike-Studio` on the
  first start and reuses it afterwards.
- **`build-windows.bat`**: double-click it to build on Windows. It installs MSYS2 (with winget) and
  the build packages when they are missing, then makes the same one-file exe.
- CI builds and starts both one-file releases with the demo mouse.

## 1.1.0

- **Check for updates from GitHub.** When the app starts it asks GitHub for the latest release, at
  most once a day. If there is a newer version it shows the version and its release notes, with
  **Open download page** (the release on GitHub), **Later** and **Skip this version**.
- **Device → Check for updates** checks right away; **Check for updates automatically** turns the
  daily check off. `superstrike-studio --check-updates` does it from a terminal.
- Privacy: the check is one HTTPS request to `api.github.com`; nothing about you or the mouse is
  sent and nothing is installed by itself. TLS is handled by the system (glib-networking on Linux,
  WinHTTP on Windows), so the app gains no new libraries.
- CI also checks that the app reaches GitHub over HTTPS on Linux and on Windows.
- Fix: the sidebar no longer changes width from one start to the next (the long "Searching for the
  mouse…" status widened it), so the HITS tabs are never cut off.

## 1.0.0

The first release of **SuperStrike Studio**, a native editor for the Logitech **PRO X3 / PRO X2
SUPERSTRIKE** on Linux and Windows.

- **Everything saves into the mouse.** HITS actuation, rapid trigger and click **haptics**, button
  actions, DPI stages and polling rate are written into the mouse's onboard profiles. They keep
  working on any PC, in any game, with this app closed and no Logitech software installed.
- **HITS tuning** for the left and right button, separately or both at once: actuation point
  1–10 on a press-travel bar, rapid trigger on/off and level 1–5, haptics 0–5, live preview on the
  mouse while you edit, your own presets, 12 published pro-player presets, and G HUB HITS codes
  (import and copy).
- **Buttons**: remap left, right, middle, back and forward on a top and left-side drawing of the
  mouse: mouse buttons, keyboard keys with modifiers (or record a key), media keys, DPI and profile
  functions. Normal and DPI-shift layer.
- **Sensor**: five DPI stages from 100 to 48 000 (separate X / Y, lift-off distance, power-on stage,
  switch the stage live), polling rate up to 8 000 Hz wireless and 1 000 Hz cable, angle snapping,
  scroll filter, gaming surface mode, power-saving timeouts.
- **Profiles**: five onboard profiles: rename, profile cycle, activate, copy, reset to factory (from
  the mouse ROM), export / import JSON, whole-memory backup and restore. An automatic backup of the
  whole memory is made before the first save of each session; every write is read back and checked.
- **Live view**: the mouse drawing reacts to your real clicks and wheel.
- **Demo mode**: `superstrike-studio --demo` runs every page with a simulated PRO X3 SUPERSTRIKE, so
  you can try the app before the mouse arrives. Nothing is written to a real mouse.
- **21 languages**, switchable while the app runs, with right-to-left layout for Arabic and Urdu.
- **Light on your PC**: one ~370 KB native binary (C++ / GTK 4 / libadwaita). No background service,
  no tray process, no network access, no account. About 0.05 % CPU while open and idle; nothing at
  all once closed.
- **Windows**: a portable zip (no installer), built and smoke-tested by GitHub Actions on every
  change. **Linux**: `./install.sh` builds it and adds an app-menu entry; `--udev` adds the hidraw rule.
