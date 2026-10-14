// Sensor page: DPI stages, polling rate, tracking and power settings of the edited profile.
#include <algorithm>
#include <cmath>
#include <memory>
#include "app.hpp"
#include "i18n.hpp"
#include "ui.hpp"

namespace {

constexpr int STAGES = 5;
constexpr int DEFAULT_DPI = 800;
const char* const RATE_LABELS[7] = {"125", "250", "500", "1K", "2K", "4K", "8K"};

GtkWidget* panel(const char* title, GtkWidget** body) {
    GtkWidget* p = ui::css(ui::vbox(12), {"ss-panel"});
    gtk_box_append(GTK_BOX(p), ui::label(title, {"ss-title"}));
    *body = p;
    return p;
}

GtkWidget* boxed_list() {
    GtkWidget* l = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(l), GTK_SELECTION_NONE);
    return ui::css(l, {"boxed-list"});
}

void add_row(GtkWidget* list, GtkWidget* row) { gtk_list_box_append(GTK_LIST_BOX(list), row); }

GtkWidget* switch_row(const char* title, const char* subtitle = nullptr) {
    GtkWidget* r = adw_switch_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(r), title);
    if (subtitle) adw_action_row_set_subtitle(ADW_ACTION_ROW(r), subtitle);
    return r;
}

GtkWidget* spin_row(const char* title, double lo, double hi, double step, const char* subtitle = nullptr) {
    GtkWidget* r = adw_spin_row_new_with_range(lo, hi, step);
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(r), title);
    if (subtitle) adw_action_row_set_subtitle(ADW_ACTION_ROW(r), subtitle);
    return r;
}

GtkWidget* combo_row(const char* title, const char* const* items, const char* subtitle = nullptr) {
    GtkWidget* r = adw_combo_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(r), title);
    adw_combo_row_set_model(ADW_COMBO_ROW(r), G_LIST_MODEL(gtk_string_list_new(items)));
    if (subtitle) adw_action_row_set_subtitle(ADW_ACTION_ROW(r), subtitle);
    return r;
}

double spin_value(GtkWidget* r) { return adw_spin_row_get_value(ADW_SPIN_ROW(r)); }
bool switch_on(GtkWidget* r) { return adw_switch_row_get_active(ADW_SWITCH_ROW(r)); }

// Nearest value of the sorted list. When that is the old value, step one entry in the move direction.
int snap_dpi(const std::vector<int>& list, int v, int prev) {
    if (list.empty()) return v;
    auto it = std::lower_bound(list.begin(), list.end(), v);
    int hi = it == list.end() ? list.back() : *it;
    int lo = it == list.begin() ? list.front() : *(it - 1);
    if (hi == v) return v;
    int best = v - lo <= hi - v ? lo : hi;
    if (best == prev && v != prev) best = v > prev ? hi : lo;
    return best;
}

struct Chip {
    GtkWidget *button, *dot, *dpi, *box, *inuse;
};

struct Sensor {
    explicit Sensor(App& a) : app(a) {}
    App& app;
    bool loading = false;
    int sel = 0;
    bool separate[STAGES] = {};
    Chip chips[STAGES];
    GtkWidget *root, *enabled, *dpi_x, *sep, *dpi_y, *lod, *is_default, *use_now;
    AdwToggleGroup *wireless, *cable;
    GtkWidget *angle, *surface, *filter, *sleep, *off;

    Stage* stage(int i) {
        Profile* p = app.cur();
        return p ? &p->stages[i] : nullptr;
    }
    void changed() { app.mark_dirty(app.edit); }
    void set_spin(GtkWidget* row, int v) {
        bool was = loading;
        loading = true;
        adw_spin_row_set_value(ADW_SPIN_ROW(row), v);
        loading = was;
    }

    void update_chip(int i) {
        Profile* p = app.cur();
        if (!p) return;
        const Stage& s = p->stages[i];
        gtk_label_set_text(GTK_LABEL(chips[i].dpi), s.x ? std::to_string(s.x).c_str() : C_("DPI stage", "off"));
        if (s.x) gtk_widget_remove_css_class(chips[i].box, "dim-label");
        else gtk_widget_add_css_class(chips[i].box, "dim-label");
        gtk_widget_set_opacity(chips[i].dot, p->default_stage == i ? 1 : 0);
        bool live = app.connected && app.edit == app.active && app.info.stage == i;
        gtk_widget_set_opacity(chips[i].inuse, live ? 1 : 0);
    }

    // Clicking a stage of the active profile switches the mouse to it at once.
    void switch_live(int i) {
        if (app.edit != app.active || !app.connected || !stage(i) || !stage(i)->x || app.info.stage == i) return;
        app.run([i](Device& d) { d.set_current_stage(i); },
                [this, i] {
                    app.info.stage = i;
                    for (int k = 0; k < STAGES; k++) update_chip(k);
                    bool unsaved = app.dirty.count(app.edit) > 0;
                    app.toast(unsaved
                                  // TRANSLATORS: %d is the DPI stage number (1 to 5)
                                  ? fmt(_("Mouse now uses stage %d — save (Ctrl+S) to feel your new DPI values"), i + 1)
                                  // TRANSLATORS: %d is the DPI stage number (1 to 5)
                                  : fmt(_("Mouse now uses stage %d"), i + 1));
                });
    }

    void update_editor() {
        Profile* p = app.cur();
        gtk_widget_set_sensitive(root, p != nullptr);
        if (!p) return;
        const Stage& s = p->stages[sel];
        bool on = s.x != 0;
        loading = true;
        adw_switch_row_set_active(ADW_SWITCH_ROW(enabled), on);
        adw_spin_row_set_value(ADW_SPIN_ROW(dpi_x), on ? s.x : DEFAULT_DPI);
        adw_spin_row_set_value(ADW_SPIN_ROW(dpi_y), on ? s.y : DEFAULT_DPI);
        adw_switch_row_set_active(ADW_SWITCH_ROW(sep), separate[sel]);
        adw_combo_row_set_selected(ADW_COMBO_ROW(lod), std::clamp(s.lod - 1, 0, 2));
        adw_switch_row_set_active(ADW_SWITCH_ROW(is_default), p->default_stage == sel);
        loading = false;
        for (GtkWidget* w : {dpi_x, sep, lod, is_default}) gtk_widget_set_sensitive(w, on);
        gtk_widget_set_visible(dpi_y, separate[sel]);
        gtk_widget_set_sensitive(dpi_y, on);
        gtk_widget_set_sensitive(use_now, on && app.connected && app.edit == app.active);
    }

    void refresh() {
        Profile* p = app.cur();
        if (p)
            for (int i = 0; i < STAGES; i++) {
                separate[i] = p->stages[i].x != p->stages[i].y;
                update_chip(i);
            }
        update_editor();
        loading = true;
        if (p) {
            adw_toggle_group_set_active(wireless, std::clamp(p->rate_wireless, 0, 6));
            adw_toggle_group_set_active(cable, std::clamp(p->rate_wired, 0, 3));
            adw_switch_row_set_active(ADW_SWITCH_ROW(angle), p->angle_snap != 0);
            adw_spin_row_set_value(ADW_SPIN_ROW(filter), std::min(p->scroll_filter, 100) * 10);
            adw_spin_row_set_value(ADW_SPIN_ROW(sleep), p->sleep_s);
            adw_spin_row_set_value(ADW_SPIN_ROW(off), p->off_s);
        }
        refresh_device();
        loading = false;
    }

    void refresh_device() {
        bool was = loading;
        loading = true;
        adw_combo_row_set_selected(ADW_COMBO_ROW(surface), std::max(app.info.surface, 0));
        loading = was;
        gtk_widget_set_sensitive(surface, app.connected);
        gtk_widget_set_sensitive(use_now,
                                 app.cur() && stage(sel) && stage(sel)->x && app.connected && app.edit == app.active);
    }

    // Keep the default and shift stage on an enabled stage.
    void fix_references(Profile& p) {
        int first = 0;
        while (first < STAGES - 1 && !p.stages[first].x) first++;
        if (!p.stages[p.default_stage].x) p.default_stage = first;
        if (!p.stages[p.shift_stage].x) p.shift_stage = first;
    }

    void on_enabled() {
        Profile* p = app.cur();
        if (loading || !p) return;
        Stage& s = p->stages[sel];
        if (switch_on(enabled)) {
            if (s.x) return;
            s = Stage{snap_dpi(app.info.dpi_list, DEFAULT_DPI, 0), 0, 2};
            s.y = s.x;
            separate[sel] = false;
        } else {
            int on = 0;
            for (auto& st : p->stages) on += st.x != 0;
            if (on <= 1) {
                loading = true;
                adw_switch_row_set_active(ADW_SWITCH_ROW(enabled), TRUE);
                loading = false;
                app.toast(_("At least one stage must stay enabled"));
                return;
            }
            s = Stage{};
            fix_references(*p);
        }
        for (int i = 0; i < STAGES; i++) update_chip(i);
        update_editor();
        changed();
    }

    void on_dpi(bool y) {
        Profile* p = app.cur();
        if (loading || !p) return;
        Stage& s = p->stages[sel];
        if (!s.x) return;
        GtkWidget* row = y ? dpi_y : dpi_x;
        int v = snap_dpi(app.info.dpi_list, (int)std::lround(spin_value(row)), y ? s.y : s.x);
        if (v != (int)spin_value(row)) set_spin(row, v);
        if (y) s.y = v;
        else {
            s.x = v;
            if (!separate[sel]) s.y = v;
        }
        update_chip(sel);
        changed();
    }

    void on_separate() {
        Profile* p = app.cur();
        if (loading || !p || !p->stages[sel].x) return;
        Stage& s = p->stages[sel];
        separate[sel] = switch_on(sep);
        if (!separate[sel]) s.y = s.x;
        set_spin(dpi_y, s.y);
        gtk_widget_set_visible(dpi_y, separate[sel]);
        changed();
    }

    void on_default() {
        Profile* p = app.cur();
        if (loading || !p) return;
        if (switch_on(is_default)) {
            p->default_stage = sel;
        } else {
            loading = true;
            adw_switch_row_set_active(ADW_SWITCH_ROW(is_default), TRUE);
            loading = false;
            app.toast(_("Pick another stage as the default instead"));
            return;
        }
        for (int i = 0; i < STAGES; i++) update_chip(i);
        changed();
    }

    void on_use_now() {
        if (app.edit != app.active || app.edit < 0) {
            app.toast(_("Activate this profile first (Profiles page)"));
            return;
        }
        int n = sel;
        bool unsaved = app.dirty.count(app.edit) > 0;
        app.run([n](Device& d) { d.set_current_stage(n); },
                [this, n, unsaved] {
                    app.toast(unsaved
                                  // TRANSLATORS: %d is the DPI stage number (1 to 5)
                                  ? fmt(_("Stage %d is now active (save to use the new values)"), n + 1)
                                  // TRANSLATORS: %d is the DPI stage number (1 to 5)
                                  : fmt(_("Stage %d is now active"), n + 1));
                });
    }

    void on_rate(AdwToggleGroup* g, bool wired) {
        Profile* p = app.cur();
        guint idx = adw_toggle_group_get_active(g);
        if (loading || !p || idx == GTK_INVALID_LIST_POSITION) return;
        (wired ? p->rate_wired : p->rate_wireless) = (int)idx;
        changed();
    }

    void on_surface() {
        if (loading || !app.connected) return;
        int m = (int)adw_combo_row_get_selected(ADW_COMBO_ROW(surface));
        app.run([m](Device& d) { d.surface_set(m); },
                [this, m] {
                    app.info.surface = m;
                    app.toast(_("Surface mode set"));
                });
    }

    template <class F> void tracking_edit(F f) {
        Profile* p = app.cur();
        if (loading || !p) return;
        f(*p);
        changed();
    }
};

AdwToggleGroup* rate_group(int count) {
    auto* g = ADW_TOGGLE_GROUP(adw_toggle_group_new());
    for (int i = 0; i < count; i++) {
        AdwToggle* t = adw_toggle_new();
        adw_toggle_set_name(t, RATE_LABELS[i]);
        adw_toggle_set_label(t, RATE_LABELS[i]);
        adw_toggle_group_add(g, t);
    }
    gtk_widget_set_valign(GTK_WIDGET(g), GTK_ALIGN_CENTER);
    return g;
}

GtkWidget* rate_row(const char* title, AdwToggleGroup* g) {
    GtkWidget* r = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(r), title);
    adw_action_row_add_suffix(ADW_ACTION_ROW(r), GTK_WIDGET(g));
    return r;
}

GtkWidget* stage_chips(Sensor* s) {
    GtkWidget* row = ui::hbox(8);
    gtk_box_set_homogeneous(GTK_BOX(row), TRUE);
    GtkWidget* first = nullptr;
    for (int i = 0; i < STAGES; i++) {
        Chip& c = s->chips[i];
        c.button = ui::css(gtk_toggle_button_new(), {"ss-chip"});
        if (first) gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(c.button), GTK_TOGGLE_BUTTON(first));
        else first = c.button;
        c.box = ui::vbox(2);
        c.dot = ui::css(ui::label("★"), {"ss-star"});
        gtk_widget_set_tooltip_text(c.dot, _("Used after power-on"));
        c.inuse = ui::css(ui::label(C_("DPI stage", "IN USE"), {}, 0.5f), {"ss-inuse"});
        c.dpi = ui::label("", {}, 0.5f);
        GtkWidget* top = ui::hbox(4);
        gtk_widget_set_halign(top, GTK_ALIGN_CENTER);
        ui::add(top, {ui::label(std::to_string(i + 1), {"ss-hint"}, 0.5f), c.dot});
        ui::add(c.box, {top, c.dpi, c.inuse});
        gtk_button_set_child(GTK_BUTTON(c.button), c.box);
        ui::on(c.button, "toggled", [s, i] {
            if (s->loading || !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->chips[i].button))) return;
            s->sel = i;
            s->update_editor();
            s->switch_live(i);
        });
        gtk_box_append(GTK_BOX(row), c.button);
    }
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s->chips[0].button), TRUE);
    return row;
}

GtkWidget* dpi_panel(Sensor* s) {
    GtkWidget* body;
    GtkWidget* pan = panel(_("DPI STAGES"), &body);
    gtk_box_append(GTK_BOX(body), stage_chips(s));
    GtkWidget* hint = ui::label(_("Click a stage to switch the mouse to it now. New DPI values are felt after "
                                  "Save to mouse (Ctrl+S) — the mouse takes DPI only from its memory. "
                                  "★ = stage after power-on."), {"ss-hint"});
    gtk_label_set_wrap(GTK_LABEL(hint), TRUE);
    gtk_box_append(GTK_BOX(body), hint);

    s->enabled = switch_row(C_("DPI stage", "Enabled"));
    s->dpi_x = spin_row("DPI", 100, 48000, 50);
    s->sep = switch_row(_("Separate X / Y"));
    s->dpi_y = spin_row(_("DPI Y"), 100, 48000, 50);
    const char* lods[] = {C_("lift-off distance", "Low"), C_("lift-off distance", "Medium"), C_("lift-off distance", "High"), nullptr};
    s->lod = combo_row(_("Lift-off distance"), lods);
    s->is_default = switch_row(_("Default after power-on"));
    s->use_now = adw_button_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(s->use_now), _("Use this stage now"));

    GtkWidget* list = boxed_list();
    for (GtkWidget* r : {s->enabled, s->dpi_x, s->sep, s->dpi_y, s->lod, s->is_default, s->use_now}) add_row(list, r);
    gtk_widget_set_visible(s->use_now, FALSE);  // clicking a stage chip switches the mouse now
    gtk_box_append(GTK_BOX(body), list);

    ui::notify(s->enabled, "active", [s] { s->on_enabled(); });
    ui::notify(s->dpi_x, "value", [s] { s->on_dpi(false); });
    ui::notify(s->dpi_y, "value", [s] { s->on_dpi(true); });
    ui::notify(s->sep, "active", [s] { s->on_separate(); });
    ui::notify(s->lod, "selected", [s] {
        if (Stage* st = s->stage(s->sel); st && !s->loading && st->x) {
            st->lod = (int)adw_combo_row_get_selected(ADW_COMBO_ROW(s->lod)) + 1;
            s->changed();
        }
    });
    ui::notify(s->is_default, "active", [s] { s->on_default(); });
    ui::on(s->use_now, "activated", [s] { s->on_use_now(); });
    return pan;
}

GtkWidget* polling_panel(Sensor* s) {
    GtkWidget* body;
    GtkWidget* pan = panel(_("POLLING RATE"), &body);
    s->wireless = rate_group(7);
    s->cable = rate_group(4);
    GtkWidget* list = boxed_list();
    add_row(list, rate_row(C_("polling rate", "Wireless"), s->wireless));
    add_row(list, rate_row(C_("polling rate", "Cable"), s->cable));
    gtk_box_append(GTK_BOX(body), list);
    gtk_box_append(GTK_BOX(body), ui::label(_("Hz. Higher rates feel smoother and use more battery."), {"ss-hint"}));
    ui::notify(s->wireless, "active", [s] { s->on_rate(s->wireless, false); });
    ui::notify(s->cable, "active", [s] { s->on_rate(s->cable, true); });
    return pan;
}

GtkWidget* tracking_panel(Sensor* s) {
    GtkWidget* body;
    GtkWidget* pan = panel(_("TRACKING"), &body);
    s->angle = switch_row(_("Angle snapping"), _("Straightens near-horizontal and vertical lines"));
    const char* modes[] = {C_("surface mode", "Auto"), C_("surface mode", "On"), C_("surface mode", "Off"), nullptr};
    s->surface = combo_row(_("Gaming surface mode"), modes, _("Applies at once. Device setting, not per profile"));
    s->filter = spin_row(_("Scroll filter (ms)"), 0, 1000, 10, _("Bunny-hop scroll filter, 0 = off"));
    GtkWidget* list = boxed_list();
    for (GtkWidget* r : {s->angle, s->surface, s->filter}) add_row(list, r);
    gtk_box_append(GTK_BOX(body), list);

    ui::notify(s->angle, "active", [s] {
        s->tracking_edit([s](Profile& p) { p.angle_snap = switch_on(s->angle) ? 1 : 0; });
    });
    ui::notify(s->surface, "selected", [s] { s->on_surface(); });
    ui::notify(s->filter, "value", [s] {
        s->tracking_edit([s](Profile& p) { p.scroll_filter = (int)std::lround(spin_value(s->filter)) / 10; });
    });
    return pan;
}

GtkWidget* power_panel(Sensor* s) {
    GtkWidget* body;
    GtkWidget* pan = panel(_("POWER"), &body);
    s->sleep = spin_row(_("Low power after (s)"), 1, 3600, 5);
    s->off = spin_row(_("Sleep after (s)"), 10, 36000, 30);
    GtkWidget* list = boxed_list();
    add_row(list, s->sleep);
    add_row(list, s->off);
    gtk_box_append(GTK_BOX(body), list);
    ui::notify(s->sleep, "value", [s] {
        s->tracking_edit([s](Profile& p) { p.sleep_s = (int)std::lround(spin_value(s->sleep)); });
    });
    ui::notify(s->off, "value", [s] {
        s->tracking_edit([s](Profile& p) { p.off_s = (int)std::lround(spin_value(s->off)); });
    });
    return pan;
}

}  // namespace

GtkWidget* page_sensor(App& app) {
    auto s = std::make_shared<Sensor>(app);
    Sensor* sp = s.get();
    sp->loading = true;
    GtkWidget* col = ui::vbox(16);
    ui::add(col, {dpi_panel(sp), polling_panel(sp), tracking_panel(sp), power_panel(sp)});
    sp->root = col;
    sp->loading = false;
    app.on_reload.push_back([s] { s->refresh(); });
    app.on_status.push_back([s] { s->refresh_device(); });
    sp->refresh();
    return ui::page_column(col);
}
