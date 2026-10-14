// Small helpers that keep GTK/libadwaita C code short in C++ (header only).
#pragma once
#include <adwaita.h>
#include <functional>
#include <initializer_list>
#include <string>
#include <utility>

namespace ui {

template <class F> static void destroy_fn(gpointer d, GClosure*) { delete static_cast<F*>(d); }

// NOTE: on/on1/on3 are only for signals that return void. For gboolean signals ("key-pressed",
// "close-request", ...) use g_signal_connect with a static function that returns TRUE/FALSE.

// Signal with no extra arguments: void (Instance*, gpointer).  F: void()
template <class F> gulong on(gpointer obj, const char* sig, F f) {
    return g_signal_connect_data(obj, sig, G_CALLBACK(+[](gpointer, gpointer d) { (*static_cast<F*>(d))(); }),
                                 new F(std::move(f)), destroy_fn<F>, GConnectFlags(0));
}
// Signal with one argument A.  F: void(A)
template <class A, class F> gulong on1(gpointer obj, const char* sig, F f) {
    return g_signal_connect_data(obj, sig, G_CALLBACK(+[](gpointer, A a, gpointer d) { (*static_cast<F*>(d))(a); }),
                                 new F(std::move(f)), destroy_fn<F>, GConnectFlags(0));
}
// Signal with three arguments (e.g. GtkGestureClick "pressed": n, x, y).  F: void(A, B, C)
template <class A, class B, class C, class F> gulong on3(gpointer obj, const char* sig, F f) {
    return g_signal_connect_data(
        obj, sig, G_CALLBACK(+[](gpointer, A a, B b, C c, gpointer d) { (*static_cast<F*>(d))(a, b, c); }),
        new F(std::move(f)), destroy_fn<F>, GConnectFlags(0));
}
// Property change: "notify::<prop>".  F: void()
template <class F> gulong notify(gpointer obj, const char* prop, F f) {
    return on1<GParamSpec*>(obj, (std::string("notify::") + prop).c_str(), [f](GParamSpec*) mutable { f(); });
}
// Run on the GTK main loop (thread safe).  F: void()
template <class F> void idle(F f) {
    g_idle_add_full(G_PRIORITY_DEFAULT, +[](gpointer d) -> gboolean { (*static_cast<F*>(d))(); return G_SOURCE_REMOVE; },
                    new F(std::move(f)), +[](gpointer d) { delete static_cast<F*>(d); });
}
// Repeating timer.  F: bool() — return false to stop.
template <class F> guint every(guint ms, F f) {
    return g_timeout_add_full(G_PRIORITY_DEFAULT, ms, +[](gpointer d) -> gboolean { return (*static_cast<F*>(d))(); },
                              new F(std::move(f)), +[](gpointer d) { delete static_cast<F*>(d); });
}

inline GtkWidget* css(GtkWidget* w, std::initializer_list<const char*> classes) {
    for (auto c : classes) gtk_widget_add_css_class(w, c);
    return w;
}
inline GtkWidget* margins(GtkWidget* w, int t, int r, int b, int l) {
    gtk_widget_set_margin_top(w, t); gtk_widget_set_margin_end(w, r);
    gtk_widget_set_margin_bottom(w, b); gtk_widget_set_margin_start(w, l);
    return w;
}
inline GtkWidget* margins(GtkWidget* w, int m) { return margins(w, m, m, m, m); }
inline GtkWidget* vbox(int spacing) { return gtk_box_new(GTK_ORIENTATION_VERTICAL, spacing); }
inline GtkWidget* hbox(int spacing) { return gtk_box_new(GTK_ORIENTATION_HORIZONTAL, spacing); }
inline void add(GtkWidget* box, std::initializer_list<GtkWidget*> kids) {
    for (auto k : kids) gtk_box_append(GTK_BOX(box), k);
}
inline GtkWidget* label(const std::string& text, std::initializer_list<const char*> classes = {}, float xalign = 0) {
    GtkWidget* l = gtk_label_new(text.c_str());
    gtk_label_set_xalign(GTK_LABEL(l), xalign);
    return css(l, classes);
}
inline GtkWidget* spacer() { GtkWidget* b = hbox(0); gtk_widget_set_hexpand(b, TRUE); return b; }
inline GtkWidget* icon(const char* name, int px = 16) {
    GtkWidget* i = gtk_image_new_from_icon_name(name);
    gtk_image_set_pixel_size(GTK_IMAGE(i), px);
    return i;
}
inline GtkWidget* button(const char* text, std::initializer_list<const char*> classes = {}) {
    return css(gtk_button_new_with_label(text), classes);
}
inline GtkWidget* icon_button(const char* icon_name, const char* tooltip) {
    GtkWidget* b = gtk_button_new_from_icon_name(icon_name);
    gtk_widget_set_tooltip_text(b, tooltip);
    return css(b, {"flat"});
}
// Adw.ToggleGroup from {name, label} pairs.
inline AdwToggleGroup* toggles(std::initializer_list<std::pair<const char*, const char*>> items) {
    auto* g = ADW_TOGGLE_GROUP(adw_toggle_group_new());
    for (auto& [name, text] : items) {
        AdwToggle* t = adw_toggle_new();
        adw_toggle_set_name(t, name);
        adw_toggle_set_label(t, text);
        adw_toggle_group_add(g, t);
    }
    return g;
}
// Scrolled, centred column with a maximum width (the standard page body).
inline GtkWidget* page_column(GtkWidget* child, int max_width = 980) {
    GtkWidget* clamp = adw_clamp_new();
    adw_clamp_set_maximum_size(ADW_CLAMP(clamp), max_width);
    adw_clamp_set_child(ADW_CLAMP(clamp), margins(child, 24, 24, 32, 24));
    GtkWidget* sc = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sc), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sc), clamp);
    gtk_widget_set_vexpand(sc, TRUE);
    return sc;
}

}  // namespace ui
