// Drawing helpers shared by mouseview*.cpp and levelbars.cpp (internal, not part of the widget contract).
#pragma once
#include <cairo.h>
#include <pango/pangocairo.h>
#include <glib.h>
#include <cmath>
#include <cstdlib>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace fx {

struct Rect { double x, y, w, h; };
struct Rgb { double r, g, b; };
struct Size { int w, h; };

inline Rgb rgb(const std::string& hex) {
    unsigned long v = strtoul(hex.c_str() + (!hex.empty() && hex[0] == '#'), nullptr, 16);
    return {((v >> 16) & 255) / 255.0, ((v >> 8) & 255) / 255.0, (v & 255) / 255.0};
}
inline void color(cairo_t* cr, Rgb c, double a = 1) { cairo_set_source_rgba(cr, c.r, c.g, c.b, a); }

inline void round_rect(cairo_t* cr, double x, double y, double w, double h, double r) {
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 1.5 * M_PI);
    cairo_close_path(cr);
}
inline void dot(cairo_t* cr, double x, double y, double r) {
    cairo_new_sub_path(cr);
    cairo_arc(cr, x, y, r, 0, 2 * M_PI);
}

// Text with the top-left corner at (x, y), moved left by align * width (0 left, .5 centre, 1 right).
// Uses the current source colour. draw = false only measures.
inline Size text(cairo_t* cr, const std::string& s, const char* font, double x, double y, double align = 0,
                 bool draw = true) {
    PangoLayout* l = pango_cairo_create_layout(cr);
    PangoFontDescription* d = pango_font_description_from_string(font);
    pango_layout_set_font_description(l, d);
    pango_font_description_free(d);
    pango_layout_set_text(l, s.c_str(), -1);
    pango_layout_set_alignment(l, align >= 1 ? PANGO_ALIGN_RIGHT : align > 0 ? PANGO_ALIGN_CENTER : PANGO_ALIGN_LEFT);
    Size sz{};
    pango_layout_get_pixel_size(l, &sz.w, &sz.h);
    if (draw) {
        cairo_move_to(cr, x - sz.w * align, y);
        pango_cairo_show_layout(cr, l);
    }
    g_object_unref(l);
    return sz;
}

// Signal with two arguments (motion, drag-update, drag-end ...).  F: void(A, B)
template <class A, class B, class F> gulong on2(gpointer obj, const char* sig, F f) {
    return g_signal_connect_data(
        obj, sig, G_CALLBACK(+[](gpointer, A a, B b, gpointer d) { (*static_cast<F*>(d))(a, b); }), new F(std::move(f)),
        +[](gpointer d, GClosure*) { delete static_cast<F*>(d); }, GConnectFlags(0));
}

// --- mouse view parts (mouseview_fx.cpp) -------------------------------------------------------

// A zone hit mask placed in widget coordinates (centroid in widget pixels).
struct ZM {
    std::string name;
    cairo_surface_t* surf;
    double ox, oy;  // where the mask surface sits in the widget
    int w, h;
    double cx, cy, top;
    bool ok;
};

// Live input state (press levels, ripples, wheel).
struct Input {
    std::map<std::string, double> press;                 // zone or "wheel" -> 0..1
    std::vector<std::pair<std::string, gint64>> ripples;  // zone, start (us)
    double phase = 0, vel = 0;
    bool busy() const {
        for (auto& [k, v] : press)
            if (v > 0.01) return true;
        return !ripples.empty();
    }
};

struct Anchor {  // one callout: where the zone is and what to say
    double x, y;
    bool left, sel, hot;
    std::string text;
};

void draw_grid(cairo_t* cr, const ZM& m, Rect r, bool selected, const std::string& accent);
void draw_input(cairo_t* cr, const Input& in, const std::vector<ZM>& zm, Rect r, bool top_view,
                const std::string& accent, gint64 now);
void callouts_lines(cairo_t* cr, std::vector<Anchor> items, Rect r, const std::string& accent);
void callouts_boxes(cairo_t* cr, std::vector<Anchor> items, Rect r, double W, double H, const std::string& accent);

}  // namespace fx
