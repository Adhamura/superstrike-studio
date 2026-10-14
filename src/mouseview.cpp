// MouseView: SVG mouse picture (librsvg) with clickable zones, callouts, flip and live input animation.
#include <librsvg/rsvg.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include "i18n.hpp"
#include "mouseview_fx.hpp"
#include "ui.hpp"
#include "widgets.hpp"

using fx::color;
using fx::Rect;
using fx::rgb;

namespace {
const char* const VIEW[2] = {"top", "side"};
const char* const FILE[2] = {"mouse_top.svg", "mouse_side.svg"};
const char* const ZONE[2][3] = {{"btn-left", "btn-right", "btn-middle"}, {"btn-forward", "btn-back", "btn-middle"}};
constexpr gint64 FLIP_US = 450000;

struct Unref { void operator()(RsvgHandle* h) const { g_object_unref(h); } };
struct SurfDel { void operator()(cairo_surface_t* s) const { cairo_surface_destroy(s); } };
using Handle = std::unique_ptr<RsvgHandle, Unref>;

const std::string& svg_data(int v) {
    static std::string d[2];
    static bool loaded[2];
    if (!loaded[v]) {
        loaded[v] = true;
        gchar* c = nullptr;
        gsize n = 0;
        if (g_file_get_contents(asset_path(FILE[v]).c_str(), &c, &n, nullptr)) d[v].assign(c, n), g_free(c);
    }
    return d[v];
}
Handle make_handle(int v, const std::string& css) {
    const std::string& d = svg_data(v);
    if (d.empty()) return nullptr;
    Handle h(rsvg_handle_new_from_data((const guint8*)d.data(), d.size(), nullptr));
    if (h) rsvg_handle_set_stylesheet(h.get(), (const guint8*)css.data(), css.size(), nullptr);
    return h;
}
void render(RsvgHandle* h, cairo_t* cr, Rect r) {
    RsvgRectangle vp{r.x, r.y, r.w, r.h};
    if (h) rsvg_handle_render_document(h, cr, &vp, nullptr);
}
std::pair<double, double> isize(int v) {
    static std::pair<double, double> s[2] = {{0, 0}, {0, 0}};
    if (s[v].first == 0) {
        s[v] = v ? std::pair{300.0, 600.0} : std::pair{510.0, 993.0};
        if (Handle h = make_handle(v, "")) rsvg_handle_get_intrinsic_size_in_pixels(h.get(), &s[v].first, &s[v].second);
    }
    return s[v];
}

// Hit mask of one zone (A8, the size of the drawn picture) plus centroid, sampled every 3 px.
struct Mask {
    std::unique_ptr<cairo_surface_t, SurfDel> surf;
    int w = 0, h = 0, stride = 0;
    const unsigned char* data = nullptr;
    double cx = 0, cy = 0, top = 0;
    bool ok = false;
    bool hit(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h && data[y * stride + x] > 100; }
};
Mask build_mask(int v, const std::string& zone, int w, int h) {
    Mask m;
    m.w = w, m.h = h;
    m.surf.reset(cairo_image_surface_create(CAIRO_FORMAT_A8, w, h));
    std::string css = "#body, #lines, #brand, #wheel { display: none !important; } ";
    if (zone == "wheel")
        css = "#body, #lines, #brand, .zone { display: none !important; } "
              "#wheel { fill: #000 !important; fill-opacity: 1 !important; stroke: none !important; }";
    else
        css += ".zone { fill: #000 !important; fill-opacity: 0 !important; stroke: none !important; } #" + zone +
               " { fill-opacity: 1 !important; }";
    cairo_t* cr = cairo_create(m.surf.get());
    render(make_handle(v, css).get(), cr, {0, 0, (double)w, (double)h});
    cairo_destroy(cr);
    cairo_surface_flush(m.surf.get());
    m.data = cairo_image_surface_get_data(m.surf.get());
    m.stride = cairo_image_surface_get_stride(m.surf.get());
    double sx = 0, sy = 0, n = 0;
    for (int y = 0; y < h; y += 3)
        for (int x = 0; x < w; x += 3)
            if (m.hit(x, y)) {
                sx += x, sy += y, n++;
                if (!m.ok) m.top = y;
                m.ok = true;
            }
    if (n) m.cx = sx / n, m.cy = sy / n;
    return m;
}
struct Masks { std::vector<std::pair<std::string, Mask>> m; };
}  // namespace

struct MouseView::Impl {
    MouseView* mv;
    GtkWidget* area;
    std::string view = "top", hover, shell = "#ffffff", accent = "#3b9cff";
    int cur = 0;  // settled view (0 top, 1 side)
    struct { gint64 t0; int from, to; double p; } anim{};
    bool animating = false, thumb_hover = false, pointer = false;
    guint anim_id = 0, in_id = 0;
    double drag_dx = 0;
    std::map<std::string, Handle> handles;
    std::vector<std::pair<std::array<int, 3>, std::shared_ptr<Masks>>> mcache;
    fx::Input in;
    std::map<std::string, bool> held;
    gint64 last_us = 0;

    explicit Impl(MouseView* m);

    // ---- helpers
    std::vector<std::string> active(int v) const {
        std::vector<std::string> r;
        for (const char* z : ZONE[v])
            if (mv->zones.empty() || std::find(mv->zones.begin(), mv->zones.end(), z) != mv->zones.end()) r.push_back(z);
        return r;
    }
    bool is_active(int v, const std::string& z) const {
        auto a = active(v);
        return std::find(a.begin(), a.end(), z) != a.end();
    }
    int target() const { return animating ? anim.to : cur; }
    void redraw() { gtk_widget_queue_draw(area); }

    std::string css(int v, const std::string& hov, const std::string& sel) const {
        std::string s = ".body { fill: " + shell + " !important; } .line { stroke: #16161a !important; } "
                        ".brand { fill: #9c9ca3 !important; } .zone { fill-opacity: 0 !important; stroke: none !important; } ";
        auto rule = [&](const std::string& z, const char* op, const char* extra) {
            s += "#" + z + " { fill: " + accent + " !important; fill-opacity: " + op + " !important; " + extra + "} ";
        };
        for (auto& z : active(v)) rule(z, "0.16", "");
        if (is_active(v, hov)) rule(hov, "0.38", "");
        if (is_active(v, sel)) rule(sel, "0.62", ("stroke: " + accent + " !important; stroke-width: 6 !important; ").c_str());
        return s;
    }
    RsvgHandle* handle(int v, const std::string& hov, const std::string& sel) {
        std::string c = css(v, hov, sel), key = (v ? "s" : "t") + c;
        auto it = handles.find(key);
        if (it == handles.end()) {
            if (handles.size() >= 10) handles.clear();
            it = handles.emplace(key, make_handle(v, c)).first;
        }
        return it->second.get();
    }

    // ---- layout
    Rect thumb_rect(int v, double W, double H) const {
        auto [sw, sh] = isize(v);
        double th = std::round(std::min(H * 0.24, 170.0)), tw = std::round(th * sw / sh);
        return {std::round(W - tw - 16), std::round(H - th - 38), tw, th};
    }
    Rect fit(int v, double W, double H) const {
        auto [sw, sh] = isize(v);
        double room = 8;
        if (mv->callouts == Callouts::Lines) room = std::min(260.0, W * 0.30);
        else if (mv->callouts == Callouts::Boxes) room = std::min(190.0, W * 0.28);
        else if (mv->thumbnail) room = thumb_rect(1 - v, W, H).w + 22;
        double s = std::min(std::max(50.0, W - 2 * room) / sw, std::max(50.0, H - 30) / sh);
        double w = std::round(sw * s), h = std::round(sh * s);
        return {std::round((W - w) / 2), std::round((H - h) / 2), w, h};
    }
    bool in_thumb(double x, double y) const {
        if (!mv->thumbnail || !mv->flip || animating) return false;
        Rect t = thumb_rect(1 - cur, gtk_widget_get_width(area), gtk_widget_get_height(area));
        return x >= t.x - 8 && x <= t.x + t.w + 8 && y >= t.y - 8 && y <= t.y + t.h + 30;
    }

    // ---- masks
    std::shared_ptr<Masks> masks(int v, Rect r) {
        std::array<int, 3> k{v, (int)r.w, (int)r.h};
        for (auto& e : mcache)
            if (e.first == k) return e.second;
        auto ms = std::make_shared<Masks>();
        for (const char* z : ZONE[v]) ms->m.emplace_back(z, build_mask(v, z, k[1], k[2]));
        if (v == 0) ms->m.emplace_back("wheel", build_mask(v, "wheel", k[1], k[2]));
        if (mcache.size() >= 4) mcache.erase(mcache.begin());
        mcache.emplace_back(k, ms);
        return ms;
    }
    static std::vector<fx::ZM> placed(const Masks& ms, Rect r) {
        std::vector<fx::ZM> v;
        for (auto& [name, m] : ms.m)
            v.push_back({name, m.surf.get(), r.x, r.y, m.w, m.h, r.x + m.cx, r.y + m.cy, r.y + m.top, m.ok});
        return v;
    }
    std::string zone_at(double px, double py) {
        if (animating) return "";
        Rect r = fit(cur, gtk_widget_get_width(area), gtk_widget_get_height(area));
        auto ms = masks(cur, r);
        auto act = active(cur);
        for (auto z = act.rbegin(); z != act.rend(); ++z)
            for (auto& [name, m] : ms->m)
                if (name == *z && m.hit((int)(px - r.x), (int)(py - r.y))) return *z;
        return "";
    }

    // ---- drawing
    void draw(cairo_t* cr, int W, int H) {
        int v = cur;
        double sx = 1;
        if (animating) v = anim.p < 0.5 ? anim.from : anim.to, sx = std::max(0.02, std::fabs(std::cos(M_PI * anim.p)));
        Rect r = fit(v, W, H);
        cairo_save(cr);  // floor shadow
        cairo_translate(cr, W / 2.0, r.y + r.h * 0.98);
        cairo_scale(cr, r.w * 0.42 * sx, 14);
        cairo_arc(cr, 0, 0, 1, 0, 2 * M_PI);
        cairo_restore(cr);
        cairo_set_source_rgba(cr, 0, 0, 0, 0.28);
        cairo_fill(cr);
        cairo_save(cr);
        cairo_translate(cr, W / 2.0, 0);
        cairo_scale(cr, sx, 1);
        cairo_translate(cr, -W / 2.0, 0);
        render(handle(v, hover, mv->selected), cr, r);
        cairo_restore(cr);
        if (animating) return;
        bool labels = mv->callouts != Callouts::None && mv->label_for;
        if (mv->grid || labels || in.busy()) {
            auto ms = masks(v, r);
            auto zm = placed(*ms, r);
            auto act = active(v);
            if (mv->grid)
                for (auto& m : zm)
                    if (std::find(act.begin(), act.end(), m.name) != act.end())
                        fx::draw_grid(cr, m, r, m.name == mv->selected, accent);
            if (in.busy()) fx::draw_input(cr, in, zm, r, v == 0, accent, g_get_monotonic_time());
            if (labels) callouts(cr, v, zm, r, W, H);
        }
        if (mv->thumbnail) draw_thumb(cr, 1 - v, W, H);
    }
    void callouts(cairo_t* cr, int v, const std::vector<fx::ZM>& zm, Rect r, int W, int H) {
        std::vector<fx::Anchor> items;
        for (auto& z : active(v))
            for (auto& m : zm) {
                if (m.name != z || !m.ok) continue;
                std::string text = mv->label_for(z);
                double y = v == 0 && z == "btn-middle" ? m.top + (m.cy - m.top) * 0.6 : m.cy;
                bool left = v == 1 || (z != "btn-middle" && m.cx < r.x + r.w / 2 - 2);
                items.push_back({m.cx, y, left, z == mv->selected, z == hover, text});
            }
        if (mv->callouts == Callouts::Lines) fx::callouts_lines(cr, items, r, accent);
        else fx::callouts_boxes(cr, items, r, W, H, accent);
    }
    void draw_thumb(cairo_t* cr, int v, int W, int H) {
        Rect t = thumb_rect(v, W, H);
        bool hov = thumb_hover && mv->flip;
        color(cr, {1, 1, 1}, hov ? 0.10 : 0.05);
        fx::round_rect(cr, t.x - 8, t.y - 8, t.w + 16, t.h + 38, 12);
        cairo_fill(cr);
        if (hov) {
            color(cr, rgb(accent), 0.9);
            cairo_set_line_width(cr, 1.5);
            fx::round_rect(cr, t.x - 8, t.y - 8, t.w + 16, t.h + 38, 12);
            cairo_stroke(cr);
        }
        render(handle(v, "", ""), cr, t);
        if (in.busy()) fx::draw_input(cr, in, placed(*masks(v, t), t), t, v == 0, accent, g_get_monotonic_time());
        cairo_set_source_rgb(cr, 0.85, 0.86, 0.9);
        // the caption names the view that a click on the small picture switches to
        fx::text(cr, std::string(v ? C_("view", "Left side") : C_("view", "Top")) + "  ⟳", "Sans 9", t.x + t.w / 2, t.y + t.h + 6, 0.5);
    }

    // ---- view switching
    void switch_to(int to, bool animate) {
        if (to == target()) return;
        if (animating) {
            gtk_widget_remove_tick_callback(area, anim_id);
            cur = anim.to, animating = false, anim_id = 0;
            if (to == cur) return redraw();
        }
        view = VIEW[to];
        if (animate && gtk_widget_get_mapped(area)) {
            animating = true;
            anim = {g_get_monotonic_time(), cur, to, 0};
            anim_id = gtk_widget_add_tick_callback(area, +[](GtkWidget*, GdkFrameClock*, gpointer d) -> gboolean {
                return static_cast<Impl*>(d)->step_anim();
            }, this, nullptr);
        } else cur = to;
        redraw();
    }
    gboolean step_anim() {
        double t = (g_get_monotonic_time() - anim.t0) / (double)FLIP_US;
        redraw();
        if (t >= 1) {
            cur = anim.to, animating = false, anim_id = 0;
            return G_SOURCE_REMOVE;
        }
        anim.p = 0.5 - 0.5 * std::cos(M_PI * t);
        return G_SOURCE_CONTINUE;
    }
    void user_flip() {
        switch_to(1 - target(), true);
        if (mv->on_view) mv->on_view(view);
    }

    // ---- live input
    void start_input_tick() {
        if (!in_id && gtk_widget_get_mapped(area)) {
            last_us = g_get_monotonic_time();
            in_id = gtk_widget_add_tick_callback(area, +[](GtkWidget*, GdkFrameClock*, gpointer d) -> gboolean {
                return static_cast<Impl*>(d)->step_input();
            }, this, nullptr);
        }
        redraw();
    }
    gboolean step_input() {
        gint64 now = g_get_monotonic_time();
        double dt = std::min(0.05, (now - last_us) / 1e6);
        last_us = now;
        for (auto it = in.press.begin(); it != in.press.end();) {
            if (it->first == "wheel") { ++it; continue; }
            double target = held[it->first] ? 1.0 : 0.0, &l = it->second;
            l += (target - l) * std::min(1.0, dt * (target ? 28 : 9));
            if (std::fabs(l - target) < 0.01) l = target;
            it = l > 0 ? std::next(it) : in.press.erase(it);
        }
        in.phase += in.vel * dt;
        in.vel *= std::exp(-dt * 4.0);
        if (std::fabs(in.vel) < 0.05) in.vel = 0;
        if (in.vel != 0) in.press["wheel"] = std::min(1.0, std::fabs(in.vel) / 4.0);
        else in.press.erase("wheel");
        std::erase_if(in.ripples, [&](auto& r) { return now - r.second >= 600000; });
        redraw();
        bool busy = in.vel != 0 || !in.ripples.empty();
        for (auto& [z, l] : in.press) busy = busy || l > 0;
        if (!busy) in_id = 0;
        return busy ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
    }

    // ---- pointer
    void set_hover(const std::string& z, bool ptr) {
        if (z != hover) hover = z, redraw();
        if (ptr != pointer) {
            pointer = ptr;
            gtk_widget_set_cursor_from_name(area, ptr ? "pointer" : "default");
        }
    }
    void motion(double x, double y) {
        bool th = in_thumb(x, y);
        if (th != thumb_hover) thumb_hover = th, redraw();
        std::string z = th ? "" : zone_at(x, y);
        set_hover(z, th || !z.empty());
    }
    void released(double x, double y) {
        if (std::fabs(drag_dx) > 8) return;
        if (in_thumb(x, y)) return user_flip();
        std::string z = zone_at(x, y);
        if (z.empty()) return;
        mv->selected = z;
        redraw();
        if (mv->on_zone) mv->on_zone(z);
    }
};

MouseView::Impl::Impl(MouseView* m) : mv(m) {
    area = gtk_drawing_area_new();
    gtk_widget_set_name(area, "mouse-view");
    gtk_widget_set_hexpand(area, TRUE);
    gtk_widget_set_vexpand(area, TRUE);
    gtk_widget_set_size_request(area, 520, 420);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), +[](GtkDrawingArea*, cairo_t* cr, int w, int h, gpointer d) {
        static_cast<Impl*>(d)->draw(cr, w, h);
    }, this, nullptr);
    auto* motion_c = gtk_event_controller_motion_new();
    fx::on2<double, double>(motion_c, "motion", [this](double x, double y) { motion(x, y); });
    ui::on(motion_c, "leave", [this] { thumb_hover = false; set_hover("", false); redraw(); });
    gtk_widget_add_controller(area, motion_c);
    auto* click = gtk_gesture_click_new();
    ui::on3<int, double, double>(click, "released", [this](int, double x, double y) { released(x, y); });
    gtk_widget_add_controller(area, GTK_EVENT_CONTROLLER(click));
    auto* drag = gtk_gesture_drag_new();
    fx::on2<double, double>(drag, "drag-begin", [this](double, double) { drag_dx = 0; });
    fx::on2<double, double>(drag, "drag-update", [this](double dx, double) { drag_dx = dx; });
    fx::on2<double, double>(drag, "drag-end", [this](double dx, double dy) {
        if (mv->flip && std::fabs(dx) > 60 && std::fabs(dx) > std::fabs(dy)) user_flip();
    });
    gtk_widget_add_controller(area, GTK_EVENT_CONTROLLER(drag));
    ui::on(area, "destroy", [this] {  // no callbacks may run after this
        if (anim_id) gtk_widget_remove_tick_callback(area, anim_id);
        if (in_id) gtk_widget_remove_tick_callback(area, in_id);
        anim_id = in_id = 0;
    });
    g_object_set_data_full(G_OBJECT(area), "ss-mouseview", mv, +[](gpointer d) {
        auto* m = static_cast<MouseView*>(d);
        delete m->p;
        delete m;
    });
}

MouseView* MouseView::create() {
    auto* mv = new MouseView;
    mv->p = new Impl(mv);
    return mv;
}
GtkWidget* MouseView::widget() { return p->area; }
const std::string& MouseView::view() const { return p->view; }
void MouseView::redraw() { p->redraw(); }
void MouseView::set_view(const std::string& view, bool animate) { p->switch_to(view == "side" ? 1 : 0, animate); }
void MouseView::set_colors(const std::string& shell_hex, const std::string& accent_hex) {
    p->shell = shell_hex, p->accent = accent_hex;
    p->handles.clear();
    p->redraw();
}
void MouseView::input_button(const std::string& zone, bool down) {
    if (!gtk_widget_get_mapped(p->area)) return;
    p->held[zone] = down;
    if (down) {
        p->in.ripples.emplace_back(zone, g_get_monotonic_time());
        double& l = p->in.press[zone];
        l = std::max(l, 0.35);
    }
    p->start_input_tick();
}
void MouseView::input_scroll(double dy) {
    if (!gtk_widget_get_mapped(p->area)) return;
    p->in.vel = std::clamp(p->in.vel + 7.0 * dy, -26.0, 26.0);
    p->in.press["wheel"] = 1.0;
    p->start_input_tick();
}
