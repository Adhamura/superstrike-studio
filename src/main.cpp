// SuperStrike Studio — Linux and Windows editor for the Logitech PRO X2/X3 SUPERSTRIKE.
// --demo: a simulated mouse (hid_sim.hpp), nothing is written to a real one.
// Debug: --shot PAGE:FILE.png [--demo] renders the window after 5 s to a PNG and quits.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <glib/gstdio.h>
#include "app_impl.hpp"
#include "hid_sim.hpp"
#include "i18n.hpp"
#include "ui.hpp"

static App g_app;
static std::string shot_page, shot_file;
static bool shot_demo = false;

// Shows the view stack page or toggle called name anywhere in the widget tree (screenshots only).
static void select_named(GtkWidget* w, const char* name) {
    if (ADW_IS_VIEW_STACK(w) && adw_view_stack_get_child_by_name(ADW_VIEW_STACK(w), name))
        adw_view_stack_set_visible_child_name(ADW_VIEW_STACK(w), name);
    if (ADW_IS_TOGGLE_GROUP(w) && adw_toggle_group_get_toggle_by_name(ADW_TOGGLE_GROUP(w), name))
        adw_toggle_group_set_active_name(ADW_TOGGLE_GROUP(w), name);
    for (GtkWidget* c = gtk_widget_get_first_child(w); c; c = gtk_widget_get_next_sibling(c)) select_named(c, name);
}

// Writes "x y w h type text" of every visible label and drawn widget, in window coordinates
// (tools/promo/drive.py reads it to click real widgets while recording the promo video).
static void dump_widgets(GtkWidget* w, GtkWidget* win, FILE* f) {
    if (!gtk_widget_get_mapped(w)) return;
    graphene_rect_t r;
    const char* type = G_OBJECT_TYPE_NAME(w);
    bool label = GTK_IS_LABEL(w), drawn = GTK_IS_DRAWING_AREA(w);
    if ((label || drawn) && gtk_widget_compute_bounds(w, win, &r))
        std::fprintf(f, "%.0f %.0f %.0f %.0f %s %s\n", r.origin.x, r.origin.y, r.size.width, r.size.height, type,
                     label ? gtk_label_get_text(GTK_LABEL(w)) : gtk_widget_get_name(w));
    for (GtkWidget* c = gtk_widget_get_first_child(w); c; c = gtk_widget_get_next_sibling(c)) dump_widgets(c, win, f);
}

static void take_shot() {
    g_app.show_page(shot_page.c_str());
    if (shot_demo)  // a click and a scroll, so the drawing shows the live view
        ui::every(600, [] {
            for (auto& f : g_app.on_button) f("btn-left", true);
            for (auto& f : g_app.on_scroll) f(3);
            return false;
        });
    if (const char* c = g_getenv("SS_SHOT_CLEAN"); shot_demo && !(c && *c))
        ui::every(3500, [] { if (g_app.edit >= 0) g_app.mark_dirty(g_app.edit); return false; });  // UI only, no write
    if (const char* n = g_getenv("SS_SHOT_PROFILE"))  // debug: edit profile N (1-5)
        ui::every(1500, [n] {
            size_t i = (size_t)atoi(n) - 1;
            if (i < g_app.dir.size()) g_app.set_edit(g_app.dir[i].sector);
            return false;
        });
    if (const char* names = g_getenv("SS_SHOT_SELECT"))  // debug: tabs / toggles to show, e.g. "rt,both"
        ui::every(2000, [names] {
            gchar** v = g_strsplit(names, ",", -1);
            for (gchar** n = v; *n; n++) select_named(GTK_WIDGET(g_app.window), *n);
            g_strfreev(v);
            return false;
        });
    if (const char* lang = g_getenv("SS_SHOT_LANG"))  // debug: switch language while running
        ui::every(2500, [lang] { g_app.set_language(lang); return false; });
    if (const char* path = g_getenv("SS_SHOT_DUMP"))  // debug: widget positions, every 300 ms
        ui::every(300, [path] {
            std::string tmp = std::string(path) + ".tmp";
            if (FILE* f = std::fopen(tmp.c_str(), "w")) {
                dump_widgets(GTK_WIDGET(g_app.window), GTK_WIDGET(g_app.window), f);
                std::fclose(f);
                g_rename(tmp.c_str(), path);
            }
            return true;
        });
    const char* delay = g_getenv("SS_SHOT_DELAY_MS");
    ui::every(delay ? atoi(delay) : 5000, [] {
        GtkWidget* w = GTK_WIDGET(g_app.window);
        GdkPaintable* paint = gtk_widget_paintable_new(w);
        GtkSnapshot* snap = gtk_snapshot_new();
        gdk_paintable_snapshot(paint, snap, gtk_widget_get_width(w), gtk_widget_get_height(w));
        GskRenderNode* node = gtk_snapshot_free_to_node(snap);
        GdkTexture* tex = gsk_renderer_render_texture(gtk_native_get_renderer(gtk_widget_get_native(w)), node, nullptr);
        gdk_texture_save_to_png(tex, shot_file.c_str());
        g_object_unref(tex);
        gsk_render_node_unref(node);
        g_object_unref(paint);
        g_application_quit(g_application_get_default());
        return false;
    });
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--shot") && i + 1 < argc) {
            std::string a = argv[++i];
            shot_page = a.substr(0, a.find(':'));
            shot_file = a.substr(a.find(':') + 1);
        } else if (!strcmp(argv[i], "--demo")) {
            shot_demo = true;
            sim_enable();
        } else if (!strcmp(argv[i], "--version")) {
            std::printf("SuperStrike Studio %s\n", SS_VERSION);
            return 0;
        } else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            std::printf("Usage: superstrike-studio [--demo] [--version]\n"
                        "  --demo     try the app with a simulated PRO X3 SUPERSTRIKE (no mouse needed)\n"
                        "  --version  print the version\n");
            return 0;
        }
    }
    g_app.p = new App::Impl();
    g_app.cfg.load();
    i18n_init(g_app.cfg.lang);  // before GTK starts, so its own strings follow the language too
    auto flags = shot_file.empty() ? G_APPLICATION_DEFAULT_FLAGS : G_APPLICATION_NON_UNIQUE;
    AdwApplication* gapp = adw_application_new("io.github.superstrike.Studio", flags);
    ui::on(gapp, "activate", [gapp] {
        if (g_app.window) return gtk_window_present(g_app.window);
        adw_style_manager_set_color_scheme(adw_style_manager_get_default(), ADW_COLOR_SCHEME_PREFER_DARK);
        if (i18n_rtl()) gtk_widget_set_default_direction(GTK_TEXT_DIR_RTL);
        window_build(g_app, GTK_APPLICATION(gapp));
        app_start(g_app);
        if (!shot_file.empty()) take_shot();
    });
    char* args[] = {argv[0], nullptr};
    int rc = g_application_run(G_APPLICATION(gapp), 1, args);
    app_stop(g_app);
    g_object_unref(gapp);
    return rc;
}
