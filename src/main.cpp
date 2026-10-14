// SUPERSTRIKE Studio — Linux editor for the Logitech PRO X2/X3 SUPERSTRIKE.
// Debug: --shot PAGE:FILE.png [--demo] renders the window after 5 s to a PNG and quits.
#include <cstdlib>
#include <cstring>
#include "app_impl.hpp"
#include "i18n.hpp"
#include "ui.hpp"

static App g_app;
static std::string shot_page, shot_file;
static bool shot_demo = false;

static void take_shot() {
    g_app.show_page(shot_page.c_str());
    if (shot_demo)
        ui::every(600, [] {
            for (auto& f : g_app.on_button) f("btn-left", true);
            for (auto& f : g_app.on_scroll) f(3);
            return false;
        });
    if (shot_demo)
        ui::every(3500, [] { if (g_app.edit >= 0) g_app.mark_dirty(g_app.edit); return false; });  // UI only, no write
    if (const char* lang = g_getenv("SS_SHOT_LANG"))  // debug: switch language while running
        ui::every(2500, [lang] { g_app.set_language(lang); return false; });
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
