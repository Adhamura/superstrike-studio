// gettext set-up: locale, message catalog location and the language list for the picker.
#include "i18n.hpp"
#include <glib.h>
#include <locale.h>
#include <cstdlib>
#include <cstring>
#include <optional>
#include "widgets.hpp"

const Language LANGUAGES[] = {
    {"en", "English"},          {"zh_CN", "简体中文"},      {"zh_TW", "繁體中文（台灣）"},
    {"ja", "日本語"},           {"ko", "한국어"},           {"ru", "Русский"},
    {"uk", "Українська"},       {"es", "Español"},          {"pt_BR", "Português (Brasil)"},
    {"fr", "Français"},         {"de", "Deutsch"},          {"it", "Italiano"},
    {"pl", "Polski"},           {"tr", "Türkçe"},           {"ar", "العربية"},
    {"hi", "हिन्दी"},             {"bn", "বাংলা"},            {"ur", "اردو"},
    {"id", "Bahasa Indonesia"}, {"vi", "Tiếng Việt"},       {"th", "ไทย"}};
const int LANGUAGE_COUNT = (int)(sizeof LANGUAGES / sizeof LANGUAGES[0]);

static std::optional<std::string> g_orig_language;  // LANGUAGE before we changed it (nullopt = unset)
static bool g_orig_saved = false;

// Windows: libintl wants the catalog folder in the ANSI code page, so hand it a form it can open.
static std::string catalog_dir() {
    std::string dir = asset_path("locale");
#ifdef _WIN32
    if (gchar* local = g_win32_locale_filename_from_utf8(dir.c_str())) {
        dir = local;
        g_free(local);
    }
#endif
    return dir;
}

static void set_language_env(const std::string& lang) {
    if (!lang.empty() && lang != "system") g_setenv("LANGUAGE", lang.c_str(), TRUE);
    else if (g_orig_language) g_setenv("LANGUAGE", g_orig_language->c_str(), TRUE);
    else g_unsetenv("LANGUAGE");
}

void i18n_init(const std::string& lang) {
    if (!g_orig_saved) {
        g_orig_saved = true;
        if (const char* e = g_getenv("LANGUAGE")) g_orig_language = e;
    }
    set_language_env(lang);
    setlocale(LC_ALL, "");
#if defined(__linux__) || defined(__APPLE__)
    // GNU gettext ignores LANGUAGE in the "C" locale, so pick a real UTF-8 locale when needed.
    const char* msg = setlocale(LC_MESSAGES, nullptr);
    if (!msg || !strcmp(msg, "C") || !strcmp(msg, "POSIX")) {
        if (!setlocale(LC_ALL, "en_US.UTF-8")) setlocale(LC_ALL, "C.UTF-8");
    }
#endif
    gtk_disable_setlocale();  // GTK must not reset the locale chosen above
    bindtextdomain("superstrike-studio", catalog_dir().c_str());
    bind_textdomain_codeset("superstrike-studio", "UTF-8");
    textdomain("superstrike-studio");
}

bool i18n_rtl() {
    // GTK picks the direction from its own catalog, which does not work with the locale trick above.
    const char* const* langs = g_get_language_names();
    for (const char* code : {"ar", "ur", "fa", "he"})
        if (langs && langs[0] && !strncmp(langs[0], code, strlen(code))) return true;
    return false;
}

void i18n_set(const std::string& lang) {
    set_language_env(lang);
    // Calling textdomain() again with the same domain makes GNU gettext drop its cached catalogs.
    textdomain("superstrike-studio");
}
