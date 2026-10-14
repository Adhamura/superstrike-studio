# Security

Please report security problems privately through GitHub: **Security → Report a
vulnerability** on this repository (or open an issue that only says you have something to
report, without details).

## What SuperStrike Studio touches

- **Only the mouse.** The app talks HID++ 2.0 to Logitech devices (vendor `046d`) through
  `/dev/hidraw*` on Linux and the Win32 HID API on Windows. It runs as your user. On Linux,
  `sudo ./install.sh --udev` adds one udev rule (`TAG+="uaccess"` for Logitech hidraw nodes) so the
  logged-in user can open them without root.
- **No network.** The app sends and receives nothing over the network. There is no update check,
  telemetry or account.
- **No background service.** Nothing is installed to run at start-up. When the window is closed,
  no part of the app runs; the mouse keeps its settings in its own memory.
- **Files.** Settings in `~/.config/superstrike/studio.ini` (Windows: `%LOCALAPPDATA%\superstrike`),
  memory backups in `~/.local/share/superstrike/backups`, and the JSON files you export or import.

## Data safety

- Before the first save of each session the app writes a **backup of the whole onboard memory**.
  **Profiles → Restore from backup…** puts it back.
- Every sector is written with its CRC and **read back and compared**; a mismatch is reported and
  nothing else is written. The factory profile in the mouse ROM (sector `0x101`) is never written.
- Saving a profile without any Left click asks for confirmation first.

## Release downloads

Release files are built by GitHub Actions from the tagged commit and listed with their SHA-256 in
`SHA256SUMS`. Check them with `sha256sum -c SHA256SUMS --ignore-missing`. The Windows exe is not
signed with a paid code-signing certificate, so SmartScreen may warn about it.
