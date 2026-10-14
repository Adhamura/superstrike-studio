# SuperStrike Studio — UI design spec (C++ / GTK 4 / libadwaita 1.10)

Goal: G HUB parity in function, cleaner than G HUB in layout. Dark first (libadwaita prefers
dark), one accent (#3b9cff). Everything aligns to an 8 px grid. No emoji in the UI.

## Window

```
+------------------+------------------------------------------------------------+
| [mouse] PRO X3   |  <page title>            (subtitle: Profile 2 · Active)  [↺][Save to mouse] |
| ● Connected 82%  +------------------------------------------------------------+
|                  |  (AdwBanner when not connected: "Mouse not found …" [Retry])|
|  ▣ HITS          |                                                            |
|  ▣ Buttons       |                       page content                          |
|  ▣ Sensor        |                                                            |
|  ▣ Profiles      |                                                            |
|  ▣ Device        |                                                            |
|                  |                                                            |
| [Editing        ]|                                                            |
| [Profile 2 ▾    ]|                                                            |
+------------------+------------------------------------------------------------+
```
- Sidebar 232 px (`AdwOverlaySplitView`, collapses under 900 px). Device card at top:
  32 px mouse glyph, name (bold 13 px), status line (12 px dim): green dot + "Connected ·
  82 %" or grey dot "Searching…". Nav rows 44 px, icon 18 px + label 14 px, 12 px gap.
- Bottom of sidebar: "EDITING" section title + profile dropdown (full width).
- Header bar: `AdwWindowTitle` (page name, subtitle "Profile 2 · Active" / "Profile 3 ·
  not active"). End: revert icon button (sensitive only when dirty) and the suggested
  "Save to mouse" button; when dirty its label is "Save to mouse · 2".
- Toasts for results. Errors never use dialogs except destructive confirmations.

## Style tokens (assets/style.css — classes pages may use)

| class | use |
|---|---|
| `.ss-panel` | raised card: bg alpha(white,.045), 1 px border alpha(white,.07), radius 16, padding 20 |
| `.ss-title` | section title: 11 px, weight 800, letter-spacing .08em, uppercase look, opacity .62 |
| `.ss-value` | big number: 30 px, weight 800, tabular numbers |
| `.ss-unit` | unit next to a big number: 13 px, opacity .6 |
| `.ss-hint` | helper text: 12 px, opacity .62, wraps |
| `.ss-chip` | pill toggle button (radius 999, padding 6 14); `:checked` = accent bg |
| `.ss-row-selected` | selected list row (accent tint) |
| `.ss-pad` | test pad: dashed accent border, radius 14 |
| `.ss-badge` | small pill label (11 px, padding 2 8, radius 999); `.ss-badge.accent`, `.ss-badge.warn` |

Spacing: page padding 24; gap between panels 16; inside a panel 12 between rows, 20 between
groups. Panels never touch the window edge. Text left aligned; numbers right aligned in rows.

## Icons (assets/icons/hicolor/scalable/actions/*-symbolic.svg, made in Inkscape)

`ss-hits` `ss-buttons` `ss-sensor` `ss-profiles` `ss-device` (nav); `ss-actuation`
`ss-rapid` `ss-haptics` (HITS values); `ss-rotate`, `ss-mouse` (sidebar glyph), `ss-code`
(HITS code). Use with `ui::icon("ss-hits-symbolic", 18)`. Other icons: standard symbolic names
(`edit-copy-symbolic`, `document-save-symbolic`, `edit-undo-symbolic`, `view-more-symbolic`).

## Page: HITS (`page_hits.cpp`)

Two columns, no outer scroll (fits 1280×800):
- **Left: configuration panel** `.ss-panel`, width 380, full height.
  1. Title row: `HITS CONFIGURATION` (.ss-title) + spacer + `ss-code` menu button
     (Copy HITS code / Import HITS code…).
  2. Target: `AdwToggleGroup` Left | Right | Both (L+R), full width. Clicking a button on
     the mouse picture selects it too. "Both" writes the same values to L and R.
  3. Tabs: `AdwInlineViewSwitcher` over an `AdwViewStack`: Actuation · Rapid trigger ·
     Haptics · Presets.
     - Actuation: hint "Where along the press the click fires. Short = faster, long = fewer
       misclicks."; value row: `.ss-value` "5" + `.ss-unit` "≈ 0.5 mm" + spacer + reset
       icon button (back to 5); the `TravelBar`.
     - Rapid trigger: `AdwSwitchRow` "Rapid trigger" (boxed list); value row with level;
       `LevelBar` 1..5 ("Most sensitive" … "Least"); the same `TravelBar` view (second
       instance) showing the orange reset zone; hint "After a click, lifting the button by
       this much re-arms it, so fast repeated clicks need less travel."
     - Haptics: value row; `LevelBar` 0..5 ("Off" … "Strongest"); hint "Click feel strength.
       Higher levels use more battery."
     - Presets: "YOUR PRESETS" list (boxed-list `AdwActionRow`s, subtitle = values summary,
       trash suffix) + "Save current as preset…" button; "PRO PRESETS" list (PRO_PRESETS),
       subtitle "L 6·1·5  R 6·1·3" when L≠R else "Act 6 · RT 1 · Haptics 5".
  When "Both" is selected and L≠R, the panel shows the left values and a `.ss-badge.warn`
  "L and R differ — changes apply to both".
- **Right: mouse stage** (expands): `MouseView` top view, zones btn-left/btn-right, grid
  on, callouts **Boxes** (G HUB look): left box beside the left button, right box beside the
  right button, 3 lines each: "\x01 5" (actuation), "\x02 2" or "\x02 off" (rapid trigger),
  "\x03 3" (haptics). Selected button = accent box border. Thumbnail on.
  Under the mouse: test pad `.ss-pad` 72 px high: "Test here — click with any button" and live
  counters "L 0 · R 0 · M 0 · Back 0 · Fwd 0 · 0 clicks/s · held 0 ms".
- Live preview: every change calls `app.live_hits(b)` (it only sends when the edited
  profile is the active one) and `app.mark_dirty(sector)`.

## Page: Buttons (`page_buttons.cpp`)

- Left (expands): toolbar row: `AdwToggleGroup` Top | Left side (bound to the MouseView view
  both ways) + `ss-rotate` button; spacer; `AdwToggleGroup` Normal | DPI-shift layer.
  `MouseView` with callouts Lines (label_for → "Left\nLeft click"), flip on, thumbnail on.
  Hint under it (.ss-hint, centred): "Click a button on the mouse to change it. Drag sideways
  to rotate."
- Right panel `.ss-panel`, width 400, scrolls inside:
  1. `BUTTONS` title + boxed list of the 5 buttons (`AdwActionRow`, title = BUTTON_NAMES[i],
     subtitle = action). Selected row = `.ss-row-selected`. Clicking selects (and rotates the
     view to show that button).
  2. `ACTION` title + chips (`.ss-chip` toggle buttons in a `GtkFlowBox`, 3 per line): Mouse ·
     Keyboard · Media · Function · Disabled · Default.
  3. Detail (boxed list): Mouse → `AdwComboRow` of MOUSE_BUTTONS. Keyboard → large
     "Record a key…" button (dialog captures key + modifiers via evdev keycode → HID) +
     modifier chips Ctrl Shift Alt Super + `AdwComboRow` (searchable) of KEYS. Media → combo
     of MEDIA. Function → combo of FUNCTIONS. Raw (macro) → read-only row with hex.
  4. Flat button "Restore factory action" (from `app.factory`).
- Saving with no Left click anywhere in the profile's first 5 slots asks for confirmation
  (handled in `App::save`).

## Page: Sensor (`page_sensor.cpp`) — `ui::page_column`

1. `DPI STAGES` panel: a row of 5 stage chips (toggle group look, each "1\n800", disabled
   stages dimmed "off"), the default stage chip shows a small dot. Below: editor for the
   selected stage in a boxed list: `AdwSwitchRow` Enabled; `AdwSpinRow` DPI (100..48000, snapped
   to info.dpi_list); `AdwSwitchRow` "Separate X / Y"; `AdwSpinRow` DPI Y (only when separate);
   `AdwComboRow` Lift-off distance Low/Medium/High; `AdwSwitchRow` "Default after power-on";
   `AdwButtonRow` "Use this stage now" (only active profile).
2. `POLLING RATE` panel: two rows: "Wireless" with chips 125 250 500 1K 2K 4K 8K; "Cable" with
   125 250 500 1K (chips = `AdwToggleGroup`).
3. `TRACKING` panel: `AdwSwitchRow` Angle snapping; `AdwComboRow` Gaming surface mode
   (Auto/On/Off, device-wide, applies at once via app.run); `AdwSpinRow` Scroll filter (ms,
   0..1000 step 10, 0 = off).
4. `POWER` panel: `AdwSpinRow` "Low power after (s)"; `AdwSpinRow` "Sleep after (s)".

## Page: Profiles (`page_profiles.cpp`) — `ui::page_column`

- Intro hint: "Profiles live inside the mouse and work on any computer without this app."
- `GtkFlowBox` of profile cards (`.ss-panel`, min width 280, 2–3 per line): big slot number
  (`.ss-value`), name `GtkEditableLabel` (placeholder "Profile N"), badges (Active / In cycle /
  Unsaved), `AdwSwitchRow`-like compact switch "In profile cycle", footer buttons: "Edit"
  (sets edit + shows HITS page), "Activate" (suggested when not active), menu button
  (`view-more-symbolic`): Reset to factory, Copy settings from → Profile N, Export…, Import….
- `CONTROL MODE` panel: combo Onboard memory (recommended) / Host software.
- `BACKUP` panel: "Back up whole memory…" and "Restore from backup…" (GtkFileDialog, JSON,
  default folder `backup_dir()`), hint about the automatic backup before the first save.

## Page: Device (`page_device.cpp`) — `ui::page_column`

Boxed lists: Name, Connection, Battery, Firmware (multi-line), Onboard memory; `LOOK` panel:
mouse colour (`GtkColorDialogButton`, saves cfg.shell, calls reload); `HID++ FEATURES`
expander with one row per feature; an "About SuperStrike Studio" button row (AdwAboutDialog:
version, website, issue link, MIT license).

## Behaviour rules

- Pages never call the device directly: use `app.run(job, done)`.
- Pages refresh only in their `on_reload` callback; guard widget→model handlers with a
  `bool loading` flag while filling widgets.
- Every edit: change `*app.cur()`, then `app.mark_dirty(app.edit)`.

## Internationalisation (GNU gettext, domain `superstrike-studio`)

- `src/i18n.hpp`: `_()` translate, `N_()`/`NC_()` mark static table entries (show them with `_()`/`C_()`),
  `C_(ctx, s)` translate with context, `ngettext` for plurals, `fmt()` for sentences with values
  (never build a sentence by concatenation; put a `// TRANSLATORS:` comment right above the call).
- `i18n_init(cfg.lang)` runs in `main()` before GTK starts. `cfg.lang` ([look] lang) is `system` or a code
  from `LANGUAGES`; the Device page picks it and offers a restart.
- Not translated: product and technical names (PRO X3 SUPERSTRIKE, HITS, DPI, HID++, LIGHTSPEED, G HUB),
  hex, file paths, pro player names, config and JSON keys, device error text.
- Translators: `po/update-pot.sh`, then `msginit -l <code> -i po/superstrike-studio.pot -o po/<code>.po`.
  CMake compiles every `po/<code>.po` into `assets/locale/<code>/LC_MESSAGES/superstrike-studio.mo`.

## Platforms (Linux and Windows)

- HID++ framing (`src/hidpp.cpp`) is shared. Raw HID access is one backend per OS behind `src/hid_backend.hpp`:
  `hidpp_linux.cpp` (hidraw via sysfs) and `hidpp_win.cpp` (Win32 HID API, overlapped I/O). CMake picks one.
- Windows has no short-report node, so the backend sets `Transport::long_only` and every request is a long report.
  `find_mouse_input_nodes()` is empty there; the live click animation uses window events only.
- `asset_path()` on Windows starts from the exe folder (`platform_win.cpp`). `i18n_set()` flushes gettext with `textdomain()`.
- Windows build kit: `tools/windows/build-windows.sh` (MSYS2 UCRT64) and `tools/windows/README-windows.txt`.
