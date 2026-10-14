// Main window: sidebar (device card, navigation, edited profile), header (title, save), pages.
#include "app_impl.hpp"
#include "i18n.hpp"
#include "ui.hpp"
#include "widgets.hpp"

static const struct { const char* name; const char* title; const char* icon; GtkWidget* (*build)(App&); } PAGES[] = {
    {"hits", NC_("page", "HITS tuning"), "ss-hits-symbolic", page_hits},
    {"buttons", NC_("page", "Buttons"), "ss-buttons-symbolic", page_buttons},
    {"sensor", NC_("page", "Sensor"), "ss-sensor-symbolic", page_sensor},
    {"profiles", NC_("page", "Profiles"), "ss-profiles-symbolic", page_profiles},
    {"device", NC_("page", "Device"), "ss-device-symbolic", page_device},
};

void App::show_page(const char* name) {
    for (size_t i = 0; i < p->pages.size(); i++)
        if (p->pages[i].first == name)
            gtk_list_box_select_row(GTK_LIST_BOX(p->nav), gtk_list_box_get_row_at_index(GTK_LIST_BOX(p->nav), (int)i));
}

static std::string profile_label(App& app, int sector) {
    int n = app.slot_of(sector) + 1;
    auto it = app.profiles.find(sector);
    // TRANSLATORS: %d is the profile number (1 to 5)
    if (it == app.profiles.end() || it->second.name.empty()) return fmt(_("Profile %d"), n);
    // TRANSLATORS: %d is the profile number (1 to 5), %s is the name the user gave the profile
    return fmt(_("Profile %d · %s"), n, it->second.name.c_str());
}

static void update_title(App& app) {
    auto& p = *app.p;
    GtkListBoxRow* row = gtk_list_box_get_selected_row(GTK_LIST_BOX(p.nav));
    int i = row ? gtk_list_box_row_get_index(row) : 0;
    adw_window_title_set_title(p.title, C_("page", p.pages[i].second.c_str()));
    std::string sub = !app.connected ? _("Not connected")
                      : app.edit < 0 ? ""
                      // TRANSLATORS: window subtitle. %s is the profile, for example "Profile 2" or "Profile 2 · Fast"
                      : app.edit == app.active ? fmt(_("%s · Active"), profile_label(app, app.edit).c_str())
                                               // TRANSLATORS: window subtitle. %s is the profile, for example "Profile 2"
                                               : fmt(_("%s · not active"), profile_label(app, app.edit).c_str());
    adw_window_title_set_subtitle(p.title, sub.c_str());
}

static void update_status(App& app) {
    auto& p = *app.p;
    gtk_widget_remove_css_class(p.status_dot, "ok");
    if (app.connected) gtk_widget_add_css_class(p.status_dot, "ok");
    bool charging = app.connected && app.info.battery_state.find("charg") == 0;
    bool level = app.connected && app.info.battery >= 0;
    std::string text = !app.connected ? _("Searching for the mouse…")
                       : level ? (charging
                                      // TRANSLATORS: status line, %d is the battery level in percent
                                      ? fmt(_("Connected · %d %% · charging"), app.info.battery)
                                      // TRANSLATORS: status line, %d is the battery level in percent
                                      : fmt(_("Connected · %d %%"), app.info.battery))
                               : charging ? _("Connected · charging") : _("Connected");
    gtk_label_set_text(GTK_LABEL(p.status_text), text.c_str());
    gtk_label_set_text(GTK_LABEL(p.device_name), app.info.name.c_str());
    adw_banner_set_revealed(p.banner, !app.connected);
    gtk_widget_set_sensitive(p.content, app.connected);
    gtk_widget_set_sensitive(p.profile_dd, app.connected);
    update_title(app);
}

static void update_dirty(App& app) {
    auto& p = *app.p;
    size_t n = app.dirty.size();
    gtk_widget_set_sensitive(p.save_btn, app.connected && n);
    gtk_widget_set_sensitive(p.revert_btn, app.connected && n);
    gtk_revealer_set_reveal_child(GTK_REVEALER(p.savebar), app.connected && n);
    // TRANSLATORS: %d is the number of edits that are not yet written to the mouse
    std::string t = fmt(ngettext("%d unsaved change", "%d unsaved changes", (unsigned long)n), (int)n);
    gtk_label_set_text(GTK_LABEL(p.savebar_text), t.c_str());
}

static GtkWidget* build_savebar(App& app) {
    auto& p = *app.p;
    GtkWidget* bar = ui::css(ui::hbox(16), {"ss-savebar"});
    GtkWidget* text = ui::vbox(2);
    p.savebar_text = ui::label("", {"heading"});
    ui::add(text, {p.savebar_text, ui::label(_("Only a preview until you save it into the mouse (Ctrl+S)."), {"ss-hint"})});
    gtk_widget_set_valign(text, GTK_ALIGN_CENTER);
    p.revert_btn = ui::button(_("Discard"), {"flat"});
    p.save_btn = ui::button(_("Save to mouse"), {"suggested-action", "pill"});
    gtk_widget_set_tooltip_text(p.save_btn, _("Write the changes into the mouse memory (Ctrl+S)"));
    for (GtkWidget* b : {p.revert_btn, p.save_btn}) gtk_widget_set_valign(b, GTK_ALIGN_CENTER);
    ui::on(p.revert_btn, "clicked", [&app] { app.revert(); });
    ui::on(p.save_btn, "clicked", [&app] { app.save(); });
    ui::add(bar, {ui::css(ui::icon("document-save-symbolic", 22), {"ss-savebar-icon"}), text, ui::spacer(), p.revert_btn, p.save_btn});
    p.savebar = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(p.savebar), GTK_REVEALER_TRANSITION_TYPE_SLIDE_UP);
    gtk_revealer_set_transition_duration(GTK_REVEALER(p.savebar), 260);
    gtk_revealer_set_child(GTK_REVEALER(p.savebar), bar);
    return p.savebar;
}

static void fill_profiles(App& app) {
    auto& p = *app.p;
    p.filling = true;
    GtkStringList* model = gtk_string_list_new(nullptr);
    int sel = 0;
    for (size_t i = 0; i < app.dir.size(); i++) {
        int s = app.dir[i].sector;
        // TRANSLATORS: entry in the profile list, %s is the profile, for example "Profile 2"
        std::string t = s == app.active ? fmt(_("%s  (active)"), profile_label(app, s).c_str()) : profile_label(app, s);
        if (app.dirty.count(s)) t += "  •";
        gtk_string_list_append(model, t.c_str());
        if (s == app.edit) sel = (int)i;
    }
    gtk_drop_down_set_model(GTK_DROP_DOWN(p.profile_dd), G_LIST_MODEL(model));
    g_object_unref(model);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(p.profile_dd), sel);
    p.filling = false;
}

static GtkWidget* build_sidebar(App& app) {
    auto& p = *app.p;
    GtkWidget* side = ui::vbox(0);
    ui::css(side, {"ss-sidebar"});

    GtkWidget* card = ui::css(ui::hbox(12), {"ss-device-card"});
    GtkWidget* info = ui::vbox(2);
    p.device_name = ui::label(app.info.name, {"heading"});
    gtk_label_set_wrap(GTK_LABEL(p.device_name), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(p.device_name), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_lines(GTK_LABEL(p.device_name), 2);
    gtk_label_set_ellipsize(GTK_LABEL(p.device_name), PANGO_ELLIPSIZE_END);
    GtkWidget* st = ui::hbox(6);
    p.status_dot = ui::css(ui::label("●"), {"ss-dot"});
    p.status_text = ui::label(_("Searching for the mouse…"), {"ss-hint"});
    ui::add(st, {p.status_dot, p.status_text});
    ui::add(info, {p.device_name, st});
    gtk_widget_set_valign(info, GTK_ALIGN_CENTER);
    GtkWidget* logo = gtk_picture_new_for_filename(asset_path("io.github.superstrike.Studio.svg").c_str());
    gtk_picture_set_content_fit(GTK_PICTURE(logo), GTK_CONTENT_FIT_CONTAIN);
    gtk_widget_set_size_request(logo, 40, 40);
    gtk_widget_set_valign(logo, GTK_ALIGN_CENTER);
    ui::add(card, {logo, info});
    ui::add(side, {ui::margins(card, 18, 16, 14, 16)});

    p.nav = gtk_list_box_new();
    ui::css(p.nav, {"navigation-sidebar", "ss-nav"});
    for (auto& pg : PAGES) {
        GtkWidget* row = ui::hbox(12);
        ui::add(row, {ui::icon(pg.icon, 18), ui::label(C_("page", pg.title))});
        gtk_list_box_append(GTK_LIST_BOX(p.nav), ui::margins(row, 10, 8, 10, 8));
    }
    ui::add(side, {ui::margins(p.nav, 0, 8, 0, 8)});
    GtkWidget* fill = ui::vbox(0);
    gtk_widget_set_vexpand(fill, TRUE);
    ui::add(side, {fill});

    GtkWidget* ed = ui::vbox(8);
    p.profile_dd = gtk_drop_down_new_from_strings((const char* const[]){"—", nullptr});
    gtk_widget_set_tooltip_text(p.profile_dd, _("The profile you are editing"));
    ui::add(ed, {ui::label(_("EDITING"), {"ss-title"}), p.profile_dd});
    ui::add(side, {ui::margins(ed, 12, 16, 18, 16)});
    ui::notify(p.profile_dd, "selected", [&app] {
        guint i = gtk_drop_down_get_selected(GTK_DROP_DOWN(app.p->profile_dd));
        if (!app.p->filling && i < app.dir.size() && app.dir[i].sector != app.edit) app.set_edit(app.dir[i].sector);
    });
    return side;
}

static gboolean on_close(GtkWindow*, gpointer data) {
    App& app = *static_cast<App*>(data);
    if (app.dirty.empty() || !app.connected) return FALSE;
    AdwDialog* d = adw_alert_dialog_new(_("Unsaved changes"), _("Write them into the mouse before closing?"));
    adw_alert_dialog_add_responses(ADW_ALERT_DIALOG(d), "discard", _("Discard"), "cancel", _("Cancel"), "save", _("Save"), nullptr);
    adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(d), "discard", ADW_RESPONSE_DESTRUCTIVE);
    adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(d), "save", ADW_RESPONSE_SUGGESTED);
    adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(d), "save");
    ui::on1<const char*>(d, "response", [&app](const char* r) {
        if (!g_strcmp0(r, "discard")) {
            app.dirty.clear();
            gtk_window_close(app.window);
        } else if (!g_strcmp0(r, "save")) {
            app.save();
        }
    });
    adw_dialog_present(d, GTK_WIDGET(app.window));
    return TRUE;
}

// Sidebar, header, pages and save bar: rebuilt when the language changes. The window, split view,
// toast overlay, breakpoint and shortcuts stay.
static void build_chrome(App& app) {
    auto& p = *app.p;
    GtkWidget* split = p.split;
    p.pages.clear();
    adw_overlay_split_view_set_sidebar(ADW_OVERLAY_SPLIT_VIEW(split), build_sidebar(app));

    GtkWidget* tv = adw_toolbar_view_new();
    GtkWidget* hb = adw_header_bar_new();
    p.title = ADW_WINDOW_TITLE(adw_window_title_new("", ""));
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(hb), GTK_WIDGET(p.title));
    GtkWidget* toggle = gtk_toggle_button_new();
    gtk_button_set_icon_name(GTK_BUTTON(toggle), "sidebar-show-symbolic");
    g_object_bind_property(split, "show-sidebar", toggle, "active", GBindingFlags(G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE));
    g_object_bind_property(split, "collapsed", toggle, "visible", G_BINDING_SYNC_CREATE);
    adw_header_bar_pack_start(ADW_HEADER_BAR(hb), toggle);
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(tv), hb);
    p.banner = ADW_BANNER(adw_banner_new(_("Searching for the PRO X3 SUPERSTRIKE — switch the mouse on or plug in the receiver")));
    adw_banner_set_button_label(p.banner, _("Retry"));
    ui::on(p.banner, "button-clicked", [&app] { app.connect(); });
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(tv), GTK_WIDGET(p.banner));

    p.stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(p.stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration(GTK_STACK(p.stack), 220);
    for (auto& pg : PAGES) {
        gtk_stack_add_named(GTK_STACK(p.stack), pg.build(app), pg.name);
        p.pages.emplace_back(pg.name, pg.title);  // title is the untranslated msgid
    }
    p.content = p.stack;
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(tv), p.stack);
    adw_toolbar_view_add_bottom_bar(ADW_TOOLBAR_VIEW(tv), build_savebar(app));
    adw_overlay_split_view_set_content(ADW_OVERLAY_SPLIT_VIEW(split), tv);

    ui::on1<GtkListBoxRow*>(p.nav, "row-selected", [&app](GtkListBoxRow* row) {
        if (!row) return;
        gtk_stack_set_visible_child_name(GTK_STACK(app.p->stack), app.p->pages[gtk_list_box_row_get_index(row)].first.c_str());
        update_title(app);
    });
    app.on_status.push_back([&app] { update_status(app); update_dirty(app); fill_profiles(app); });
    app.on_dirty.push_back([&app] { update_dirty(app); fill_profiles(app); });
    app.on_reload.push_back([&app] { fill_profiles(app); update_title(app); });
}

void App::set_language(const std::string& code) {
    cfg.lang = code;
    cfg.save();
    i18n_set(code);
    gtk_widget_set_default_direction(i18n_rtl() ? GTK_TEXT_DIR_RTL : GTK_TEXT_DIR_LTR);
    ui::idle([this] {  // not inside the combo row's own signal: it is destroyed by the rebuild
        GtkListBoxRow* row = gtk_list_box_get_selected_row(GTK_LIST_BOX(p->nav));
        std::string page = row ? p->pages[gtk_list_box_row_get_index(row)].first : "device";
        for (auto* v : {&on_reload, &on_dirty, &on_status}) v->clear();
        on_button.clear();
        on_scroll.clear();
        build_chrome(*this);
        show_page(page.c_str());
        for (auto& f : on_status) f();
        reload_ui();
    });
}

void window_build(App& app, GtkApplication* gapp) {
    auto& p = *app.p;
    GtkCssProvider* css = gtk_css_provider_new();
    gtk_css_provider_load_from_path(css, asset_path("style.css").c_str());
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(css),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);
    gtk_icon_theme_add_search_path(gtk_icon_theme_get_for_display(gdk_display_get_default()), asset_path("icons").c_str());

    GtkWidget* win = adw_application_window_new(gapp);
    app.window = GTK_WINDOW(win);
    gtk_window_set_title(app.window, "SuperStrike Studio");
    gtk_window_set_icon_name(app.window, "io.github.superstrike.Studio");
    gtk_window_set_default_size(app.window, 1320, 860);
    g_signal_connect(win, "close-request", G_CALLBACK(on_close), &app);

    p.split = adw_overlay_split_view_new();
    adw_overlay_split_view_set_min_sidebar_width(ADW_OVERLAY_SPLIT_VIEW(p.split), 232);
    adw_overlay_split_view_set_max_sidebar_width(ADW_OVERLAY_SPLIT_VIEW(p.split), 248);
    app.toasts = ADW_TOAST_OVERLAY(adw_toast_overlay_new());
    adw_toast_overlay_set_child(app.toasts, p.split);
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(win), GTK_WIDGET(app.toasts));

    GtkEventController* keys = gtk_shortcut_controller_new();
    gtk_shortcut_controller_set_scope(GTK_SHORTCUT_CONTROLLER(keys), GTK_SHORTCUT_SCOPE_GLOBAL);
    gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(keys), gtk_shortcut_new(
        gtk_shortcut_trigger_parse_string("<Control>s"),
        gtk_callback_action_new([](GtkWidget*, GVariant*, gpointer d) -> gboolean { static_cast<App*>(d)->save(); return TRUE; },
                                &app, nullptr)));
    gtk_widget_add_controller(win, keys);

    AdwBreakpoint* bp = adw_breakpoint_new(adw_breakpoint_condition_parse("max-width: 980sp"));
    GValue v = G_VALUE_INIT;
    g_value_init(&v, G_TYPE_BOOLEAN);
    g_value_set_boolean(&v, TRUE);
    adw_breakpoint_add_setter(bp, G_OBJECT(p.split), "collapsed", &v);
    adw_application_window_add_breakpoint(ADW_APPLICATION_WINDOW(win), bp);

    build_chrome(app);
    app.show_page("hits");
    update_status(app);
    update_dirty(app);
    gtk_window_present(app.window);
}
