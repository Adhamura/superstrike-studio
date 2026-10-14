// Custom drawn widgets. Each object lives as long as its GtkWidget (freed on "destroy").
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <adwaita.h>

// Zones: top view "btn-left","btn-right","btn-middle"; side view "btn-forward","btn-back","btn-middle".
struct MouseView {
    struct Impl;
    static MouseView* create();  // owned by widget()
    GtkWidget* widget();

    // --- configuration (call redraw() after changing)
    std::vector<std::string> zones;  // clickable zones; empty = all of the view
    enum class Callouts { None, Lines, Boxes } callouts = Callouts::Lines;
    bool thumbnail = true;  // small other-side view, click it to rotate
    bool grid = false;      // G HUB style grid on the zones (HITS page)
    bool flip = true;       // drag sideways / thumbnail rotates the view
    std::string selected;   // highlighted zone ("" none)
    // Callout text per zone: "Title\nline 2\nline 3" ("" = no callout). Boxes mode draws the
    // lines inside a rounded box beside the mouse (G HUB HITS look); first char of a line may
    // be an icon key: '\x01' actuation, '\x02' rapid trigger, '\x03' haptics.
    std::function<std::string(const std::string& zone)> label_for;
    std::function<void(const std::string& zone)> on_zone;     // a zone was clicked
    std::function<void(const std::string& view)> on_view;    // view changed ("top"/"side")

    void set_view(const std::string& view, bool animate = true);
    const std::string& view() const;
    void set_colors(const std::string& shell_hex, const std::string& accent_hex);
    void redraw();
    // live input animation (real mouse): press ripple + glow, rolling wheel ribs
    void input_button(const std::string& zone, bool down);
    void input_scroll(double dy);  // > 0 = wheel rolled toward the palm

    Impl* p = nullptr;
};

// G HUB style press-travel bar: 10 segments from Short click to Long click. Click/drag sets the
// actuation point; with rapid trigger on, the orange reset band's end sets the RT level 1..5.
struct TravelBar {
    struct Impl;
    static TravelBar* create();
    GtkWidget* widget();
    std::function<void(int act, int rt)> on_change;
    void set(int act, int rt, bool rt_on);
    void set_depth(double depth01);  // live press depth, < 0 hides it
    Impl* p = nullptr;
};

// Discrete level picker drawn as pills (haptics 0..5, RT 1..5, ...). Click/drag selects.
struct LevelBar {
    struct Impl;
    static LevelBar* create(int lo, int hi, const char* lo_text, const char* hi_text);
    GtkWidget* widget();
    std::function<void(int)> on_change;
    void set(int v);
    Impl* p = nullptr;
};

std::string asset_path(const std::string& name);  // finds assets/ next to the binary or install dir
