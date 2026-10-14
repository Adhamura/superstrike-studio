// MouseView effects: grid, live input animation, Lines and Boxes callouts, value glyphs.
#include "mouseview_fx.hpp"
#include <librsvg/rsvg.h>
#include <algorithm>
#include <memory>
#include "widgets.hpp"

namespace fx {
namespace {

// Draw `f` into a group and paint it through the zone mask.
template <class F> void masked(cairo_t* cr, const ZM& m, F f) {
    cairo_save(cr);
    cairo_rectangle(cr, m.ox, m.oy, m.w, m.h);
    cairo_clip(cr);
    cairo_push_group(cr);
    f();
    cairo_pop_group_to_source(cr);
    cairo_mask_surface(cr, m.surf, m.ox, m.oy);
    cairo_restore(cr);
}
const ZM* find(const std::vector<ZM>& zm, const char* name) {
    for (auto& m : zm)
        if (m.name == name && m.ok) return &m;
    return nullptr;
}

// ---- glyphs: '\x01' actuation, '\x02' rapid trigger, '\x03' haptics (14 px, accent coloured)
struct Unref { void operator()(RsvgHandle* h) const { g_object_unref(h); } };

RsvgHandle* icon_handle(int k, const std::string& accent) {
    static const char* const names[] = {"ss-actuation-symbolic", "ss-rapid-symbolic", "ss-haptics-symbolic"};
    static std::map<std::string, std::unique_ptr<RsvgHandle, Unref>> cache;
    std::string key = std::to_string(k) + accent;
    auto it = cache.find(key);
    if (it == cache.end()) {
        std::unique_ptr<RsvgHandle, Unref> h;
        std::string path = asset_path(std::string("icons/hicolor/scalable/actions/") + names[k - 1] + ".svg");
        if (g_file_test(path.c_str(), G_FILE_TEST_EXISTS)) {
            h.reset(rsvg_handle_new_from_file(path.c_str(), nullptr));
            std::string css = "* { fill: " + accent + " !important; stroke: " + accent + " !important }";
            if (h) rsvg_handle_set_stylesheet(h.get(), (const guint8*)css.data(), css.size(), nullptr);
        }
        it = cache.emplace(key, std::move(h)).first;
    }
    return it->second.get();
}

void arrow(cairo_t* cr, double x, double y0, double y1) {  // vertical arrow from y0 to y1
    double d = y1 > y0 ? 1 : -1;
    cairo_move_to(cr, x, y0);
    cairo_line_to(cr, x, y1);
    cairo_move_to(cr, x - 2.6, y1 - d * 2.6);
    cairo_line_to(cr, x, y1);
    cairo_line_to(cr, x + 2.6, y1 - d * 2.6);
}

void glyph(cairo_t* cr, int k, double x, double y, const std::string& accent) {
    if (RsvgHandle* h = icon_handle(k, accent)) {
        RsvgRectangle vp{x, y, 14, 14};
        rsvg_handle_render_document(h, cr, &vp, nullptr);
        return;
    }
    cairo_save(cr);
    color(cr, rgb(accent));
    cairo_set_line_width(cr, 1.6);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    if (k == 1) {  // down arrow onto a bar
        arrow(cr, x + 7, y + 1, y + 9.5);
        cairo_move_to(cr, x + 1.5, y + 12.5);
        cairo_line_to(cr, x + 12.5, y + 12.5);
    } else if (k == 2) {  // up + down arrows
        arrow(cr, x + 4, y + 12.5, y + 1.5);
        arrow(cr, x + 10, y + 1.5, y + 12.5);
    } else {  // two waves
        for (double r : {4.5, 8.5}) cairo_new_sub_path(cr), cairo_arc(cr, x + 3, y + 7, r, -0.9, 0.9);
    }
    cairo_stroke(cr);
    if (k == 3) dot(cr, x + 3, y + 7, 1.4), cairo_fill(cr);
    cairo_restore(cr);
}

std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> v;
    size_t a = 0, b;
    while ((b = s.find('\n', a)) != std::string::npos) v.push_back(s.substr(a, b - a)), a = b + 1;
    v.push_back(s.substr(a));
    return v;
}
bool has_glyph(const std::string& row) { return !row.empty() && row[0] >= 1 && row[0] <= 3; }

}  // namespace

// ---- grid -------------------------------------------------------------------------------------
void draw_grid(cairo_t* cr, const ZM& m, Rect r, bool sel, const std::string& accent) {
    Rgb a = rgb(accent);
    double step = std::max(6.0, r.w * 0.045);
    masked(cr, m, [&] {
        color(cr, a, sel ? 0.95 : 0.55);
        cairo_set_line_width(cr, 1);
        for (double x = r.x; x < r.x + r.w; x += step) cairo_move_to(cr, std::floor(x) + .5, r.y), cairo_line_to(cr, std::floor(x) + .5, r.y + r.h);
        for (double y = r.y; y < r.y + r.h; y += step) cairo_move_to(cr, r.x, std::floor(y) + .5), cairo_line_to(cr, r.x + r.w, std::floor(y) + .5);
        cairo_stroke(cr);
    });
    if (!sel || !m.ok) return;
    double rad = std::max(9.0, r.w * 0.05), bx = m.cx, by = m.cy + rad * 2.4;  // below the callout dot
    color(cr, a);
    dot(cr, bx, by, rad);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_set_line_width(cr, std::max(2.0, rad * 0.22));
    cairo_move_to(cr, bx - rad * 0.45, by);
    cairo_line_to(cr, bx - rad * 0.1, by + rad * 0.35);
    cairo_line_to(cr, bx + rad * 0.45, by - rad * 0.35);
    cairo_stroke(cr);
}

// ---- live input -------------------------------------------------------------------------------
void draw_input(cairo_t* cr, const Input& in, const std::vector<ZM>& zm, Rect r, bool top, const std::string& accent,
                gint64 now) {
    Rgb a = rgb(accent);
    for (auto& m : zm) {  // press shade + glow
        auto it = in.press.find(m.name);
        if (m.name == "wheel" || it == in.press.end() || it->second <= 0.01) continue;
        color(cr, {0, 0, 0}, 0.18 * it->second);
        cairo_mask_surface(cr, m.surf, m.ox, m.oy);
        color(cr, a, 0.55 * it->second);
        cairo_mask_surface(cr, m.surf, m.ox, m.oy);
    }
    for (auto& [z, t0] : in.ripples) {  // ripple masked by the zone
        const ZM* m = find(zm, z.c_str());
        double age = (now - t0) / 600000.0;
        if (!m || age < 0 || age >= 1) continue;
        masked(cr, *m, [&] {
            dot(cr, m->cx, m->cy, 6 + r.w * 0.45 * (1 - (1 - age) * (1 - age)));
            color(cr, a, 0.30 * (1 - age));
            cairo_fill_preserve(cr);
            cairo_set_source_rgba(cr, 1, 1, 1, 0.75 * (1 - age));
            cairo_set_line_width(cr, 3);
            cairo_stroke(cr);
        });
    }
    auto wi = in.press.find("wheel");
    double wl = wi == in.press.end() ? 0 : wi->second;
    const ZM* wm = find(zm, "wheel") ? find(zm, "wheel") : find(zm, "btn-middle");
    if (wl <= 0.01 || !wm) return;
    double half = std::max(8.0, (wm->cy - wm->top) * 1.2);
    double gap = std::max(3.0, r.h * (top ? 0.011 : 0.016));
    double off = std::fmod(std::fmod(in.phase * gap, gap) + gap, gap);
    masked(cr, *wm, [&] {
        color(cr, a, 0.45 * wl);
        cairo_paint(cr);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.9 * wl);
        cairo_set_line_width(cr, std::max(1.2, gap * 0.35));
        for (double y = wm->cy - half - gap + off; y < wm->cy + half + gap; y += gap) {
            cairo_move_to(cr, wm->cx - r.w * 0.2, y);
            cairo_line_to(cr, wm->cx + r.w * 0.2, y);
        }
        cairo_stroke(cr);
    });
    double d = in.vel > 0 ? 1 : -1, s = std::max(6.0, r.w * 0.035);  // direction arrow beside the wheel
    double ax = wm->cx + r.w * (top ? 0.16 : 0.30), ay = wm->cy + d * half * 0.2;
    color(cr, a, wl);
    cairo_move_to(cr, ax - s, ay - d * s * 0.6);
    cairo_line_to(cr, ax + s, ay - d * s * 0.6);
    cairo_line_to(cr, ax, ay + d * s * 0.8);
    cairo_close_path(cr);
    cairo_fill(cr);
}

// ---- callouts: lines --------------------------------------------------------------------------
void callouts_lines(cairo_t* cr, std::vector<Anchor> items, Rect r, const std::string& accent) {
    Rgb a = rgb(accent);
    std::sort(items.begin(), items.end(), [](auto& p, auto& q) { return p.y < q.y; });
    std::vector<double> placed[2];
    for (auto& it : items) {
        if (it.text.empty()) continue;
        size_t nl = it.text.find('\n');
        std::string title = it.text.substr(0, nl), detail = nl == std::string::npos ? "" : it.text.substr(nl + 1);
        double ly = it.y;
        auto& pl = placed[it.left ? 0 : 1];
        for (double py : pl)
            if (std::fabs(ly - py) < 54) ly = py + 54;
        pl.push_back(ly);
        bool hi = it.sel || it.hot;
        double lx = it.left ? r.x - 30 : r.x + r.w + 30;
        color(cr, a, hi ? 1 : 0.75);
        cairo_set_line_width(cr, hi ? 2 : 1.3);
        cairo_move_to(cr, it.x, it.y);
        cairo_line_to(cr, lx + (it.left ? 14 : -14), ly);
        cairo_line_to(cr, lx, ly);
        cairo_stroke(cr);
        dot(cr, it.x, it.y, hi ? 4.5 : 3.5);
        cairo_fill(cr);
        double al = it.left ? 1 : 0, tx = it.left ? lx - 8 : lx + 8;
        cairo_set_source_rgb(cr, 0.93, 0.93, 0.95);
        Size t = text(cr, title, "Sans Bold 10.5", tx, 0, al, false);
        text(cr, title, "Sans Bold 10.5", tx, ly - t.h, al);
        if (!detail.empty()) {
            color(cr, a);
            text(cr, detail, "Sans 9.5", tx, ly + 1, al);
        }
    }
}

// ---- callouts: boxes (G HUB HITS look) --------------------------------------------------------
void callouts_boxes(cairo_t* cr, std::vector<Anchor> items, Rect r, double W, double H, const std::string& accent) {
    constexpr double PAD = 12, ROW = 22, VPAD = 8, GLY = 22;
    const char* font = "Sans Bold 11";
    Rgb a = rgb(accent);
    std::sort(items.begin(), items.end(), [](auto& p, auto& q) { return p.y < q.y; });
    double bottom[2] = {-1e9, -1e9};
    for (auto& it : items) {
        if (it.text.empty()) continue;
        auto rows = split(it.text);
        double tw = 0;
        for (auto& row : rows)
            tw = std::max(tw, (has_glyph(row) ? GLY : 0) + text(cr, has_glyph(row) ? row.substr(1) : row, font, 0, 0, 0, false).w);
        double bw = std::max(88.0, tw + 2 * PAD), bh = rows.size() * ROW + 2 * VPAD;
        double bx = it.left ? std::max(6.0, r.x - 10 - bw) : std::min(W - 6 - bw, r.x + r.w + 10);
        double& bt = bottom[it.left ? 0 : 1];
        double by = std::max(std::min(it.y - bh / 2, H - 8 - bh), 8.0);
        by = std::max(by, bt + 8);
        bt = by + bh;
        // leader line from the box edge to a dot on the zone
        double ex = it.left ? bx + bw : bx;
        color(cr, a, it.sel || it.hot ? 1 : 0.7);
        cairo_set_line_width(cr, 1.2);
        cairo_move_to(cr, ex, by + bh / 2);
        cairo_line_to(cr, it.x, it.y);
        cairo_stroke(cr);
        dot(cr, it.x, it.y, 4);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 0, 0, 0, 0.45);
        cairo_set_line_width(cr, 1);
        cairo_stroke(cr);
        // box
        cairo_set_source_rgba(cr, 0.12, 0.12, 0.14, 1);  // opaque base keeps the leader line out of the text
        round_rect(cr, bx, by, bw, bh, 10);
        cairo_fill(cr);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.06);
        round_rect(cr, bx, by, bw, bh, 10);
        cairo_fill(cr);
        if (it.sel) color(cr, a), cairo_set_line_width(cr, 1.5);
        else if (it.hot) color(cr, a, 0.6), cairo_set_line_width(cr, 1);
        else cairo_set_source_rgba(cr, 1, 1, 1, 0.12), cairo_set_line_width(cr, 1);
        round_rect(cr, bx + .75, by + .75, bw - 1.5, bh - 1.5, 9.5);
        cairo_stroke(cr);
        for (size_t i = 0; i < rows.size(); i++) {
            double cy = by + VPAD + i * ROW + ROW / 2;
            bool g = has_glyph(rows[i]);
            if (g) glyph(cr, rows[i][0], bx + PAD, cy - 7, accent);
            cairo_set_source_rgb(cr, 0.93, 0.93, 0.95);
            std::string s = g ? rows[i].substr(1) : rows[i];
            while (!s.empty() && s[0] == ' ') s.erase(0, 1);
            Size t = text(cr, s, font, 0, 0, 0, false);
            text(cr, s, font, bx + PAD + (g ? GLY : 0), cy - t.h / 2.0);
        }
    }
}

}  // namespace fx
