### Install

**Windows 10 / 11 (64-bit):** download **SuperStrike-Studio-@VERSION@-windows-x64.exe** and run
it. One file: no installer, no zip, no service, no account (the first start unpacks it to
`%LOCALAPPDATA%\SuperStrike-Studio` and takes a few seconds). Close Logitech G HUB first (also
in the tray). SmartScreen may say "Windows protected your PC" because the exe is not signed
with a paid certificate: click **More info → Run anyway**.

**Linux (x86_64):** download **SuperStrike-Studio-@VERSION@-x86_64.AppImage**, then
```bash
chmod +x SuperStrike-Studio-@VERSION@-x86_64.AppImage && ./SuperStrike-Studio-@VERSION@-x86_64.AppImage
```
Everything it needs is inside (GTK 4, libadwaita). If it says it has no permission for
/dev/hidraw*, add this udev rule once:
```bash
echo 'KERNEL=="hidraw*", SUBSYSTEM=="hidraw", ATTRS{idVendor}=="046d", TAG+="uaccess"' | sudo tee /etc/udev/rules.d/70-superstrike.rules
sudo udevadm control --reload-rules && sudo udevadm trigger --subsystem-match=hidraw
```

**macOS (Apple silicon or Intel):** download **SuperStrike-Studio-@VERSION@-macos-arm64.dmg**
(Apple silicon: M1 and later) or **SuperStrike-Studio-@VERSION@-macos-x86_64.dmg** (Intel), open it and
drag **SuperStrike Studio** to **Applications**. The app is not notarized by Apple (that needs a paid
developer account), so the first start is blocked: open **System Settings → Privacy & Security**
and click **Open Anyway** (or run
`xattr -dr com.apple.quarantine "/Applications/SuperStrike Studio.app"` once). Close Logitech
G HUB first.

**No mouse yet?** Start it with `--demo` to try every page with a simulated PRO X3 SUPERSTRIKE.

Check downloads with `sha256sum -c SHA256SUMS --ignore-missing`.
