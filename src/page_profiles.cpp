// Profiles page: onboard profile cards, control mode, backup and restore.
#include <algorithm>
#include <ctime>
#include <functional>
#include <glib/gstdio.h>
#include <memory>
#include <stdexcept>
#include "app.hpp"
#include "i18n.hpp"
#include "ui.hpp"

namespace {

using PathFn = std::function<void(const std::string&)>;

struct DialogCall {
    PathFn fn;
    bool save;
};

void json_dialog(App& app, bool save, const char* title, const std::string& name, PathFn done) {
    GtkFileDialog* d = gtk_file_dialog_new();
    gtk_file_dialog_set_title(d, title);
    g_mkdir_with_parents(backup_dir().c_str(), 0755);
    GFile* folder = g_file_new_for_path(backup_dir().c_str());
    gtk_file_dialog_set_initial_folder(d, folder);
    g_object_unref(folder);
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_add_pattern(filter, "*.json");
    gtk_file_filter_set_name(filter, "JSON");
    GListStore* filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(filters, filter);
    gtk_file_dialog_set_filters(d, G_LIST_MODEL(filters));
    g_object_unref(filter);
    g_object_unref(filters);

    auto* call = new DialogCall{std::move(done), save};
    auto ready = +[](GObject* src, GAsyncResult* res, gpointer data) {
        std::unique_ptr<DialogCall> c(static_cast<DialogCall*>(data));
        auto* dlg = GTK_FILE_DIALOG(src);
        GFile* f = c->save ? gtk_file_dialog_save_finish(dlg, res, nullptr)
                           : gtk_file_dialog_open_finish(dlg, res, nullptr);
        if (!f) return;
        if (char* path = g_file_get_path(f)) {
            c->fn(path);
            g_free(path);
        }
        g_object_unref(f);
    };
    if (save) {
        gtk_file_dialog_set_initial_name(d, name.c_str());
        gtk_file_dialog_save(d, app.window, nullptr, ready, call);
    } else {
        gtk_file_dialog_open(d, app.window, nullptr, ready, call);
    }
    g_object_unref(d);
}

bool write_text(App& app, const std::string& path, const std::string& text) {
    GError* err = nullptr;
    if (g_file_set_contents(path.c_str(), text.data(), (gssize)text.size(), &err)) return true;
    // TRANSLATORS: %s is the system error message
    app.toast(fmt(_("Could not write file: %s"), err->message));
    g_error_free(err);
    return false;
}

bool read_text(App& app, const std::string& path, std::string* out) {
    gchar* data = nullptr;
    gsize len = 0;
    GError* err = nullptr;
    if (!g_file_get_contents(path.c_str(), &data, &len, &err)) {
        // TRANSLATORS: %s is the system error message
        app.toast(fmt(_("Could not read file: %s"), err->message));
        g_error_free(err);
        return false;
    }
    out->assign(data, len);
    g_free(data);
    return true;
}

std::string stamp() {
    char buf[32];
    std::time_t t = std::time(nullptr);
    std::strftime(buf, sizeof buf, "%Y%m%d-%H%M%S", std::localtime(&t));
    return buf;
}

// Default profile name. TRANSLATORS: %d is the profile number (1 to 5)
std::string slot_text(int i) { return fmt(_("Profile %d"), i + 1); }

std::string clean_name(std::string s, int max_chars) {
    s.erase(0, s.find_first_not_of(" \t"));
    s.erase(s.find_last_not_of(" \t") + 1);
    if (g_utf8_strlen(s.c_str(), -1) <= max_chars) return s;
    return s.substr(0, g_utf8_offset_to_pointer(s.c_str(), max_chars) - s.c_str());
}

GtkWidget* panel(const char* title) {
    GtkWidget* p = ui::css(ui::vbox(12), {"ss-panel"});
    gtk_box_append(GTK_BOX(p), ui::label(title, {"ss-title"}));
    return p;
}

GtkWidget* boxed_list() {
    GtkWidget* l = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(l), GTK_SELECTION_NONE);
    return ui::css(l, {"boxed-list"});
}

GtkWidget* link_row(const char* title, const char* subtitle) {
    GtkWidget* r = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(r), title);
    adw_action_row_set_subtitle(ADW_ACTION_ROW(r), subtitle);
    gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(r), TRUE);
    adw_action_row_add_suffix(ADW_ACTION_ROW(r), ui::icon("go-next-symbolic"));
    return r;
}

struct Profiles {
    explicit Profiles(App& a) : app(a) {}
    App& app;
    GtkWidget *cards, *mode, *backup_row, *restore_row;
    bool loading = false, pending = false;

    Profile* profile(int sector) {
        auto it = app.profiles.find(sector);
        return it == app.profiles.end() ? nullptr : &it->second;
    }

    void schedule_rebuild() {
        if (pending) return;
        pending = true;
        auto self = this;
        ui::idle([self] {
            self->pending = false;
            self->rebuild();
        });
    }

    void after_change(int sector) {
        app.mark_dirty(sector);
        if (sector == app.edit) app.reload_ui();
    }

    // --- card actions
    void rename(int sector, int slot, const std::string& text) {
        Profile* p = profile(sector);
        if (!p) return;
        std::string name = text == slot_text(slot) ? "" : clean_name(text, 24);
        if (name == p->name) return;
        p->name = name;
        app.mark_dirty(sector);
    }

    void set_cycle(int slot, bool on) {
        if (on == app.dir[slot].enabled) return;
        if (!on && std::count_if(app.dir.begin(), app.dir.end(), [](const DirEntry& e) { return e.enabled; }) <= 1) {
            app.toast(_("At least one profile must stay in the cycle"));
            schedule_rebuild();
            return;
        }
        app.dir[slot].enabled = on;
        app.mark_dirty(DIR_SECTOR);
    }

    void activate(int sector, int slot) {
        if (app.dirty.count(sector)) {
            // TRANSLATORS: %d is the profile number (1 to 5)
            app.toast(fmt(_("Save Profile %d first"), slot + 1));
            return;
        }
        app.activate_profile(sector);
    }

    void factory_reset(int sector, int slot) {
        Profile* p = profile(sector);
        if (!p || app.factory.raw.empty()) {
            app.toast(_("Factory profile not loaded"));
            return;
        }
        *p = app.factory;
        p->sector = sector;
        after_change(sector);
        // TRANSLATORS: %d is the profile number (1 to 5)
        app.toast(fmt(_("Profile %d reset to factory. Save to write it"), slot + 1));
    }

    void copy_from(int sector, int slot, int from) {
        Profile* dst = profile(sector);
        Profile* src = profile(app.dir[from].sector);
        if (!dst || !src) return;
        Profile n = *src;
        n.sector = sector;
        n.name = dst->name;
        n.raw = dst->raw;
        *dst = n;
        after_change(sector);
        // TRANSLATORS: first %d is the profile copied from, second %d the profile copied into (1 to 5)
        app.toast(fmt(_("Copied Profile %d into Profile %d. Save to keep it"), from + 1, slot + 1));
    }

    void export_profile(int sector, int slot) {
        Profile* p = profile(sector);
        if (!p) return;
        std::string base = p->name.empty() ? "slot" + std::to_string(slot + 1) : p->name;
        std::replace(base.begin(), base.end(), ' ', '_');
        json_dialog(app, true, _("Export profile"), "profile-" + base + ".json", [this, sector](const std::string& path) {
            if (Profile* q = profile(sector); q && write_text(app, path, q->to_json()))
                // TRANSLATORS: %s is a file path
                app.toast(fmt(_("Exported to %s"), path.c_str()));
        });
    }

    void import_profile(int sector, int slot) {
        json_dialog(app, false, _("Import profile"), "", [this, sector, slot](const std::string& path) {
            std::string text;
            Profile* p = profile(sector);
            if (!p || !read_text(app, path, &text)) return;
            Profile n = *p;
            if (!n.from_json(text)) {
                app.toast(_("Not a valid profile file"));
                return;
            }
            n.sector = sector;
            *p = n;
            after_change(sector);
            // TRANSLATORS: %d is the profile number (1 to 5)
            app.toast(fmt(_("Imported into Profile %d. Save to write it"), slot + 1));
        });
    }

    // --- cards
    GtkWidget* menu_button(int sector, int slot) {
        GtkWidget* pop = gtk_popover_new();
        GtkWidget* box = ui::vbox(0);
        auto item = [&](const std::string& text, std::function<void()> fn) {
            GtkWidget* b = ui::button(text.c_str(), {"flat"});
            gtk_button_set_has_frame(GTK_BUTTON(b), FALSE);
            gtk_widget_set_halign(gtk_button_get_child(GTK_BUTTON(b)), GTK_ALIGN_START);
            GtkWidget* popover = pop;
            ui::on(b, "clicked", [popover, fn] {
                gtk_popover_popdown(GTK_POPOVER(popover));
                fn();
            });
            gtk_box_append(GTK_BOX(box), b);
        };
        item(_("Reset to factory"), [this, sector, slot] { factory_reset(sector, slot); });
        item(_("Export…"), [this, sector, slot] { export_profile(sector, slot); });
        item(_("Import…"), [this, sector, slot] { import_profile(sector, slot); });
        gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
        gtk_box_append(GTK_BOX(box), ui::margins(ui::label(_("Copy settings from"), {"ss-hint"}), 6, 12, 2, 12));
        for (int j = 0; j < (int)app.dir.size(); j++)
            if (j != slot) item(slot_text(j), [this, sector, slot, j] { copy_from(sector, slot, j); });
        gtk_popover_set_child(GTK_POPOVER(pop), ui::margins(box, 4));
        GtkWidget* mb = gtk_menu_button_new();
        gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(mb), "view-more-symbolic");
        gtk_menu_button_set_popover(GTK_MENU_BUTTON(mb), pop);
        gtk_widget_set_tooltip_text(mb, _("More actions"));
        return ui::css(mb, {"flat"});
    }

    GtkWidget* card(int slot) {
        int sector = app.dir[slot].sector;
        Profile* p = profile(sector);
        bool active = sector == app.active;
        GtkWidget* c = ui::css(ui::vbox(12), {"ss-panel"});
        gtk_widget_set_size_request(c, 280, -1);

        GtkWidget* top = ui::hbox(8);
        gtk_widget_set_valign(top, GTK_ALIGN_CENTER);
        ui::add(top, {ui::label(std::to_string(slot + 1), {"ss-value"}), ui::spacer()});
        auto badge = [&](const char* t, const char* c) {
            GtkWidget* b = ui::label(t, {"ss-badge", c});
            gtk_widget_set_valign(b, GTK_ALIGN_CENTER);
            ui::add(top, {b});
        };
        if (active) badge(C_("profile badge", "Active"), "accent");
        if (app.dirty.count(sector)) badge(_("Unsaved"), "warn");

        std::string shown = p && !p->name.empty() ? p->name : slot_text(slot);
        GtkWidget* name = gtk_editable_label_new(shown.c_str());
        if (GtkText* text = GTK_TEXT(gtk_editable_get_delegate(GTK_EDITABLE(name))))
            gtk_entry_buffer_set_max_length(gtk_text_get_buffer(text), 24);
        ui::notify(name, "editing", [this, name, sector, slot] {
            if (!gtk_editable_label_get_editing(GTK_EDITABLE_LABEL(name)))
                rename(sector, slot, gtk_editable_get_text(GTK_EDITABLE(name)));
        });

        GtkWidget* cycle = ui::hbox(8);
        GtkWidget* sw = gtk_switch_new();
        gtk_switch_set_active(GTK_SWITCH(sw), app.dir[slot].enabled);
        gtk_widget_set_valign(sw, GTK_ALIGN_CENTER);
        ui::add(cycle, {ui::label(_("In profile cycle")), ui::spacer(), sw});
        ui::notify(sw, "active", [this, sw, slot] { set_cycle(slot, gtk_switch_get_active(GTK_SWITCH(sw))); });

        GtkWidget* foot = ui::hbox(8);
        GtkWidget* edit = ui::button(C_("profile card", "Edit"));
        GtkWidget* act = ui::button(C_("profile card", "Activate"));
        if (!active) gtk_widget_add_css_class(act, "suggested-action");
        gtk_widget_set_sensitive(act, !active && app.connected);
        gtk_widget_set_sensitive(edit, p != nullptr);
        ui::on(edit, "clicked", [this, sector] {
            app.set_edit(sector);
            app.show_page("hits");
        });
        ui::on(act, "clicked", [this, sector, slot] { activate(sector, slot); });
        ui::add(foot, {edit, act, ui::spacer(), menu_button(sector, slot)});
        gtk_widget_set_sensitive(foot, p != nullptr);

        ui::add(c, {top, name, cycle, foot});
        return c;
    }

    void rebuild() {
        gtk_flow_box_remove_all(GTK_FLOW_BOX(cards));
        for (int i = 0; i < (int)app.dir.size(); i++) gtk_flow_box_append(GTK_FLOW_BOX(cards), card(i));
    }

    // --- control mode and backups
    void refresh_status() {
        loading = true;
        adw_combo_row_set_selected(ADW_COMBO_ROW(mode), app.info.mode == MODE_HOST ? 1 : 0);
        loading = false;
        for (GtkWidget* w : {mode, backup_row, restore_row}) gtk_widget_set_sensitive(w, app.connected);
    }

    void refresh() {
        rebuild();
        refresh_status();
    }

    void on_mode() {
        if (loading || !app.connected) return;
        int m = adw_combo_row_get_selected(ADW_COMBO_ROW(mode)) == 0 ? MODE_ONBOARD : MODE_HOST;
        app.run([m](Device& d) { d.set_onboard_mode(m); },
                [this, m] {
                    app.info.mode = m;
                    app.toast(m == MODE_ONBOARD ? _("Onboard memory mode")
                                                : _("Host mode: saved profiles are ignored until you switch back"));
                    for (auto& f : std::vector(app.on_status)) f();
                },
                [this](const std::string& e) {
                    // TRANSLATORS: %s is a technical error message
                    app.toast(fmt(_("Could not change the control mode: %s"), e.c_str()));
                    refresh_status();
                });
    }

    void backup() {
        json_dialog(app, true, _("Save memory backup"), "superstrike-backup-" + stamp() + ".json",
                    [this](const std::string& path) {
                        auto text = std::make_shared<std::string>();
                        app.run([text](Device& d) { *text = backup_json(d); },
                                [this, text, path] {
                                    // TRANSLATORS: %s is a file path
                                    if (write_text(app, path, *text)) app.toast(fmt(_("Backup saved: %s"), path.c_str()));
                                },
                                // TRANSLATORS: %s is a technical error message
                                [this](const std::string& e) { app.toast(fmt(_("Backup failed: %s"), e.c_str())); });
                    });
    }

    void restore() {
        json_dialog(app, false, _("Open memory backup"), "", [this](const std::string& path) {
            auto text = std::make_shared<std::string>();
            if (!read_text(app, path, text.get())) return;
            gchar* base = g_path_get_basename(path.c_str());
            // TRANSLATORS: %s is the file name of the backup
            std::string body = fmt(_("This overwrites the profiles in the mouse with the contents of %s. "
                                     "The current memory is saved to the backup folder first."), base);
            g_free(base);
            AdwDialog* dlg = adw_alert_dialog_new(_("Restore memory?"), body.c_str());
            adw_alert_dialog_add_response(ADW_ALERT_DIALOG(dlg), "cancel", _("Cancel"));
            adw_alert_dialog_add_response(ADW_ALERT_DIALOG(dlg), "restore", _("Restore"));
            adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(dlg), "restore", ADW_RESPONSE_DESTRUCTIVE);
            adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(dlg), "cancel");
            adw_alert_dialog_set_close_response(ADW_ALERT_DIALOG(dlg), "cancel");
            ui::on1<const char*>(dlg, "response", [this, text](const char* response) {
                if (std::string(response) == "restore") run_restore(text);
            });
            adw_dialog_present(dlg, GTK_WIDGET(app.window));
        });
    }

    void run_restore(std::shared_ptr<std::string> text) {
        struct Result {
            bool ok = false;
            std::string err;
        };
        auto res = std::make_shared<Result>();
        app.run(
            [text, res](Device& d) {
                std::string pre = backup_dir() + "/superstrike-before-restore-" + stamp() + ".json";
                g_mkdir_with_parents(backup_dir().c_str(), 0755);
                g_file_set_contents(pre.c_str(), backup_json(d).c_str(), -1, nullptr);
                res->ok = restore_json(d, *text, &res->err);
            },
            [this, res] {
                if (!res->ok) {
                    // TRANSLATORS: %s is the reason
                    app.toast(fmt(_("Restore failed: %s"), res->err.c_str()));
                    return;
                }
                app.toast(_("Memory restored"));
                app.revert();
            },
            // TRANSLATORS: %s is the reason
            [this](const std::string& e) { app.toast(fmt(_("Restore failed: %s"), e.c_str())); });
    }
};

}  // namespace

GtkWidget* page_profiles(App& app) {
    auto s = std::make_shared<Profiles>(app);
    Profiles* pp = s.get();

    pp->cards = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(pp->cards), GTK_SELECTION_NONE);
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(pp->cards), TRUE);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(pp->cards), 2);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(pp->cards), 3);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(pp->cards), 16);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(pp->cards), 16);

    GtkWidget* intro = ui::label(_("Profiles live inside the mouse and work on any computer without this app."), {"ss-hint"});
    gtk_label_set_wrap(GTK_LABEL(intro), TRUE);

    GtkWidget* mode_panel = panel(_("CONTROL MODE"));
    pp->mode = adw_combo_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(pp->mode), _("Who controls the mouse"));
    const char* modes[] = {_("Onboard memory (recommended)"), _("Host software"), nullptr};
    adw_combo_row_set_model(ADW_COMBO_ROW(pp->mode), G_LIST_MODEL(gtk_string_list_new(modes)));
    adw_combo_row_set_use_subtitle(ADW_COMBO_ROW(pp->mode), TRUE);  // full value as subtitle, never cut off
    ui::notify(pp->mode, "selected", [pp] { pp->on_mode(); });
    GtkWidget* mode_list = boxed_list();
    gtk_list_box_append(GTK_LIST_BOX(mode_list), pp->mode);
    gtk_box_append(GTK_BOX(mode_panel), mode_list);

    GtkWidget* backup_panel = panel(_("BACKUP"));
    pp->backup_row = link_row(_("Back up whole memory…"), _("All profiles, the directory and the factory profile, to a file"));
    pp->restore_row = link_row(_("Restore from backup…"), _("Writes the saved sectors back into the mouse"));
    ui::on(pp->backup_row, "activated", [pp] { pp->backup(); });
    ui::on(pp->restore_row, "activated", [pp] { pp->restore(); });
    GtkWidget* backup_list = boxed_list();
    gtk_list_box_append(GTK_LIST_BOX(backup_list), pp->backup_row);
    gtk_list_box_append(GTK_LIST_BOX(backup_list), pp->restore_row);
    // TRANSLATORS: %s is a folder path
    GtkWidget* backup_hint = ui::label(fmt(_("The app also makes a full backup automatically before the first save of each session, in %s."), backup_dir().c_str()), {"ss-hint"});
    gtk_label_set_wrap(GTK_LABEL(backup_hint), TRUE);
    ui::add(backup_panel, {backup_list, backup_hint});

    GtkWidget* col = ui::vbox(16);
    ui::add(col, {intro, pp->cards, mode_panel, backup_panel});

    app.on_reload.push_back([s] { s->refresh(); });
    app.on_dirty.push_back([s] { s->schedule_rebuild(); });
    app.on_status.push_back([s] { s->refresh_status(); });
    pp->refresh();
    return ui::page_column(col);
}
