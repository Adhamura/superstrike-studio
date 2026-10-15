// macOS helpers: the app's own folder and, inside "SuperStrike Studio.app", where GTK finds its
// bundled files (tools/macos/build-macos.sh lays them out under Contents/Resources).
#include <CoreFoundation/CoreFoundation.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <mach-o/dyld.h>

#include <climits>
#include <cstdlib>
#include <string>

#include "platform.hpp"

std::string exe_dir() {
    uint32_t n = 0;
    _NSGetExecutablePath(nullptr, &n);
    std::string p(n, '\0');
    if (_NSGetExecutablePath(p.data(), &n) != 0) return {};
    char real[PATH_MAX];
    if (!realpath(p.c_str(), real)) return {};
    std::string s = real;
    return s.substr(0, s.find_last_of('/'));
}

// The user's languages from System Settings as a gettext LANGUAGE list ("pt_BR:de"), because an
// app started from Finder gets no LANG.
static std::string preferred_languages() {
    std::string out;
    CFArrayRef langs = CFLocaleCopyPreferredLanguages();
    if (!langs) return out;
    for (CFIndex i = 0; i < CFArrayGetCount(langs); i++) {
        char buf[64];
        if (!CFStringGetCString((CFStringRef)CFArrayGetValueAtIndex(langs, i), buf, sizeof buf, kCFStringEncodingUTF8))
            continue;
        std::string tag = buf, lang = tag.substr(0, tag.find('-')), code;
        if (lang == "zh") code = tag.find("Hant") != std::string::npos ? "zh_TW" : "zh_CN";
        else if (size_t dash = tag.find_last_of('-'); dash != std::string::npos && tag.size() - dash == 3)
            code = lang + "_" + tag.substr(dash + 1);  // pt-BR -> pt_BR (a script subtag is skipped)
        else code = lang;
        if (!out.empty()) out += ":";
        out += code;
        if (code != lang) out += ":" + lang;
    }
    CFRelease(langs);
    return out;
}

void platform_init() {
    if (!g_getenv("LANGUAGE") && !g_getenv("LANG") && !g_getenv("LC_ALL") && !g_getenv("LC_MESSAGES"))
        if (std::string l = preferred_languages(); !l.empty()) g_setenv("LANGUAGE", l.c_str(), TRUE);

    std::string res = exe_dir() + "/../Resources";
    if (char real[PATH_MAX]; realpath(res.c_str(), real)) res = real;
    if (!g_file_test((res + "/share/glib-2.0/schemas/gschemas.compiled").c_str(), G_FILE_TEST_EXISTS))
        return;  // not inside the .app (a build from source uses the Homebrew files)

    g_setenv("GSETTINGS_SCHEMA_DIR", (res + "/share/glib-2.0/schemas").c_str(), TRUE);
    g_setenv("XDG_DATA_DIRS", (res + "/share").c_str(), TRUE);
    g_setenv("GTK_DATA_PREFIX", res.c_str(), TRUE);
    g_setenv("GTK_EXE_PREFIX", res.c_str(), TRUE);
    if (g_file_test((res + "/etc/fonts/fonts.conf").c_str(), G_FILE_TEST_EXISTS)) {
        g_setenv("FONTCONFIG_PATH", (res + "/etc/fonts").c_str(), TRUE);
        g_setenv("FONTCONFIG_FILE", (res + "/etc/fonts/fonts.conf").c_str(), TRUE);
    }

    // gdk-pixbuf wants absolute loader paths: fill in where the .app is now.
    std::string loaders = res + "/lib/gdk-pixbuf-2.0/2.10.0";
    gchar* tmpl = nullptr;
    if (g_file_get_contents((loaders + "/loaders.cache.in").c_str(), &tmpl, nullptr, nullptr)) {
        std::string cache = tmpl;
        g_free(tmpl);
        for (size_t at; (at = cache.find("@LOADERS@")) != std::string::npos;)
            cache.replace(at, 9, loaders + "/loaders");
        std::string dir = std::string(g_get_user_cache_dir()) + "/superstrike-studio";
        g_mkdir_with_parents(dir.c_str(), 0700);
        std::string file = dir + "/loaders.cache";
        if (g_file_set_contents(file.c_str(), cache.c_str(), (gssize)cache.size(), nullptr))
            g_setenv("GDK_PIXBUF_MODULE_FILE", file.c_str(), TRUE);
    }
}
