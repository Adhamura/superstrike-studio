// Internationalisation (GNU gettext). Every user-visible string goes through one of these macros.
//   _("text")            translate now              N_("text")        mark only (static tables), shown with _()
//   C_("ctx", "text")    translate with context     NC_("ctx","text") mark only, shown with C_("ctx", text)
//   ngettext(one, many, n) for plurals;            fmt(_("... %d ..."), n) for sentences with values.
// xgettext keywords: see po/update-pot.sh.
#pragma once
#include <libintl.h>
#include <cstdio>
#include <string>

#define _(s) gettext(s)
#define N_(s) (s)
#define C_(ctx, s) pgettext_(ctx, s)
#define NC_(ctx, s) (s)

// Message with context: the catalog key is "ctx" EOT "text". Returns text itself when untranslated.
inline const char* pgettext_(const char* ctx, const char* text) {
    std::string key = std::string(ctx) + '\004' + text;
    const char* t = gettext(key.c_str());
    return t == key.c_str() ? text : t;
}

// printf into a std::string (translated format strings may use positional %1$s arguments).
template <class... A> std::string fmt(const char* f, A... a) {
    int n = std::snprintf(nullptr, 0, f, a...);
    if (n <= 0) return std::string(f);
    std::string s((size_t)n, '\0');
    std::snprintf(s.data(), (size_t)n + 1, f, a...);
    return s;
}

struct Language { const char* code; const char* native; };
extern const Language LANGUAGES[];  // native names, never translated; first entry is English
extern const int LANGUAGE_COUNT;

// lang: "" / "system" = follow the environment, else a code from LANGUAGES. Call before GTK starts.
void i18n_init(const std::string& lang);
bool i18n_rtl();  // first UI language is right-to-left (ar, ur, fa, he)
void i18n_set(const std::string& lang);  // switch the UI language at run time ("system" = desktop)
