// Buttons page: assign an action to each of the five buttons, normal and DPI-shift layer.
#include <cstdio>
#include <memory>
#include "app.hpp"
#include "i18n.hpp"
#include "ui.hpp"
#include "widgets.hpp"

namespace {

// Short button names on the callouts (shown with C_("button label", ...)).
const char* const SHORT_NAMES[5] = {NC_("button label", "Left"), NC_("button label", "Right"),
                                    NC_("button label", "Middle"), NC_("button label", "Back"),
                                    NC_("button label", "Forward")};
// HID modifier bits 1, 2, 4, 8 (shown with C_("modifier key", ...), same msgids as MODIFIERS)
const char* const MOD_NAMES[4] = {NC_("modifier key", "Ctrl"), NC_("modifier key", "Shift"),
                                  NC_("modifier key", "Alt"), NC_("modifier key", "Super")};
struct KindChip { Kind kind; const char* label; };  // label is a msgid, context "button action"
const KindChip KIND_CHIPS[6] = {{Kind::Mouse, NC_("button action", "Mouse")},
                                {Kind::Key, NC_("button action", "Keyboard")},
                                {Kind::Media, NC_("button action", "Media")},
                                {Kind::Function, NC_("button action", "Function")},
                                {Kind::Disabled, NC_("button action", "Disabled")},
                                {Kind::Default, NC_("button action", "Default")}};

struct St {
    explicit St(App& a) : app(a) {}
    App& app;
    bool loading = false;
    int layer = 0;  // 0 normal, 1 DPI-shift
    int sel = 0;

    GtkWidget* root = nullptr;
    MouseView* mv = nullptr;
    AdwToggleGroup *view_tg = nullptr, *layer_tg = nullptr;
    GtkWidget* rows[5] = {};
    GtkWidget* chips[6] = {};
    GtkWidget *mouse_list = nullptr, *key_box = nullptr, *media_list = nullptr, *func_list = nullptr;
    GtkWidget *raw_list = nullptr, *raw_row = nullptr, *default_hint = nullptr, *restore = nullptr;
    GtkWidget *mouse_row = nullptr, *key_row = nullptr, *media_row = nullptr, *func_row = nullptr;
    GtkWidget* mods[4] = {};
};
using SP = std::shared_ptr<St>;

Action* slot_at(St& s, int i) {
    Profile* p = s.app.cur();
    if (!p) return nullptr;
    return s.layer ? &p->shift_buttons[i] : &p->buttons[i];
}

Action effective(St& s, int i) {
    Action* a = slot_at(s, i);
    if (!a) return {};
    if (a->kind() == Kind::Default) {
        const Action& f = s.layer ? s.app.factory.shift_buttons[i] : s.app.factory.buttons[i];
        if (f.kind() != Kind::Default) return f;
    }
    return *a;
}

guint index_of(const std::vector<Named>& table, int code) {
    for (size_t i = 0; i < table.size(); i++)
        if (table[i].code == code) return (guint)i;
    return GTK_INVALID_LIST_POSITION;
}

int zone_index(const std::string& zone) {
    for (int i = 0; i < 5; i++)
        if (zone == BUTTON_IDS[i]) return i;
    return -1;
}

std::string hex_text(const Action& a) {
    char b[16];
    std::string t;
    for (size_t i = 0; i < a.raw.size(); i++) {
        snprintf(b, sizeof b, i ? " %02x" : "%02x", a.raw[i]);
        t += b;
    }
    return t;
}

void refresh(St& s);

void set_action(const std::shared_ptr<St>& s, const Action& a) {
    Action* slot = slot_at(*s, s->sel);
    if (!slot || slot->raw == a.raw) return;
    *slot = a;
    s->app.mark_dirty(s->app.edit);
    refresh(*s);
}

int mods_from_chips(St& s) {
    int m = 0;
    for (int i = 0; i < 4; i++)
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s.mods[i]))) m |= 1 << i;
    return m;
}

Action action_from_ui(St& s, Kind k) {
    auto pick = [](GtkWidget* row, const std::vector<Named>& t) {
        guint i = adw_combo_row_get_selected(ADW_COMBO_ROW(row));
        return i < t.size() ? t[i].code : 0;
    };
    switch (k) {
        case Kind::Mouse: return Action::make(k, pick(s.mouse_row, MOUSE_BUTTONS));
        case Kind::Media: return Action::make(k, pick(s.media_row, MEDIA));
        case Kind::Function: return Action::make(k, pick(s.func_row, FUNCTIONS));
        case Kind::Key: {
            guint i = adw_combo_row_get_selected(ADW_COMBO_ROW(s.key_row));
            Action* cur = slot_at(s, s.sel);
            int code = i < KEYS.size() ? KEYS[i].code : (cur && cur->kind() == Kind::Key ? cur->code() : 0);
            return Action::make(k, code, mods_from_chips(s));
        }
        default: return Action::make(k);
    }
}

void refresh(St& s) {
    s.loading = true;
    Profile* p = s.app.cur();
    gtk_widget_set_sensitive(s.root, p != nullptr);
    adw_toggle_group_set_active_name(s.layer_tg, s.layer ? "shift" : "normal");
    for (int i = 0; i < 5; i++) {
        adw_action_row_set_subtitle(ADW_ACTION_ROW(s.rows[i]), p ? effective(s, i).describe().c_str() : "");
        if (i == s.sel) gtk_widget_add_css_class(s.rows[i], "ss-row-selected");
        else gtk_widget_remove_css_class(s.rows[i], "ss-row-selected");
    }
    Action* a = slot_at(s, s.sel);
    Kind k = a ? a->kind() : Kind::Default;
    for (int i = 0; i < 6; i++)
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s.chips[i]), a && KIND_CHIPS[i].kind == k);
    gtk_widget_set_visible(s.mouse_list, k == Kind::Mouse);
    gtk_widget_set_visible(s.key_box, k == Kind::Key);
    gtk_widget_set_visible(s.media_list, k == Kind::Media);
    gtk_widget_set_visible(s.func_list, k == Kind::Function);
    gtk_widget_set_visible(s.raw_list, k == Kind::Raw);
    gtk_widget_set_visible(s.default_hint, k == Kind::Default);
    if (a) {
        auto set = [&](GtkWidget* row, const std::vector<Named>& t, Kind want) {
            if (k == want) adw_combo_row_set_selected(ADW_COMBO_ROW(row), index_of(t, a->code()));
        };
        set(s.mouse_row, MOUSE_BUTTONS, Kind::Mouse);
        set(s.key_row, KEYS, Kind::Key);
        set(s.media_row, MEDIA, Kind::Media);
        set(s.func_row, FUNCTIONS, Kind::Function);
        if (k == Kind::Key)
            for (int i = 0; i < 4; i++) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s.mods[i]), a->mods() >> i & 1);
        adw_action_row_set_subtitle(ADW_ACTION_ROW(s.raw_row), hex_text(*a).c_str());
        // TRANSLATORS: %s is the name of the factory action, for example "Left click"
        gtk_label_set_text(GTK_LABEL(s.default_hint), fmt(_("Uses the factory action: %s"), effective(s, s.sel).describe().c_str()).c_str());
    }
    s.mv->selected = BUTTON_IDS[s.sel];
    s.mv->redraw();
    s.loading = false;
}

void show_view(St& s, const std::string& view) {
    s.loading = true;
    adw_toggle_group_set_active_name(s.view_tg, view.c_str());
    s.loading = false;
    s.mv->set_view(view);
}

void select_button(const SP& s, int i) {
    s->sel = i;
    const std::string id = BUTTON_IDS[i];
    if ((id == "btn-back" || id == "btn-forward") && s->mv->view() == "top") show_view(*s, "side");
    else if ((id == "btn-left" || id == "btn-right") && s->mv->view() == "side") show_view(*s, "top");
    refresh(*s);
}

struct Capture {
    SP s;
    AdwDialog* dialog;
    int pending_mod = 0;
};

void apply_captured(Capture& c, int usage, int mods) {
    Action a = Action::make(Kind::Key, usage, mods);
    set_action(c.s, a);
    // TRANSLATORS: %s is the recorded key, for example "Ctrl+C"
    c.s->app.toast(fmt(_("Recorded: %s"), a.describe().c_str()));
    adw_dialog_close(c.dialog);
}

gboolean on_key_pressed(GtkEventControllerKey*, guint, guint keycode, GdkModifierType state, gpointer d) {
    auto* c = static_cast<Capture*>(d);
    int usage = evdev_to_hid((int)keycode - 8);
    if (usage < 0) return TRUE;
    if (modifier_bit(usage)) {
        c->pending_mod = usage;
        return TRUE;
    }
    c->pending_mod = 0;
    int mods = (state & GDK_CONTROL_MASK ? 1 : 0) | (state & GDK_SHIFT_MASK ? 2 : 0) |
               (state & GDK_ALT_MASK ? 4 : 0) | (state & GDK_SUPER_MASK ? 8 : 0);
    apply_captured(*c, usage, mods);
    return TRUE;
}

void on_key_released(GtkEventControllerKey*, guint, guint keycode, GdkModifierType, gpointer d) {
    auto* c = static_cast<Capture*>(d);
    int usage = evdev_to_hid((int)keycode - 8);
    if (usage >= 0 && usage == c->pending_mod) {
        c->pending_mod = 0;
        apply_captured(*c, usage, 0);
    }
}

void record_key(const SP& s) {
    if (!slot_at(*s, s->sel)) return;
    AdwDialog* d = adw_alert_dialog_new(_("Press a key"),
                                        _("Press the key, with Ctrl, Shift, Alt or Super if you want them."));
    adw_alert_dialog_add_response(ADW_ALERT_DIALOG(d), "cancel", _("Cancel"));
    adw_alert_dialog_set_close_response(ADW_ALERT_DIALOG(d), "cancel");
    GtkEventController* kc = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(kc, GTK_PHASE_CAPTURE);
    auto* cap = new Capture{s, d};
    g_object_set_data_full(G_OBJECT(kc), "capture", cap, +[](gpointer p) { delete static_cast<Capture*>(p); });
    g_signal_connect(kc, "key-pressed", G_CALLBACK(on_key_pressed), cap);
    g_signal_connect(kc, "key-released", G_CALLBACK(on_key_released), cap);
    gtk_widget_add_controller(GTK_WIDGET(d), kc);
    adw_dialog_present(d, s->root);
}

GtkWidget* title(const char* text) { return ui::label(text, {"ss-title"}); }

GtkWidget* boxed_list() {
    GtkWidget* l = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(l), GTK_SELECTION_NONE);
    return ui::css(l, {"boxed-list"});
}

GtkWidget* combo_row(const char* name, const std::vector<Named>& items, bool search) {
    GtkStringList* list = gtk_string_list_new(nullptr);
    for (auto& it : items) gtk_string_list_append(list, named_text(items, it));
    GtkWidget* row = adw_combo_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), name);
    adw_combo_row_set_model(ADW_COMBO_ROW(row), G_LIST_MODEL(list));
    g_object_unref(list);
    if (search) {
        adw_combo_row_set_enable_search(ADW_COMBO_ROW(row), TRUE);
        adw_combo_row_set_expression(ADW_COMBO_ROW(row),
                                     gtk_property_expression_new(GTK_TYPE_STRING_OBJECT, nullptr, "string"));
    }
    return row;
}

GtkWidget* single_row_list(GtkWidget* row) {
    GtkWidget* l = boxed_list();
    gtk_list_box_append(GTK_LIST_BOX(l), row);
    return l;
}

GtkWidget* build_view(const SP& s) {
    GtkWidget* box = ui::vbox(12);
    gtk_widget_set_hexpand(box, TRUE);

    s->view_tg = ui::toggles({{"top", C_("view", "Top")}, {"side", C_("view", "Left side")}});
    adw_toggle_group_set_active_name(s->view_tg, "top");
    ui::notify(s->view_tg, "active-name", [s] {
        const char* n = adw_toggle_group_get_active_name(s->view_tg);
        if (!s->loading && n) s->mv->set_view(n);
    });
    GtkWidget* rotate = ui::icon_button("ss-rotate-symbolic", _("Rotate the mouse"));
    ui::on(rotate, "clicked", [s] { show_view(*s, s->mv->view() == "top" ? "side" : "top"); });

    s->layer_tg = ui::toggles({{"normal", C_("button layer", "Normal")}, {"shift", _("DPI-shift layer")}});
    adw_toggle_group_set_active_name(s->layer_tg, "normal");
    gtk_widget_set_tooltip_text(GTK_WIDGET(s->layer_tg), _("Used while a button set to DPI shift is held"));
    ui::notify(s->layer_tg, "active-name", [s] {
        const char* n = adw_toggle_group_get_active_name(s->layer_tg);
        if (s->loading || !n) return;
        s->layer = std::string(n) == "shift";
        refresh(*s);
    });

    GtkWidget* bar = ui::hbox(8);
    ui::add(bar, {GTK_WIDGET(s->view_tg), rotate, ui::spacer(), GTK_WIDGET(s->layer_tg)});

    s->mv = MouseView::create();
    s->mv->callouts = MouseView::Callouts::Lines;
    s->mv->flip = true;
    s->mv->thumbnail = true;
    s->mv->label_for = [s](const std::string& zone) -> std::string {
        int i = zone_index(zone);
        if (i < 0 || !s->app.cur()) return "";
        return std::string(C_("button label", SHORT_NAMES[i])) + "\n" + effective(*s, i).describe();
    };
    s->mv->on_zone = [s](const std::string& zone) {
        int i = zone_index(zone);
        if (i >= 0) select_button(s, i);
    };
    s->mv->on_view = [s](const std::string& view) {
        if (s->loading) return;
        s->loading = true;
        adw_toggle_group_set_active_name(s->view_tg, view.c_str());
        s->loading = false;
    };
    gtk_widget_set_vexpand(s->mv->widget(), TRUE);
    gtk_widget_set_hexpand(s->mv->widget(), TRUE);

    GtkWidget* hint = ui::label(_("Click a button on the mouse to change it. Drag sideways to rotate."), {"ss-hint"}, 0.5);
    gtk_widget_set_halign(hint, GTK_ALIGN_CENTER);
    ui::add(box, {bar, s->mv->widget(), hint});
    return box;
}

GtkWidget* build_button_list(const SP& s) {
    GtkWidget* list = boxed_list();
    for (int i = 0; i < 5; i++) {
        GtkWidget* row = adw_action_row_new();
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), _(BUTTON_NAMES[i]));
        gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), TRUE);
        ui::on(row, "activated", [s, i] { select_button(s, i); });
        gtk_list_box_append(GTK_LIST_BOX(list), row);
        s->rows[i] = row;
    }
    return list;
}

GtkWidget* build_chips(const SP& s) {
    GtkWidget* flow = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(flow), GTK_SELECTION_NONE);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(flow), 3);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(flow), 3);
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(flow), TRUE);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(flow), 8);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(flow), 8);
    for (int i = 0; i < 6; i++) {
        GtkWidget* chip = ui::css(gtk_toggle_button_new_with_label(C_("button action", KIND_CHIPS[i].label)), {"ss-chip"});
        ui::on(chip, "toggled", [s, i] {
            if (s->loading) return;
            if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->chips[i])))
                set_action(s, action_from_ui(*s, KIND_CHIPS[i].kind));
            refresh(*s);
        });
        gtk_flow_box_append(GTK_FLOW_BOX(flow), chip);
        s->chips[i] = chip;
    }
    return flow;
}

void on_combo(GtkWidget* row, Kind k, const SP& s) {
    ui::notify(row, "selected", [s, k] {
        if (!s->loading) set_action(s, action_from_ui(*s, k));
    });
}

GtkWidget* build_detail(const SP& s) {
    GtkWidget* box = ui::vbox(12);

    s->mouse_row = combo_row(_("Mouse button"), MOUSE_BUTTONS, false);
    s->mouse_list = single_row_list(s->mouse_row);
    s->media_row = combo_row(_("Media key"), MEDIA, false);
    s->media_list = single_row_list(s->media_row);
    s->func_row = combo_row(C_("combo row title", "Function"), FUNCTIONS, false);
    s->func_list = single_row_list(s->func_row);
    s->key_row = combo_row(C_("combo row title", "Key"), KEYS, true);
    on_combo(s->mouse_row, Kind::Mouse, s);
    on_combo(s->media_row, Kind::Media, s);
    on_combo(s->func_row, Kind::Function, s);
    on_combo(s->key_row, Kind::Key, s);

    s->key_box = ui::vbox(12);
    GtkWidget* rec = ui::button(_("Record a key…"), {"suggested-action", "pill"});
    ui::on(rec, "clicked", [s] { record_key(s); });
    GtkWidget* mods = ui::hbox(8);
    gtk_box_set_homogeneous(GTK_BOX(mods), TRUE);
    for (int i = 0; i < 4; i++) {
        s->mods[i] = ui::css(gtk_toggle_button_new_with_label(C_("modifier key", MOD_NAMES[i])), {"ss-chip"});
        ui::on(s->mods[i], "toggled", [s] {
            Action* a = slot_at(*s, s->sel);
            if (!s->loading && a && a->kind() == Kind::Key) set_action(s, action_from_ui(*s, Kind::Key));
        });
        ui::add(mods, {s->mods[i]});
    }
    ui::add(s->key_box, {rec, mods, single_row_list(s->key_row)});

    s->raw_row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(s->raw_row), _("Macro or custom data (kept as is)"));
    s->raw_list = single_row_list(s->raw_row);

    s->default_hint = ui::label("", {"ss-hint"});
    gtk_label_set_wrap(GTK_LABEL(s->default_hint), TRUE);

    ui::add(box, {s->mouse_list, s->key_box, s->media_list, s->func_list, s->raw_list, s->default_hint});
    return box;
}

GtkWidget* build_panel(const SP& s) {
    GtkWidget* content = ui::vbox(12);
    s->restore = ui::button(_("Restore factory action"), {"flat"});
    ui::on(s->restore, "clicked", [s] {
        Action* a = slot_at(*s, s->sel);
        if (!a) return;
        set_action(s, s->layer ? s->app.factory.shift_buttons[s->sel] : s->app.factory.buttons[s->sel]);
    });
    GtkWidget* action_title = title(_("ACTION"));
    gtk_widget_set_margin_top(action_title, 8);
    ui::add(content, {title(_("BUTTONS")), build_button_list(s), action_title, build_chips(s), build_detail(s), s->restore});

    GtkWidget* sc = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sc), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sc), content);
    gtk_widget_set_vexpand(sc, TRUE);

    GtkWidget* panel = ui::css(ui::vbox(0), {"ss-panel"});
    gtk_widget_set_size_request(panel, 400, -1);
    gtk_widget_set_hexpand(panel, FALSE);
    ui::add(panel, {sc});
    return panel;
}

}  // namespace

GtkWidget* page_buttons(App& app) {
    auto s = std::make_shared<St>(app);
    s->root = ui::hbox(16);
    ui::margins(s->root, 24);
    ui::add(s->root, {build_view(s), build_panel(s)});

    app.on_button.push_back([s](const std::string& zone, bool down) { s->mv->input_button(zone, down); });
    app.on_scroll.push_back([s](double dy) { s->mv->input_scroll(dy); });
    app.on_reload.push_back([s] { refresh(*s); });
    refresh(*s);
    return s->root;
}
