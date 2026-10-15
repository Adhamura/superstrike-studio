@echo off
rem Build SuperStrike Studio on Windows 10 / 11 (64-bit): double-click this file.
rem
rem Uses MSYS2 (https://www.msys2.org). If it is missing, it is installed with winget.
rem The first run also installs the build packages (some hundred MB) and takes a few minutes.
rem Result: dist\SuperStrike-Studio-windows-x64.exe  (one file, copy it anywhere)
rem
rem MSYS2 in another folder:  set MSYS2_ROOT=D:\msys64  before running this.
rem build-windows.bat --no-pause  does not wait for a key at the end (scripts, CI).
setlocal
cd /d "%~dp0"

if not defined MSYS2_ROOT set "MSYS2_ROOT=C:\msys64"
set "BASH=%MSYS2_ROOT%\usr\bin\bash.exe"

if not exist "%BASH%" (
    echo MSYS2 was not found in %MSYS2_ROOT%. Installing it with winget...
    winget install -e --id MSYS2.MSYS2 --accept-source-agreements --accept-package-agreements
    if not exist "%BASH%" (
        echo.
        echo ERROR: MSYS2 is still missing. Install it from https://www.msys2.org
        echo into C:\msys64, or set MSYS2_ROOT to its folder, then run this again.
        goto :fail
    )
)

set MSYSTEM=UCRT64
set CHERE_INVOKING=1
set "PKGS=mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-gtk4 mingw-w64-ucrt-x86_64-libadwaita mingw-w64-ucrt-x86_64-librsvg mingw-w64-ucrt-x86_64-json-glib mingw-w64-ucrt-x86_64-gettext-runtime mingw-w64-ucrt-x86_64-gettext-tools mingw-w64-ucrt-x86_64-ntldd"

echo ==^> Build packages
"%BASH%" -lc "missing=$(pacman -T $PKGS); if [ -n \"$missing\" ]; then echo Installing: $missing; pacman -Sy --needed --noconfirm $missing; else echo all installed; fi"
if errorlevel 1 (
    echo ERROR: installing the MSYS2 packages failed.
    goto :fail
)

"%BASH%" -lc "tools/windows/build-windows.sh"
if errorlevel 1 (
    echo ERROR: the build failed, see the messages above.
    goto :fail
)

echo.
echo Done: %CD%\dist\SuperStrike-Studio-windows-x64.exe
if /i not "%~1"=="--no-pause" pause
exit /b 0

:fail
if /i not "%~1"=="--no-pause" pause
exit /b 1
