// Windows-only helpers (kept apart from GTK code so windows.h macros stay out of it).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "platform.hpp"

std::string exe_dir() {
    wchar_t path[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, path, (DWORD)(sizeof path / sizeof path[0]));
    if (n == 0 || n >= sizeof path / sizeof path[0]) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, path, (int)n, nullptr, 0, nullptr, nullptr);
    std::string s((size_t)len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, path, (int)n, s.data(), len, nullptr, nullptr);
    for (char& c : s)
        if (c == '\\') c = '/';
    return s.substr(0, s.find_last_of('/'));
}
