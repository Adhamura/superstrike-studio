SuperStrike Studio on Windows 10 / 11 (64 bit)
==============================================

Most people do not need to build anything: download the ready zip
(SuperStrike-Studio-<version>-windows-x64.zip) from
https://github.com/Adhamura/SuperStrike-Studio/releases/latest
unzip it and run superstrike-studio.exe.

The steps below build the same thing yourself. You build the program once on
your PC. After that you get a portable folder and a zip file. You can copy
them to any Windows PC. No installer is needed.


1. Install MSYS2
----------------
1. Go to https://www.msys2.org and download the installer.
2. Run it. Keep the default folder (C:\msys64). Finish the installer.
3. At the end, a window may open. You can close it.


2. Open the correct shell
-------------------------
Open the Start menu and type:   MSYS2 UCRT64
Click the entry named "MSYS2 UCRT64" (dark blue/teal icon).
Do NOT use "MSYS2 MSYS", "MSYS2 MINGW64" or "MSYS2 CLANG64". The build
script stops with a message if you use the wrong one.


3. Install the build tools (once)
---------------------------------
Copy this line into the shell and press Enter. Type Y when it asks.
It downloads some hundred MB, so it takes a few minutes.

  pacman -S --needed mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,pkgconf,gtk4,libadwaita,librsvg,json-glib,gettext-runtime,gettext-tools,ntldd}

(Tip: Shift+Insert pastes into the MSYS2 window.)


4. Go to the project folder
---------------------------
The MSYS2 shell writes Windows drives as /c/, /d/ and so on.
  C:\Users\Anna\Desktop\XSuperlight\studio   becomes   /c/Users/Anna/Desktop/XSuperlight/studio
Use forward slashes. Put the path in quotes if it has spaces. Example:

  cd "/c/Users/Anna/Desktop/XSuperlight/studio"

Check that you are in the right place. This command must list CMakeLists.txt:

  ls


5. Build
--------
  tools/windows/build-windows.sh

The first build takes a few minutes. The script prints each step. When it
ends it shows the result:

  dist/SuperStrike-Studio/superstrike-studio.exe       the program
  dist/SuperStrike-Studio-windows-x64.zip              the same folder, zipped

You can run the script again at any time. It rebuilds the dist folder.


6. Start the program
--------------------
Plug in the mouse or its LIGHTSPEED receiver first.
Then run (in the shell):

  dist/SuperStrike-Studio/superstrike-studio.exe

Or open the folder dist\SuperStrike-Studio in Windows Explorer and
double-click superstrike-studio.exe. You can move or copy the whole folder
(keep all files together) or unzip the zip on another PC. That PC needs no
MSYS2.


Notes
-----
* Close Logitech G HUB first (also in the tray, near the clock). G HUB can
  keep the receiver busy, and then SuperStrike Studio shows "Mouse not found".
  Use "Retry" after you close it.

* The mouse or the receiver must be plugged in and awake. Move the mouse
  once. The program searches again every 5 seconds.

* Windows Defender SmartScreen may warn: "Windows protected your PC". The
  exe is not signed with a paid certificate. Click "More info", then
  "Run anyway". You built the file yourself from the sources.

* Settings and backups are stored in your user profile:
    Settings:  %LOCALAPPDATA%\superstrike\studio.ini
    Backups:   %LOCALAPPDATA%\superstrike\backups
  Paste that text into the Explorer address bar to open the folder. The
  program makes an automatic backup of the mouse memory before your first
  save in each session.

* The live click animation (the mouse picture that lights up when you press
  a button) only reacts while the pointer is over the program window. Windows
  does not allow a program to read the mouse buttons in the background the
  way the Linux version does. Everything else works the same.

* No mouse at hand? Try every page with a simulated mouse:
    dist/SuperStrike-Studio/superstrike-studio.exe --demo

* The language follows Windows. You can pick another one on the "Device" page.

* If the window stays empty or the program stops at start-up, graphics drivers
  may be the cause. Start it from the shell with the software renderer:
    GSK_RENDERER=cairo dist/SuperStrike-Studio/superstrike-studio.exe

* To check only the connection (no changes are made to the mouse), run this in
  the shell and read the last line. It must say PASS:
    dist/SuperStrike-Studio/sscheck.exe
