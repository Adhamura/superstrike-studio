<p align="center"><img src="assets/logo-lockup.svg" alt="SUPERSTRIKE Studio" width="420"></p>

# SUPERSTRIKE Studio

A native editor for the **Logitech PRO X3 / PRO X2 SUPERSTRIKE** gaming mouse — a lightweight
alternative to G HUB for **Linux** and **Windows**. It talks to the mouse directly over HID++,
needs no background service, and writes everything into the mouse's **onboard memory**, so your
settings work on any computer without this app.

> Not affiliated with or endorsed by Logitech. Use at your own risk; the app makes a full memory
> backup before the first save of each session.

## Features

- **HITS tuning** (Haptic Inductive Trigger System), per button or both:
  actuation point (1–10, G HUB-style press-travel bar), rapid trigger on/off + level (1–5),
  click haptics (0–5), live preview on the mouse, presets, **G HUB HITS codes** (import / copy)
  and the published pro-player codes.
- **Buttons**: remap left, right, middle, back and forward on a top / left-side drawing of the
  mouse — mouse buttons, keyboard keys with modifiers (or record a key), media keys, DPI and
  profile functions; normal and DPI-shift layer.
- **Sensor**: 5 DPI stages (100–48 000, separate X/Y, lift-off distance, power-on stage, switch
  stage live), polling rate up to 8 000 Hz (wireless) / 1 000 Hz (cable), angle snapping,
  scroll filter, gaming surface mode, power-saving timeouts.
- **Profiles**: 5 onboard profiles — rename, profile cycle, activate, copy, reset to factory
  (from the mouse ROM), export / import JSON, whole-memory backup and restore.
- **Live view**: the drawing reacts to real clicks and wheel scrolling.
- **21 languages**, switchable at run time: English, 简体中文, 繁體中文（台灣）, 日本語, 한국어,
  Русский, Українська, Español, Português (Brasil), Français, Deutsch, Italiano, Polski, Türkçe,
  العربية, हिन्दी, বাংলা, اردو, Bahasa Indonesia, Tiếng Việt, ไทย (right-to-left layout for
  Arabic and Urdu).
- Small and quiet: C++ with the plain GTK 4 / libadwaita C API, one ~350 KB binary, no CPU use
  while idle.

## Build and install — Linux

Dependencies (Arch names): `gtk4 libadwaita librsvg json-glib cmake gettext` and a C++20 compiler.

```bash
./install.sh
```

This builds `build/release/superstrike-studio` and adds an app-menu entry and a desktop launcher.
If the app reports no permission for `/dev/hidraw*`, run once:

```bash
sudo ./install.sh --udev
```

A read-only device check is built too: `build/release/sscheck`.

## Build — Windows

See [`tools/windows/README-windows.txt`](tools/windows/README-windows.txt): install MSYS2, install
the listed packages in the **UCRT64** shell, run `tools/windows/build-windows.sh`. The result is a
portable folder and a zip in `dist/`. Close Logitech G HUB before using the app.

## Project layout

| Path | What |
|---|---|
| `src/hidpp*.cpp` | HID++ 2.0 transport (Linux hidraw, Windows HID API) |
| `src/device.cpp` | SUPERSTRIKE features: HITS `0x1B0C`, report rate `0x8061`, DPI `0x2202`, onboard profiles `0x8100`, … |
| `src/profile.cpp` | onboard profile sector format, button actions, HITS codes |
| `src/app.cpp`, `src/window.cpp` | worker thread, saving, window |
| `src/page_*.cpp` | the five pages |
| `src/mouseview*.cpp`, `src/levelbars.cpp` | drawn widgets (mouse, travel bar, level bars) |
| `assets/` | mouse drawings, icons, logo, stylesheet |
| `po/` | translations (`po/update-pot.sh` refreshes the template) |
| `DESIGN.md` | UI design spec |

## Credits

Protocol knowledge cross-checked with [Solaar](https://github.com/pwr-Solaar/Solaar),
[xatuke/logitech-superstrike-pro-x2-controller](https://github.com/xatuke/logitech-superstrike-pro-x2-controller),
[mclol0/linux-superstrike](https://github.com/mclol0/linux-superstrike),
[kazehana99k/Logitech-PRO-X2-SUPERSTRIKE-Linux-haptics-actuation-control-](https://github.com/kazehana99k/Logitech-PRO-X2-SUPERSTRIKE-Linux-haptics-actuation-control-)
and [OpenMouse-Project/mouse-protocol](https://github.com/OpenMouse-Project/mouse-protocol), and
verified on a real PRO X3 SUPERSTRIKE.
