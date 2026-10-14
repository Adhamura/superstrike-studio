#include "update.hpp"

#include <cstdio>
#include <ctime>
#include <memory>
#include <thread>
#include <json-glib/json-glib.h>

#include "app.hpp"
#include "http.hpp"
#include "i18n.hpp"
#include "ui.hpp"

bool version_newer(const std::string& a, const std::string& b) {
    int x[3] = {0, 0, 0}, y[3] = {0, 0, 0};
    std::sscanf(a.c_str(), "%d.%d.%d", &x[0], &x[1], &x[2]);
    std::sscanf(b.c_str(), "%d.%d.%d", &y[0], &y[1], &y[2]);
    for (int i = 0; i < 3; i++)
        if (x[i] != y[i]) return x[i] > y[i];
    return false;
}

// Release notes for the dialog: the changelog part only, light Markdown removed.
static std::string plain_notes(std::string md) {
    size_t cut = md.find("### Install");
    if (cut != std::string::npos) md.resize(cut);
    std::string out;
    for (size_t i = 0; i < md.size(); i++) {
        if (md.compare(i, 2, "**") == 0) { i++; continue; }
        if (md[i] == '\r' || md[i] == '`') continue;
        bool line_start = i == 0 || md[i - 1] == '\n';
        if (line_start && md.compare(i, 2, "- ") == 0) { out += "• "; i++; continue; }
        if (line_start && md[i] == '#') {  // "### Fixes" -> "Fixes"
            while (i < md.size() && (md[i] == '#' || md[i] == ' ')) i++;
            i--;
            continue;
        }
        out += md[i];
    }
    size_t a = out.find_first_not_of(" \n"), b = out.find_last_not_of(" \n");
    return a == std::string::npos ? "" : out.substr(a, b - a + 1);
}

int fetch_latest_release(ReleaseInfo& out, std::string& error) {
    HttpResult r;
    gchar* test = nullptr;
    if (const char* f = g_getenv("SS_UPDATE_TEST_JSON"); f && g_file_get_contents(f, &test, nullptr, nullptr)) {
        r.status = 200, r.body = test;  // debug and screenshots: a release from a file instead of GitHub
        g_free(test);
    } else {
        r = https_get("api.github.com", std::string("/repos/") + UPDATE_REPO + "/releases/latest",
                      std::string("SuperStrike-Studio/") + SS_VERSION);
    }
    if (!r.error.empty()) { error = r.error; return 1; }
    if (r.status != 200) {
        error = "GitHub answered HTTP " + std::to_string(r.status);
        if (r.status == 404) error += " (no release yet)";
        else if (r.status == 403 || r.status == 429) error += " (rate limit, try again later)";
        if (g_getenv("SS_HTTP_DEBUG")) error += "\n" + r.body.substr(0, 400);
        return 2;
    }
    JsonParser* p = json_parser_new();
    int rc = 2;
    if (json_parser_load_from_data(p, r.body.data(), (gssize)r.body.size(), nullptr) &&
        JSON_NODE_HOLDS_OBJECT(json_parser_get_root(p))) {
        JsonObject* o = json_node_get_object(json_parser_get_root(p));
        auto str = [o](const char* k) {
            const char* v = json_object_has_member(o, k) ? json_object_get_string_member_with_default(o, k, "") : "";
            return std::string(v ? v : "");
        };
        out.version = str("tag_name");
        if (!out.version.empty() && (out.version[0] == 'v' || out.version[0] == 'V')) out.version.erase(0, 1);
        out.url = str("html_url");
        out.notes = plain_notes(str("body"));
        if (!out.version.empty() && out.url.rfind("https://github.com/", 0) == 0) rc = 0;
    }
    g_object_unref(p);
    if (rc) error = "unexpected answer from GitHub";
    return rc;
}

static void show_update(App& app, const ReleaseInfo& rel) {
    // TRANSLATORS: %s is a version number, for example 1.2.0
    AdwDialog* d = adw_alert_dialog_new(fmt(_("Version %s is available"), rel.version.c_str()).c_str(),
                                        // TRANSLATORS: %s is the version number the user has now
                                        fmt(_("You have version %s. The new version is on GitHub."), SS_VERSION).c_str());
    if (!rel.notes.empty()) {
        GtkWidget* notes = gtk_label_new(rel.notes.c_str());
        gtk_label_set_wrap(GTK_LABEL(notes), TRUE);
        gtk_label_set_xalign(GTK_LABEL(notes), 0);
        GtkWidget* scroll = gtk_scrolled_window_new();
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
        gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroll), 260);
        gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroll), TRUE);
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), notes);
        adw_alert_dialog_set_extra_child(ADW_ALERT_DIALOG(d), scroll);
    }
    adw_alert_dialog_add_responses(ADW_ALERT_DIALOG(d), "skip", _("Skip this version"), "later", _("Later"), "open",
                                   _("Open download page"), nullptr);
    adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(d), "open", ADW_RESPONSE_SUGGESTED);
    adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(d), "open");
    adw_alert_dialog_set_close_response(ADW_ALERT_DIALOG(d), "later");
    std::string version = rel.version, url = rel.url;
    ui::on1<const char*>(d, "response", [&app, version, url](const char* r) {
        if (!g_strcmp0(r, "skip")) {
            app.cfg.update_skip = version;
            app.cfg.save();
        } else if (!g_strcmp0(r, "open")) {
            GtkUriLauncher* l = gtk_uri_launcher_new(url.c_str());
            gtk_uri_launcher_launch(l, app.window, nullptr, nullptr, nullptr);
            g_object_unref(l);
        }
    });
    adw_dialog_present(d, GTK_WIDGET(app.window));
}

void update_check(App& app, bool manual) {
    long long now = (long long)time(nullptr);
    if (!manual) {
        if (!app.cfg.update_auto || now - app.cfg.update_last < 24 * 3600) return;
        app.cfg.update_last = now;
        app.cfg.save();
    }
    struct Result { ReleaseInfo rel; std::string error; int rc = 1; };
    auto res = std::make_shared<Result>();
    std::thread([&app, res, manual] {
        res->rc = fetch_latest_release(res->rel, res->error);
        ui::idle([&app, res, manual] {
            if (res->rc != 0) {
                // TRANSLATORS: %s is a technical error message
                if (manual) app.toast(fmt(_("Update check failed: %s"), res->error.c_str()));
                return;
            }
            bool newer = version_newer(res->rel.version, SS_VERSION);
            if (newer && (manual || res->rel.version != app.cfg.update_skip)) return show_update(app, res->rel);
            // TRANSLATORS: %s is the version number the user has now
            if (manual) app.toast(fmt(_("You have the latest version (%s)"), SS_VERSION));
        });
    }).detach();
}
