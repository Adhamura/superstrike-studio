// Small OS helpers: platform_win.cpp (Windows) and platform_mac.cpp (macOS).
#pragma once
#include <string>

std::string exe_dir();  // folder of the running program, UTF-8 with forward slashes
#ifdef __APPLE__
void platform_init();   // before GTK starts: language and the files bundled in the .app
#endif
