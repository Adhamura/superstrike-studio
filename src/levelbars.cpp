// TravelBar (press-travel bar) and LevelBar (pill level picker), plus asset_path().
#include <algorithm>
#include <cmath>
#include "i18n.hpp"
#include "mouseview_fx.hpp"
#include "platform.hpp"
#include "ui.hpp"
#include "widgets.hpp"

#ifndef SS_ASSET_DIR
#define SS_ASSET_DIR "assets"
#endif

using fx::color;
using fx::rgb;
using fx::text;

namespace {
const char* const ACCENT = "#3b9cff";
constexpr double ORANGE[3] = {1.0, 0.62, 0.2};

void free_with(GtkWidget* w, const char* key, gpointer obj, GDestroyNotify fn) { g_object_set_data_full(G_OBJECT(w), key, obj, fn); }
void pointer_cursor(GtkWidget* w) { gtk_widget_set_cursor_from_name(w, "pointer"); }
}  // namespace

std::string asset_path(const std::string& name) {
    std::vector<std::string> dirs;
    if (const char* e = g_getenv("SUPERSTRIKE_ASSETS")) dirs.push_back(e);
#if defined(_WIN32) || defined(__APPLE__)
    std::string exe = exe_dir();
    if (!exe.empty()) {
        dirs.push_back(exe + "/../Resources/assets");  // macOS: SuperStrike Studio.app/Contents/MacOS/..
        dirs.push_back(exe + "/assets");
        dirs.push_back(exe + "/../share/superstrike-studio/assets");
    }
#else
    // AppImage (tools/linux/build-appimage.sh): the runtime sets APPDIR, sharun SHARUN_DIR
    for (const char* var : {"APPDIR", "SHARUN_DIR"})
        if (const char* e = g_getenv(var)) dirs.push_back(std::string(e) + "/share/superstrike-studio/assets");
    if (gchar* exe = g_file_read_link("/proc/self/exe", nullptr)) {
        gchar* d = g_path_get_dirname(exe);
        dirs.push_back(std::string(d) + "/assets");
        dirs.push_back(std::string(d) + "/../share/superstrike-studio/assets");
        g_free(d);
        g_free(exe);
    }
#endif
    dirs.push_back(SS_ASSET_DIR);
    for (auto& d : dirs)
        if (g_file_test((d + "/" + name).c_str(), G_FILE_TEST_EXISTS)) return d + "/" + name;
    return dirs.back() + "/" + name;
}

// ================================================================================================
struct TravelBar::Impl {
    static constexpr int SEG = 10;
    static constexpr double BAR_T = 60, BAR_B = 82, RT_T = 92, RT_B = 106;
    TravelBar* tb;
    GtkWidget* area;
    int act = 5, rt = 2, hover = 0;  // hover: 1..SEG, 0 none
    bool rt_on = false;
    double depth = -1, start_x = 0;
    enum { NONE, ACT, RT } drag = NONE;

    double x0() const { return 10; }
    double x1() const { return gtk_widget_get_width(area) - 10.0; }
    double seg_w() const { return (x1() - x0()) / SEG; }
    int seg_at(double x) const { return std::clamp((int)std::floor((x - x0()) / seg_w()) + 1, 1, SEG); }
    double act_x() const { return x0() + (act - 0.5) * seg_w(); }
    double rt_len(int r) const { return r * 0.6 * seg_w(); }

    void apply(double x) {
        if (drag == ACT) {
            int a = seg_at(x);
            if (a != act) act = a, fire();
        } else if (drag == RT) {
            int r = std::clamp((int)std::lround((act_x() - x) / (0.6 * seg_w())), 1, 5);
            if (r != rt) rt = r, fire();
        }
        gtk_widget_queue_draw(area);
    }
    void fire() { if (tb->on_change) tb->on_change(act, rt); }
    void set_hover(int s) {
        if (s != hover) hover = s, gtk_widget_queue_draw(area);
    }

    void draw(cairo_t* cr, int W, int) {
        fx::Rgb ac = rgb(ACCENT), dim{0.75, 0.76, 0.8};
        color(cr, dim);
        text(cr, C_("travel bar", "Short click"), "Sans Bold 8.5", x0(), 0);
        text(cr, C_("travel bar", "Long click"), "Sans Bold 8.5", x1(), 0, 1);
        double sw = seg_w();
        for (int i = 1; i <= SEG; i++) {  // segments
            double a = i < act ? 0.10 : i == act ? 1.0 : 0.38;
            if (hover == i && i != act) a = 0.65;
            if (i < act && hover != i) cairo_set_source_rgba(cr, 1, 1, 1, a);
            else color(cr, ac, a);
            fx::round_rect(cr, x0() + (i - 1) * sw + 1.5, BAR_T, sw - 3, BAR_B - BAR_T, 3);
            cairo_fill(cr);
        }
        double ax = act_x();  // handle
        cairo_set_source_rgb(cr, 1, 1, 1);
        fx::dot(cr, ax, BAR_T - 11, 7);
        cairo_fill(cr);
        color(cr, ac);
        fx::dot(cr, ax, BAR_T - 11, 4);
        cairo_fill(cr);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_move_to(cr, ax - 5, BAR_T - 5);
        cairo_line_to(cr, ax + 5, BAR_T - 5);
        cairo_line_to(cr, ax, BAR_T);
        cairo_close_path(cr);
        cairo_fill(cr);
        // TRANSLATORS: label above the handle of the press-travel bar. %d is the click point (1 to 10),
        // %.1f is the travel in millimetres. Keep it short, it must fit above the bar.
        std::string buf = fmt(_("clicks at %d (≈%.1f mm)"), act, act / 10.0);  // kept inside the bar, clear of the captions
        color(cr, ac);
        int tw = text(cr, buf, "Sans 8.5", 0, 0, 0, false).w;
        text(cr, buf, "Sans 8.5", std::clamp(ax, x0() + tw / 2.0, std::max(x0() + tw / 2.0, x1() - tw / 2.0)), 20, 0.5);
        if (rt_on) {  // rapid trigger reset band
            double ln = rt_len(rt), rx = ax - ln;
            cairo_set_source_rgba(cr, ORANGE[0], ORANGE[1], ORANGE[2], 0.85);
            fx::round_rect(cr, rx, RT_T, ln, RT_B - RT_T, 4);
            cairo_fill(cr);
            cairo_set_source_rgb(cr, 1, 1, 1);
            fx::dot(cr, rx, (RT_T + RT_B) / 2, 6);
            cairo_fill(cr);
            cairo_set_source_rgb(cr, ORANGE[0], ORANGE[1], ORANGE[2]);
            fx::dot(cr, rx, (RT_T + RT_B) / 2, 3.5);
            cairo_fill(cr);
            // TRANSLATORS: caption under the orange reset band. %d is the rapid trigger level (1 to 5). Keep it short.
            buf = fmt(_("rapid trigger: re-clicks after lifting %d/5 — drag ◀▶"), rt);
            cairo_set_source_rgb(cr, 1.0, 0.75, 0.45);
            text(cr, buf, "Sans 8", x0(), RT_B + 6);
        } else {
            cairo_set_source_rgb(cr, 0.6, 0.61, 0.66);
            // TRANSLATORS: caption under the travel bar when rapid trigger is off. Keep it short.
            text(cr, _("rapid trigger off: re-clicks only after release past the click point"), "Sans 8", x0(), RT_T + 2);
        }
        if (depth >= 0) {  // live press depth
            double dx = x0() + (x1() - x0()) * std::min(1.0, depth);
            cairo_set_source_rgba(cr, 1, 1, 1, 0.95);
            cairo_set_line_width(cr, 3);
            cairo_move_to(cr, dx, BAR_T - 3);
            cairo_line_to(cr, dx, BAR_B + 3);
            cairo_stroke(cr);
        }
        (void)W;
    }
};

TravelBar* TravelBar::create() {
    auto* tb = new TravelBar;
    auto* p = tb->p = new Impl;
    p->tb = tb;
    GtkWidget* a = p->area = gtk_drawing_area_new();
    gtk_widget_set_name(a, "travel-bar");
    gtk_widget_set_size_request(a, 300, 130);
    gtk_widget_set_hexpand(a, TRUE);
    pointer_cursor(a);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(a), +[](GtkDrawingArea*, cairo_t* cr, int w, int h, gpointer d) {
        static_cast<Impl*>(d)->draw(cr, w, h);
    }, p, nullptr);
    auto* drag = gtk_gesture_drag_new();
    fx::on2<double, double>(drag, "drag-begin", [p](double x, double y) {
        p->drag = p->rt_on && y >= Impl::BAR_B + 2 ? Impl::RT : Impl::ACT;
        p->start_x = x;
        p->apply(x);
    });
    fx::on2<double, double>(drag, "drag-update", [p](double dx, double) { p->apply(p->start_x + dx); });
    fx::on2<double, double>(drag, "drag-end", [p](double, double) { p->drag = Impl::NONE; });
    gtk_widget_add_controller(a, GTK_EVENT_CONTROLLER(drag));
    auto* mo = gtk_event_controller_motion_new();
    fx::on2<double, double>(mo, "motion", [p](double x, double y) {
        p->set_hover(y >= Impl::BAR_T - 30 && y <= Impl::BAR_B + 4 ? p->seg_at(x) : 0);
    });
    ui::on(mo, "leave", [p] { p->set_hover(0); });
    gtk_widget_add_controller(a, mo);
    free_with(a, "ss-travelbar", tb, [](gpointer d) { auto* t = static_cast<TravelBar*>(d); delete t->p; delete t; });
    return tb;
}
GtkWidget* TravelBar::widget() { return p->area; }
void TravelBar::set(int act, int rt, bool rt_on) {
    p->act = std::clamp(act, 1, Impl::SEG), p->rt = std::clamp(rt, 1, 5), p->rt_on = rt_on;
    gtk_widget_queue_draw(p->area);
}
void TravelBar::set_depth(double d) {
    p->depth = d;
    gtk_widget_queue_draw(p->area);
}

// ================================================================================================
struct LevelBar::Impl {
    static constexpr double PILL_H = 28, GAP = 6, SIDE = 2;
    LevelBar* lb;
    GtkWidget* area;
    int lo, hi, v, hover = -1;
    std::string lo_text, hi_text;

    int n() const { return hi - lo + 1; }
    double pill_w() const { return (gtk_widget_get_width(area) - 2 * SIDE - GAP * (n() - 1)) / n(); }
    int at(double x) const { return std::clamp((int)std::floor((x - SIDE) / (pill_w() + GAP)), 0, n() - 1) + lo; }
    void pick(double x) {
        int a = at(x);
        if (a == v) return;
        v = a;
        gtk_widget_queue_draw(area);
        if (lb->on_change) lb->on_change(v);
    }
    void set_hover(int h) {
        if (h != hover) hover = h, gtk_widget_queue_draw(area);
    }
    void draw(cairo_t* cr, int W, int) {
        fx::Rgb ac = rgb(ACCENT);
        double pw = pill_w();
        for (int i = lo; i <= hi; i++) {
            double x = SIDE + (i - lo) * (pw + GAP);
            bool sel = i == v, below = i < v, hov = i == hover;
            if (sel) color(cr, ac);
            else if (below) color(cr, ac, hov ? 0.5 : 0.35);
            else cairo_set_source_rgba(cr, 1, 1, 1, hov ? 0.20 : 0.10);
            fx::round_rect(cr, x, 0, pw, PILL_H, PILL_H / 2);
            cairo_fill(cr);
            cairo_set_source_rgba(cr, 1, 1, 1, sel ? 1 : below ? 0.8 : 0.55);
            text(cr, std::to_string(i), "Sans Bold 9.5", x + pw / 2, PILL_H / 2 - 8, 0.5);
        }
        cairo_set_source_rgba(cr, 0.75, 0.76, 0.8, 0.8);
        text(cr, lo_text, "Sans 8.5", SIDE + 4, PILL_H + 7);
        text(cr, hi_text, "Sans 8.5", W - SIDE - 4, PILL_H + 7, 1);
    }
};

LevelBar* LevelBar::create(int lo, int hi, const char* lo_text, const char* hi_text) {
    auto* lb = new LevelBar;
    auto* p = lb->p = new Impl;
    p->lb = lb, p->lo = lo, p->hi = std::max(lo, hi), p->v = lo - 1;
    p->lo_text = lo_text ? lo_text : "", p->hi_text = hi_text ? hi_text : "";
    GtkWidget* a = p->area = gtk_drawing_area_new();
    gtk_widget_set_name(a, "level-bar");
    gtk_widget_set_size_request(a, 200, 52);
    gtk_widget_set_hexpand(a, TRUE);
    pointer_cursor(a);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(a), +[](GtkDrawingArea*, cairo_t* cr, int w, int h, gpointer d) {
        static_cast<Impl*>(d)->draw(cr, w, h);
    }, p, nullptr);
    auto* drag = gtk_gesture_drag_new();
    auto start = std::make_shared<double>(0);
    fx::on2<double, double>(drag, "drag-begin", [p, start](double x, double) { *start = x, p->pick(x); });
    fx::on2<double, double>(drag, "drag-update", [p, start](double dx, double) { p->pick(*start + dx); });
    gtk_widget_add_controller(a, GTK_EVENT_CONTROLLER(drag));
    auto* mo = gtk_event_controller_motion_new();
    fx::on2<double, double>(mo, "motion", [p](double x, double y) { p->set_hover(y <= Impl::PILL_H + 4 ? p->at(x) : -1); });
    ui::on(mo, "leave", [p] { p->set_hover(-1); });
    gtk_widget_add_controller(a, mo);
    free_with(a, "ss-levelbar", lb, [](gpointer d) { auto* t = static_cast<LevelBar*>(d); delete t->p; delete t; });
    return lb;
}
GtkWidget* LevelBar::widget() { return p->area; }
void LevelBar::set(int v) {
    p->v = v;
    gtk_widget_queue_draw(p->area);
}
