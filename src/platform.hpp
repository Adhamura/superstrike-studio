// Small OS helpers. Windows only: implemented in platform_win.cpp.
#pragma once
#include <string>

std::string exe_dir();  // folder of the running .exe, UTF-8 with forward slashes
