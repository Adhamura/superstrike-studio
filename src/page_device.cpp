// Device page: identity, connection, firmware, onboard memory, mouse colour and HID++ features.
#include <cstdio>
#include <memory>
#include <vector>
#include "app.hpp"
#include "i18n.hpp"
#include "ui.hpp"
#include "update.hpp"

namespace {

GtkWidget* boxed_list() {
    GtkWidget* l = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(l), GTK_SELECTION_NONE);
    return ui::css(l, {"boxed-list"});
}

GtkWidget* panel(const char* title, GtkWidget* child) {
    GtkWidget* p = ui::css(ui::vbox(12), {"ss-panel"});
    ui::add(p, {ui::label(title, {"ss-title"}), child});
    return p;
}

std::string hex(const char* fmt, unsigned v) {
    char buf[16];
    std::snprintf(buf, sizeof buf, fmt, v);
    return buf;
}

void show_about(App& app) {
    AdwDialog* d = adw_about_dialog_new();
    AdwAboutDialog* a = ADW_ABOUT_DIALOG(d);
    adw_about_dialog_set_application_name(a, "SuperStrike Studio");
    adw_about_dialog_set_application_icon(a, "io.github.superstrike.Studio");
    adw_about_dialog_set_version(a, SS_VERSION);
    adw_about_dialog_set_developer_name(a, "Adhamura");
    adw_about_dialog_set_website(a, "https://github.com/Adhamura/SuperStrike-Studio");
    adw_about_dialog_set_issue_url(a, "https://github.com/Adhamura/SuperStrike-Studio/issues/new/choose");
    adw_about_dialog_set_license_type(a, GTK_LICENSE_MIT_X11);
    adw_about_dialog_set_copyright(a, "© 2026 Adhamura");
    // TRANSLATORS: shown in the About dialog. Logitech is a trademark, keep the name as it is
    adw_about_dialog_set_comments(a, _("Not affiliated with or endorsed by Logitech."));
    adw_dialog_present(d, GTK_WIDGET(app.window));
}

struct Device_page {
    explicit Device_page(App& a) : app(a) {}
    App& app;
    GtkWidget *name, *connection, *battery, *firmware, *memory, *color, *features, *language = nullptr;
    std::vector<GtkWidget*> feature_rows;
    bool loading = false;

    GtkWidget* info_row(const char* title) {
        GtkWidget* r = adw_action_row_new();
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(r), title);
        adw_action_row_set_subtitle(ADW_ACTION_ROW(r), "—");
        adw_action_row_set_subtitle_lines(ADW_ACTION_ROW(r), 0);
        adw_action_row_set_subtitle_selectable(ADW_ACTION_ROW(r), TRUE);
        gtk_widget_add_css_class(r, "property");
        return r;
    }

    static void set_text(GtkWidget* row, const std::string& text) {
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), text.empty() ? "—" : text.c_str());
    }

    void refresh_status() {
        const DeviceInfo& i = app.info;
        if (!app.connected) {
            for (GtkWidget* r : {name, connection, battery, firmware, memory}) set_text(r, "");
            return;
        }
        std::string state = battery_state_text(i.battery_state);
        set_text(battery, i.battery < 0 ? "" : state.empty()
                                                   // TRANSLATORS: battery row. %d is the charge in percent
                                                   ? fmt(_("%d %%"), i.battery)
                                                   // TRANSLATORS: battery row. %d is the charge in percent, %s is the state, for example "charging"
                                                   : fmt(_("%d %% · %s"), i.battery, state.c_str()));
    }

    void refresh() {
        const DeviceInfo& i = app.info;
        if (app.connected) {
            set_text(name, i.name);
            set_text(connection, i.path + " · " +
                                     (i.index == 0xFF ? std::string(_("USB cable"))
                                                      // TRANSLATORS: %d is the receiver slot number (1 to 6)
                                                      : fmt(_("LIGHTSPEED receiver slot %d"), i.index)));
            std::string fw;
            for (auto& line : i.firmware) fw += (fw.empty() ? "" : "\n") + line;
            set_text(firmware, fw);
            const OnboardInfo& o = i.onboard;
            // TRANSLATORS: %d is the number of profile slots in the mouse
            std::string n_profiles = fmt(ngettext("%d profile", "%d profiles", (unsigned long)o.profiles), (int)o.profiles);
            // TRANSLATORS: %d is the version number of the memory layout
            std::string n_format = fmt(_("format %d"), (int)o.format);
            // TRANSLATORS: first %d is the number of memory sectors, second %d is the size of one sector
            std::string n_sectors = fmt(ngettext("%d sector × %d bytes", "%d sectors × %d bytes", (unsigned long)o.sectors),
                                        (int)o.sectors, (int)o.sector_size);
            set_text(memory, n_profiles + " · " + n_format + " · " + n_sectors);
        }
        refresh_status();

        loading = true;
        guint sel = 0;
        for (int k = 0; k < LANGUAGE_COUNT; k++)
            if (app.cfg.lang == LANGUAGES[k].code) sel = (guint)k + 1;
        adw_combo_row_set_selected(ADW_COMBO_ROW(language), sel);
        GdkRGBA rgba;
        if (gdk_rgba_parse(&rgba, app.cfg.shell.c_str()))
            gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(color), &rgba);
        loading = false;

        for (GtkWidget* r : feature_rows) adw_expander_row_remove(ADW_EXPANDER_ROW(features), r);
        feature_rows.clear();
        for (const FeatureEntry& f : i.features) {
            GtkWidget* r = adw_action_row_new();
            adw_preferences_row_set_title(ADW_PREFERENCES_ROW(r), ("0x" + hex("%04X", f.id)).c_str());
            // TRANSLATORS: HID++ feature details. %d: table index, %d: feature version, %s: flags as hex digits
            adw_action_row_set_subtitle(ADW_ACTION_ROW(r), fmt(_("index %d · v%d · flags 0x%s"), (int)f.index, (int)f.version,
                                                               hex("%02X", f.flags).c_str()).c_str());
            adw_expander_row_add_row(ADW_EXPANDER_ROW(features), r);
            feature_rows.push_back(r);
        }
        adw_expander_row_set_subtitle(ADW_EXPANDER_ROW(features),
                                      i.features.empty() ? ""
                                      // TRANSLATORS: %d is the number of HID++ features the mouse reports
                                      : fmt(ngettext("%d feature", "%d features", (unsigned long)i.features.size()),
                                            (int)i.features.size()).c_str());
    }

    // Language picker: "System default" first, then every language by its own name (never translated).
    GtkWidget* language_row() {
        language = adw_combo_row_new();
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(language), _("Language"));
        GtkStringList* list = gtk_string_list_new(nullptr);
        gtk_string_list_append(list, _("System default"));
        for (int k = 0; k < LANGUAGE_COUNT; k++) gtk_string_list_append(list, LANGUAGES[k].native);
        adw_combo_row_set_model(ADW_COMBO_ROW(language), G_LIST_MODEL(list));
        g_object_unref(list);
        ui::notify(language, "selected", [this] { on_language(); });
        return language;
    }

    void on_language() {
        if (loading) return;
        guint i = adw_combo_row_get_selected(ADW_COMBO_ROW(language));
        std::string code = i == 0 || i > (guint)LANGUAGE_COUNT ? "system" : LANGUAGES[i - 1].code;
        if (code == app.cfg.lang) return;
        app.set_language(code);
    }

    void on_color() {
        if (loading) return;
        const GdkRGBA* c = gtk_color_dialog_button_get_rgba(GTK_COLOR_DIALOG_BUTTON(color));
        std::string value = "#" + hex("%02x", (unsigned)(c->red * 255 + 0.5f)) + hex("%02x", (unsigned)(c->green * 255 + 0.5f)) +
                            hex("%02x", (unsigned)(c->blue * 255 + 0.5f));
        if (value == app.cfg.shell) return;
        app.cfg.shell = value;
        app.cfg.save();
        app.reload_ui();
    }
};

}  // namespace

GtkWidget* page_device(App& app) {
    auto s = std::make_shared<Device_page>(app);
    Device_page* d = s.get();

    d->name = d->info_row(C_("device info", "Name"));
    d->connection = d->info_row(_("Connection"));
    d->battery = d->info_row(_("Battery"));
    d->firmware = d->info_row(_("Firmware"));
    d->memory = d->info_row(_("Onboard memory"));
    GtkWidget* info = boxed_list();
    for (GtkWidget* r : {d->name, d->connection, d->battery, d->firmware, d->memory})
        gtk_list_box_append(GTK_LIST_BOX(info), r);

    GtkWidget* color_row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(color_row), _("Mouse colour in the pictures"));
    d->color = gtk_color_dialog_button_new(gtk_color_dialog_new());
    gtk_widget_set_valign(d->color, GTK_ALIGN_CENTER);
    adw_action_row_add_suffix(ADW_ACTION_ROW(color_row), d->color);
    ui::notify(d->color, "rgba", [d] { d->on_color(); });
    GtkWidget* look_list = boxed_list();
    gtk_list_box_append(GTK_LIST_BOX(look_list), color_row);
    gtk_list_box_append(GTK_LIST_BOX(look_list), d->language_row());

    d->features = adw_expander_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(d->features), _("Feature table"));
    GtkWidget* feature_list = boxed_list();
    gtk_list_box_append(GTK_LIST_BOX(feature_list), d->features);

    GtkWidget* about = adw_button_row_new();
    // TRANSLATORS: %s is the app name, SuperStrike Studio
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(about), fmt(_("About %s"), "SuperStrike Studio").c_str());
    adw_button_row_set_start_icon_name(ADW_BUTTON_ROW(about), "help-about-symbolic");
    ui::on(about, "activated", [&app] { show_about(app); });
    GtkWidget* auto_row = adw_switch_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(auto_row), _("Check for updates automatically"));
    adw_action_row_set_subtitle(ADW_ACTION_ROW(auto_row), _("At most once a day, one request to github.com"));
    adw_switch_row_set_active(ADW_SWITCH_ROW(auto_row), app.cfg.update_auto);
    ui::notify(auto_row, "active", [&app, auto_row] {
        app.cfg.update_auto = adw_switch_row_get_active(ADW_SWITCH_ROW(auto_row));
        app.cfg.save();
    });
    GtkWidget* check = adw_button_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(check), _("Check for updates"));
    adw_button_row_set_start_icon_name(ADW_BUTTON_ROW(check), "view-refresh-symbolic");
    ui::on(check, "activated", [&app] { update_check(app, true); });
    GtkWidget* about_list = boxed_list();
    for (GtkWidget* r : {auto_row, check, about}) gtk_list_box_append(GTK_LIST_BOX(about_list), r);

    GtkWidget* col = ui::vbox(16);
    ui::add(col, {info, panel(_("LOOK"), look_list), panel(_("HID++ FEATURES"), feature_list), about_list});

    app.on_reload.push_back([s] { s->refresh(); });
    app.on_status.push_back([s] { s->refresh_status(); });
    d->refresh();
    return ui::page_column(col);
}
