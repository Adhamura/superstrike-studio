// HITS page: actuation, rapid trigger and haptics per button, presets, HITS codes, test pad.
#include <algorithm>
#include <cstdio>
#include <deque>
#include <memory>
#include "app.hpp"
#include "i18n.hpp"
#include "ui.hpp"
#include "widgets.hpp"

namespace {

constexpr int TARGET_LEFT = 0, TARGET_RIGHT = 1, TARGET_BOTH = 2;
const char* const TARGET_NAMES[3] = {"left", "right", "both"};
const char* const ZONES[2] = {"btn-left", "btn-right"};
constexpr int PAD_BUTTONS[5] = {1, 3, 2, 8, 9};  // GDK button numbers: L, R, M, Back, Forward

struct St {
    explicit St(App& a) : app(a) {}
    App& app;
    bool loading = false;
    int target = TARGET_LEFT;

    GtkWidget *panel = nullptr, *root = nullptr, *warn = nullptr, *rt_switch = nullptr;
    GtkWidget *act_val = nullptr, *act_unit = nullptr, *rt_val = nullptr, *rt_unit = nullptr;
    GtkWidget *hap_val = nullptr, *hap_unit = nullptr, *your_list = nullptr, *pro_list = nullptr;
    GtkWidget *code_pop = nullptr, *pad_counts = nullptr;
    AdwToggleGroup* tgt = nullptr;
    MouseView* mv = nullptr;
    TravelBar *bar_act = nullptr, *bar_rt = nullptr;
    LevelBar *lv_rt = nullptr, *lv_hap = nullptr;

    int counts[5] = {};
    int held_ms = 0;
    std::deque<gint64> clicks;
    gint64 down_at[16] = {};
};
using SP = std::shared_ptr<St>;

std::string trim(const std::string& t) {
    size_t a = t.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    return t.substr(a, t.find_last_not_of(" \t\r\n") - a + 1);
}

// Short value shown in the big number and in the callout boxes: the level, or "off".
std::string rt_text(const Hits& h) { return h.rt_on ? std::to_string(h.rt) : C_("level value", "off"); }

std::string summary(const std::array<Hits, 2>& a) {
    auto pair = [&](auto get) {
        std::string l = get(a[0]), r = get(a[1]);
        return l == r ? l : l + " / " + r;
    };
    // TRANSLATORS: subtitle of a preset. Each %s is a level (1 to 10, 1 to 5 or 0 to 5), or "L / R" when the two buttons differ
    return fmt(_("Actuation %s   Rapid trigger %s   Haptics %s"), pair([](const Hits& h) { return std::to_string(h.act); }).c_str(),
               pair(rt_text).c_str(), pair([](const Hits& h) { return std::to_string(h.hap); }).c_str());
}

void refresh(St& s);
void fill_presets(const std::shared_ptr<St>& s);

void commit(const SP& s, const std::array<Hits, 2>& v) {
    Profile* p = s->app.cur();
    if (!p) return;
    std::vector<int> changed;
    for (int b = 0; b < 2; b++)
        if (p->hits[b] != v[b]) { p->hits[b] = v[b]; changed.push_back(b); }
    if (changed.empty()) return;
    s->app.mark_dirty(s->app.edit);
    for (int b : changed) s->app.live_hits(b);
    refresh(*s);
}

template <class F> void edit(const SP& s, F fn) {
    Profile* p = s->app.cur();
    if (s->loading || !p) return;
    Hits h = p->hits[s->target == TARGET_RIGHT ? 1 : 0];
    fn(h);
    h.act = std::clamp(h.act, 1, 10);
    h.rt = std::clamp(h.rt, 1, 5);
    h.hap = std::clamp(h.hap, 0, 5);
    std::array<Hits, 2> v = p->hits;
    if (s->target != TARGET_RIGHT) v[0] = h;
    if (s->target != TARGET_LEFT) v[1] = h;
    commit(s, v);
}

void apply_preset(const SP& s, const std::string& name, const std::array<Hits, 2>& v) {
    if (!s->app.cur()) return;
    commit(s, v);
    // TRANSLATORS: %s is the name of a HITS preset
    s->app.toast(fmt(_("Preset \"%s\" applied. Press Save to mouse to keep it."), name.c_str()));
}

std::string zone_label(St& s, const std::string& zone) {
    Profile* p = s.app.cur();
    if (!p) return "";
    const Hits& h = p->hits[zone == ZONES[1] ? 1 : 0];
    return std::string("\x01 ") + std::to_string(h.act) + "\n" + std::string("\x02 ") + rt_text(h) + "\n" +
           std::string("\x03 ") + std::to_string(h.hap);
}

void refresh(St& s) {
    s.loading = true;
    Profile* p = s.app.cur();
    gtk_widget_set_sensitive(s.panel, p != nullptr);
    if (p) {
        const Hits h = p->hits[s.target == TARGET_RIGHT ? 1 : 0];
        adw_toggle_group_set_active_name(s.tgt, TARGET_NAMES[s.target]);
        gtk_widget_set_visible(s.warn, s.target == TARGET_BOTH && p->hits[0] != p->hits[1]);
        gtk_label_set_text(GTK_LABEL(s.act_val), std::to_string(h.act).c_str());
        // TRANSLATORS: %.1f is the travel in millimetres
        gtk_label_set_text(GTK_LABEL(s.act_unit), fmt(_("≈ %.1f mm"), h.act * 0.1).c_str());
        gtk_label_set_text(GTK_LABEL(s.rt_val), rt_text(h).c_str());
        gtk_label_set_text(GTK_LABEL(s.rt_unit), h.rt_on ? _("of 5") : "");
        gtk_label_set_text(GTK_LABEL(s.hap_val), h.hap ? std::to_string(h.hap).c_str() : C_("level value", "off"));
        gtk_label_set_text(GTK_LABEL(s.hap_unit), h.hap ? _("of 5") : "");
        adw_switch_row_set_active(ADW_SWITCH_ROW(s.rt_switch), h.rt_on);
        gtk_widget_set_sensitive(s.lv_rt->widget(), h.rt_on);
        s.bar_act->set(h.act, h.rt, false);
        s.bar_rt->set(h.act, h.rt, h.rt_on);
        s.lv_rt->set(h.rt);
        s.lv_hap->set(h.hap);
    }
    s.mv->selected = s.target == TARGET_BOTH ? "" : ZONES[s.target];
    s.mv->redraw();
    s.loading = false;
}

GtkWidget* hint(const char* text) {
    GtkWidget* l = ui::label(text, {"ss-hint"});
    gtk_label_set_wrap(GTK_LABEL(l), TRUE);
    return l;
}

GtkWidget* value_row(GtkWidget*& val, GtkWidget*& unit, GtkWidget* extra = nullptr) {
    GtkWidget* row = ui::hbox(8);
    val = ui::label("", {"ss-value"});
    unit = ui::label("", {"ss-unit"});
    gtk_widget_set_valign(val, GTK_ALIGN_BASELINE_FILL);
    gtk_widget_set_valign(unit, GTK_ALIGN_BASELINE_FILL);
    ui::add(row, {val, unit, ui::spacer()});
    if (extra) {
        gtk_widget_set_valign(extra, GTK_ALIGN_CENTER);
        ui::add(row, {extra});
    }
    return row;
}

GtkWidget* section_title(const char* text) { return ui::label(text, {"ss-title"}); }

void entry_dialog(const SP& s, const char* heading, const char* body, const char* placeholder,
                  const char* ok_label, std::function<void(const std::string&)> done) {
    AdwDialog* d = adw_alert_dialog_new(heading, body);
    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), placeholder);
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    adw_alert_dialog_set_extra_child(ADW_ALERT_DIALOG(d), entry);
    adw_alert_dialog_add_response(ADW_ALERT_DIALOG(d), "cancel", _("Cancel"));
    adw_alert_dialog_add_response(ADW_ALERT_DIALOG(d), "ok", ok_label);
    adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(d), "ok", ADW_RESPONSE_SUGGESTED);
    adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(d), "ok");
    adw_alert_dialog_set_close_response(ADW_ALERT_DIALOG(d), "cancel");
    ui::on1<const char*>(d, "response", [entry, done](const char* r) {
        if (std::string(r) == "ok") done(gtk_editable_get_text(GTK_EDITABLE(entry)));
    });
    adw_dialog_set_focus(d, entry);
    adw_dialog_present(d, s->root);
}

void copy_code(const SP& s) {
    Profile* p = s->app.cur();
    if (!p) return;
    std::string code = hits_code_encode(p->hits);
    gdk_clipboard_set_text(gtk_widget_get_clipboard(s->root), code.c_str());
    // TRANSLATORS: %s is the HITS code (letters and digits)
    s->app.toast(fmt(_("HITS code copied: %s"), code.c_str()));
}

void import_code(const SP& s) {
    if (!s->app.cur()) return;
    entry_dialog(s, _("Import HITS code"), _("Paste a HITS code from G HUB, a pro player or a friend."),
                 "MDAwNjA2MDEwMTExMDUwMw==", _("Apply"), [s](const std::string& text) {
                     std::array<Hits, 2> v{};
                     std::string err;
                     std::string code = trim(text);
                     if (!hits_code_decode(code, v, &err)) {
                         s->app.toast(err.empty() ? std::string(_("Not a valid HITS code"))
                                                  // TRANSLATORS: %s is the reason, for example "not a HITS code"
                                                  : fmt(_("Not a valid HITS code: %s"), err.c_str()));
                         return;
                     }
                     apply_preset(s, _("HITS code"), v);
                 });
}

void save_preset(const SP& s) {
    if (!s->app.cur()) return;
    entry_dialog(s, _("Save HITS preset"), _("Name for the current left and right button settings."), _("My preset"), _("Save"),
                 [s](const std::string& text) {
                     Profile* p = s->app.cur();
                     std::string name = trim(text);
                     if (!p || name.empty()) return;
                     std::string code = hits_code_encode(p->hits);
                     auto& list = s->app.cfg.presets;
                     auto it = std::find_if(list.begin(), list.end(), [&](auto& e) { return e.first == name; });
                     if (it != list.end()) it->second = code;
                     else list.emplace_back(name, code);
                     s->app.cfg.save();
                     fill_presets(s);
                     // TRANSLATORS: %s is the name of a HITS preset
                     s->app.toast(fmt(_("Preset \"%s\" saved"), name.c_str()));
                 });
}

GtkWidget* code_menu(const SP& s) {
    GtkWidget* mb = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(mb), "ss-code-symbolic");
    gtk_widget_set_tooltip_text(mb, _("HITS code"));
    ui::css(mb, {"flat"});
    GtkWidget* pop = gtk_popover_new();
    GtkWidget* box = ui::vbox(2);
    GtkWidget* copy = ui::button(_("Copy HITS code"), {"flat"});
    GtkWidget* imp = ui::button(_("Import HITS code…"), {"flat"});
    ui::add(box, {copy, imp});
    gtk_popover_set_child(GTK_POPOVER(pop), box);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(mb), pop);
    s->code_pop = pop;
    ui::on(copy, "clicked", [s] { gtk_popover_popdown(GTK_POPOVER(s->code_pop)); copy_code(s); });
    ui::on(imp, "clicked", [s] { gtk_popover_popdown(GTK_POPOVER(s->code_pop)); import_code(s); });
    return mb;
}

void clear_list(GtkWidget* list) {
    while (GtkWidget* c = gtk_widget_get_first_child(list)) gtk_list_box_remove(GTK_LIST_BOX(list), c);
}

GtkWidget* preset_list() {
    GtkWidget* l = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(l), GTK_SELECTION_NONE);
    return ui::css(l, {"boxed-list"});
}

void add_preset_row(const SP& s, GtkWidget* list, const std::string& name, const std::string& code, int user_idx) {
    std::array<Hits, 2> v{};
    if (!hits_code_decode(code, v)) return;
    GtkWidget* row = adw_action_row_new();
    adw_preferences_row_set_use_markup(ADW_PREFERENCES_ROW(row), FALSE);
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), name.c_str());
    adw_action_row_set_subtitle(ADW_ACTION_ROW(row), summary(v).c_str());
    gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), TRUE);
    // TRANSLATORS: tooltip of a preset, %s is the HITS code (letters and digits)
    gtk_widget_set_tooltip_text(row, fmt(_("HITS code %s"), code.c_str()).c_str());
    ui::on(row, "activated", [s, name, v] { apply_preset(s, name, v); });
    if (user_idx >= 0) {
        GtkWidget* del = ui::icon_button("user-trash-symbolic", _("Delete preset"));
        gtk_widget_set_valign(del, GTK_ALIGN_CENTER);
        adw_action_row_add_suffix(ADW_ACTION_ROW(row), del);
        ui::on(del, "clicked", [s, user_idx] {
            ui::idle([s, user_idx] {
                auto& list = s->app.cfg.presets;
                if (user_idx >= (int)list.size()) return;
                list.erase(list.begin() + user_idx);
                s->app.cfg.save();
                fill_presets(s);
            });
        });
    }
    gtk_list_box_append(GTK_LIST_BOX(list), row);
}

void fill_presets(const SP& s) {
    clear_list(s->your_list);
    clear_list(s->pro_list);
    for (auto& b : BUILTIN_PRESETS) add_preset_row(s, s->your_list, C_("HITS preset", b.name), b.code, -1);
    auto& user = s->app.cfg.presets;
    for (size_t i = 0; i < user.size(); i++) add_preset_row(s, s->your_list, user[i].first, user[i].second, (int)i);
    for (auto& pr : PRO_PRESETS) add_preset_row(s, s->pro_list, pr.name, pr.code, -1);
}

GtkWidget* tab_actuation(const SP& s) {
    GtkWidget* box = ui::vbox(12);
    GtkWidget* reset = ui::icon_button("ss-rotate-symbolic", _("Reset to 5"));
    ui::on(reset, "clicked", [s] { edit(s, [](Hits& h) { h.act = 5; }); });
    s->bar_act = TravelBar::create();
    s->bar_act->on_change = [s](int act, int) { edit(s, [act](Hits& h) { h.act = act; }); };
    gtk_widget_set_hexpand(s->bar_act->widget(), TRUE);
    ui::add(box, {hint(_("Where along the press the click fires. Short = faster, long = fewer misclicks.")),
                  value_row(s->act_val, s->act_unit, reset), s->bar_act->widget()});
    return box;
}

GtkWidget* tab_rapid(const SP& s) {
    GtkWidget* box = ui::vbox(12);
    GtkWidget* list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
    ui::css(list, {"boxed-list"});
    s->rt_switch = adw_switch_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(s->rt_switch), _("Rapid trigger"));
    gtk_list_box_append(GTK_LIST_BOX(list), s->rt_switch);
    ui::notify(s->rt_switch, "active", [s] {
        bool on = adw_switch_row_get_active(ADW_SWITCH_ROW(s->rt_switch));
        edit(s, [on](Hits& h) { h.rt_on = on; });
    });
    s->lv_rt = LevelBar::create(1, 5, C_("rapid trigger level", "Most sensitive"), C_("rapid trigger level", "Least"));
    s->lv_rt->on_change = [s](int v) { edit(s, [v](Hits& h) { h.rt = v; }); };
    s->bar_rt = TravelBar::create();
    s->bar_rt->on_change = [s](int act, int rt) {
        edit(s, [act, rt](Hits& h) { h.act = act; if (h.rt_on) h.rt = rt; });
    };
    gtk_widget_set_hexpand(s->bar_rt->widget(), TRUE);
    ui::add(box, {list, value_row(s->rt_val, s->rt_unit), s->lv_rt->widget(), s->bar_rt->widget(),
                  hint(_("After a click, lifting the button by this much re-arms it, so fast repeated clicks need "
                         "less travel."))});
    return box;
}

GtkWidget* tab_haptics(const SP& s) {
    GtkWidget* box = ui::vbox(12);
    s->lv_hap = LevelBar::create(0, 5, C_("haptics level", "Off"), C_("haptics level", "Strongest"));
    s->lv_hap->on_change = [s](int v) { edit(s, [v](Hits& h) { h.hap = v; }); };
    ui::add(box, {value_row(s->hap_val, s->hap_unit), s->lv_hap->widget(),
                  hint(_("Click feel strength. Higher levels use more battery."))});
    return box;
}

GtkWidget* tab_presets(const SP& s) {
    GtkWidget* box = ui::vbox(12);
    s->your_list = preset_list();
    s->pro_list = preset_list();
    GtkWidget* save = ui::button(_("Save current as preset…"));
    ui::on(save, "clicked", [s] { save_preset(s); });
    GtkWidget* pro_title = section_title(_("PRO PRESETS"));
    gtk_widget_set_margin_top(pro_title, 8);
    ui::add(box, {section_title(_("YOUR PRESETS")), s->your_list, save, pro_title, s->pro_list});
    GtkWidget* sc = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sc), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sc), ui::margins(box, 0, 0, 0, 0));
    gtk_widget_set_vexpand(sc, TRUE);
    return sc;
}

GtkWidget* build_panel(const SP& s) {
    GtkWidget* panel = ui::css(ui::vbox(12), {"ss-panel"});
    gtk_widget_set_size_request(panel, 380, -1);
    gtk_widget_set_hexpand(panel, FALSE);
    s->panel = panel;

    GtkWidget* head = ui::hbox(8);
    ui::add(head, {section_title(_("HITS CONFIGURATION")), ui::spacer(), code_menu(s)});

    s->tgt = ui::toggles({{"left", C_("HITS target", "Left")}, {"right", C_("HITS target", "Right")}, {"both", _("Both (L+R)")}});
    adw_toggle_group_set_homogeneous(s->tgt, TRUE);
    gtk_widget_set_hexpand(GTK_WIDGET(s->tgt), TRUE);
    ui::notify(s->tgt, "active-name", [s] {
        if (s->loading) return;
        const char* n = adw_toggle_group_get_active_name(s->tgt);
        if (!n) return;
        s->target = n[0] == 'r' ? TARGET_RIGHT : n[0] == 'b' ? TARGET_BOTH : TARGET_LEFT;
        refresh(*s);
    });

    s->warn = ui::css(ui::label(_("L and R differ — changes apply to both")), {"ss-badge", "warn"});
    gtk_label_set_wrap(GTK_LABEL(s->warn), TRUE);
    gtk_widget_set_halign(s->warn, GTK_ALIGN_START);
    gtk_widget_set_visible(s->warn, FALSE);

    AdwViewStack* stack = ADW_VIEW_STACK(adw_view_stack_new());
    adw_view_stack_set_enable_transitions(stack, TRUE);
    adw_view_stack_set_transition_duration(stack, 200);
    adw_view_stack_add_titled_with_icon(stack, tab_actuation(s), "act", C_("HITS tab", "Actuation"), "ss-actuation-symbolic");
    adw_view_stack_add_titled_with_icon(stack, tab_rapid(s), "rt", C_("HITS tab", "Rapid"), "ss-rapid-symbolic");
    adw_view_stack_add_titled_with_icon(stack, tab_haptics(s), "hap", C_("HITS tab", "Haptics"), "ss-haptics-symbolic");
    adw_view_stack_add_titled_with_icon(stack, tab_presets(s), "presets", C_("HITS tab", "Presets"), "view-list-symbolic");
    gtk_widget_set_vexpand(GTK_WIDGET(stack), TRUE);
    GtkWidget* sw = adw_inline_view_switcher_new();
    adw_inline_view_switcher_set_stack(ADW_INLINE_VIEW_SWITCHER(sw), stack);
    adw_inline_view_switcher_set_display_mode(ADW_INLINE_VIEW_SWITCHER(sw), ADW_INLINE_VIEW_SWITCHER_LABELS);
    adw_inline_view_switcher_set_homogeneous(ADW_INLINE_VIEW_SWITCHER(sw), TRUE);

    ui::add(panel, {head, GTK_WIDGET(s->tgt), s->warn, sw, GTK_WIDGET(stack)});
    return panel;
}

void update_pad(St& s) {
    if (!s.pad_counts) return;
    gint64 now = g_get_monotonic_time();
    while (!s.clicks.empty() && now - s.clicks.front() > 1000000) s.clicks.pop_front();
    // TRANSLATORS: live counters under the test pad. Counts of the left, right, middle, back and forward button
    // (L, R, M, Back, Fwd), then clicks per second and how long the last button was held in milliseconds. Keep it short.
    std::string t = fmt(_("L %d · R %d · M %d · Back %d · Fwd %d · %d clicks/s · held %d ms"), s.counts[0], s.counts[1],
                        s.counts[2], s.counts[3], s.counts[4], (int)s.clicks.size(), s.held_ms);
    if (t != gtk_label_get_text(GTK_LABEL(s.pad_counts))) gtk_label_set_text(GTK_LABEL(s.pad_counts), t.c_str());
}

GtkWidget* build_pad(const SP& s) {
    GtkWidget* pad = ui::css(ui::vbox(4), {"ss-pad"});
    gtk_widget_set_size_request(pad, -1, 88);
    gtk_widget_set_vexpand(pad, FALSE);
    GtkWidget* title = ui::label(_("Test here — click with any button"), {}, 0.5);
    s->pad_counts = ui::label("", {"ss-hint"}, 0.5);
    g_object_add_weak_pointer(G_OBJECT(s->pad_counts), reinterpret_cast<gpointer*>(&s->pad_counts));
    gtk_widget_set_vexpand(title, TRUE);
    gtk_widget_set_valign(title, GTK_ALIGN_END);
    gtk_widget_set_vexpand(s->pad_counts, TRUE);
    gtk_widget_set_valign(s->pad_counts, GTK_ALIGN_START);
    ui::add(pad, {title, s->pad_counts});

    GtkGesture* g = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(g), 0);
    ui::on3<int, double, double>(g, "pressed", [s, g](int, double, double) {
        guint b = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(g));
        gint64 now = g_get_monotonic_time();
        if (b < 16) s->down_at[b] = now;
        for (int i = 0; i < 5; i++)
            if ((guint)PAD_BUTTONS[i] == b) s->counts[i]++;
        s->clicks.push_back(now);
        update_pad(*s);
    });
    ui::on3<int, double, double>(g, "released", [s, g](int, double, double) {
        guint b = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(g));
        if (b < 16 && s->down_at[b]) s->held_ms = (int)((g_get_monotonic_time() - s->down_at[b]) / 1000);
        update_pad(*s);
    });
    gtk_widget_add_controller(pad, GTK_EVENT_CONTROLLER(g));
    ui::every(250, [s] {
        if (!s->pad_counts) return false;
        update_pad(*s);
        return true;
    });
    update_pad(*s);
    return pad;
}

GtkWidget* build_stage(const SP& s) {
    GtkWidget* stage = ui::vbox(16);
    gtk_widget_set_hexpand(stage, TRUE);
    s->mv = MouseView::create();
    s->mv->zones = {ZONES[0], ZONES[1]};
    s->mv->callouts = MouseView::Callouts::Boxes;
    s->mv->grid = true;
    s->mv->thumbnail = true;
    s->mv->label_for = [s](const std::string& zone) { return zone_label(*s, zone); };
    s->mv->on_zone = [s](const std::string& zone) {
        s->target = zone == ZONES[1] ? TARGET_RIGHT : TARGET_LEFT;
        refresh(*s);
    };
    s->mv->set_view("top", false);
    s->mv->redraw();
    gtk_widget_set_hexpand(s->mv->widget(), TRUE);
    gtk_widget_set_vexpand(s->mv->widget(), TRUE);
    ui::add(stage, {s->mv->widget(), build_pad(s)});
    return stage;
}

}  // namespace

GtkWidget* page_hits(App& app) {
    auto s = std::make_shared<St>(app);
    s->root = ui::hbox(16);
    ui::margins(s->root, 24);
    ui::add(s->root, {build_panel(s), build_stage(s)});

    app.on_button.push_back([s](const std::string& zone, bool down) { s->mv->input_button(zone, down); });
    app.on_scroll.push_back([s](double dy) { s->mv->input_scroll(dy); });
    app.on_reload.push_back([s] {
        refresh(*s);
        fill_presets(s);
    });
    refresh(*s);
    fill_presets(s);
    return s->root;
}
