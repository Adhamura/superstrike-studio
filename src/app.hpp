// Shared application state. Only touched on the GTK main thread; device IO runs on the worker.
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include <adwaita.h>
#include "profile.hpp"

struct DeviceInfo {
    std::string name = "PRO X3 SUPERSTRIKE", path;
    int index = 0;  // receiver slot, 0xFF cable
    std::vector<std::string> firmware;
    int battery = -1;
    std::string battery_state;
    OnboardInfo onboard;
    int mode = MODE_ONBOARD;
    int surface = -1;  // -1 unknown
    int stage = 0;     // DPI stage the mouse uses now
    std::vector<int> dpi_list;
    std::vector<FeatureEntry> features;
};

struct Config {  // ~/.config/superstrike/studio.ini
    std::string shell = "#ffffff", accent = "#3b9cff";
    std::string lang = "system";  // "system" or a code from LANGUAGES (i18n.hpp)
    std::vector<std::pair<std::string, std::string>> presets;  // user presets: name -> HITS code
    void load();
    void save() const;
};

constexpr int DIR_SECTOR = 0;  // mark_dirty(DIR_SECTOR) = directory (profile cycle) changed

struct App {
    GtkWindow* window = nullptr;
    AdwToastOverlay* toasts = nullptr;
    Config cfg;

    bool connected = false;
    DeviceInfo info;
    Bytes dir_raw;
    std::vector<DirEntry> dir;
    std::map<int, Profile> profiles;  // by sector
    Profile factory;                  // ROM sector 0x101
    int active = -1, edit = -1;       // sectors
    std::set<int> dirty;              // sectors (DIR_SECTOR = directory)

    // Events. Pages register callbacks in their build function.
    std::vector<std::function<void()>> on_reload;  // device (re)loaded or edited profile changed
    std::vector<std::function<void()>> on_dirty;   // dirty set changed
    std::vector<std::function<void()>> on_status;  // connection / battery / mode changed
    std::vector<std::function<void(const std::string& zone, bool down)>> on_button;  // real mouse
    std::vector<std::function<void(double dy)>> on_scroll;

    Profile* cur();                 // profile being edited, nullptr when none
    int slot_of(int sector) const;  // 0-based position in the directory
    void set_edit(int sector);      // fires on_reload
    void mark_dirty(int sector);    // fires on_dirty
    void reload_ui();               // fires on_reload
    void toast(const std::string& text);

    // Worker thread. job runs off the main thread with the open device; done/fail run on the
    // main thread. Nothing runs (fail gets "not connected") without a device.
    void run(std::function<void(Device&)> job, std::function<void()> done = {},
             std::function<void(const std::string&)> fail = {});
    void live_hits(int button);     // send cur()->hits[button] now when editing the active profile
    void activate_profile(int sector);
    void save();                    // write dirty sectors (+auto backup first time), reload active
    void revert();                  // re-read everything from the mouse
    void connect();                 // find the mouse and load everything
    void show_page(const char* name);
    void set_language(const std::string& code);  // switch language now: rebuilds sidebar and pages in place  // "hits","buttons","sensor","profiles","device"

    struct Impl;
    Impl* p = nullptr;
};

// Pages (one file each). They read/write app state and register on_reload to refresh.
GtkWidget* page_hits(App& app);
GtkWidget* page_buttons(App& app);
GtkWidget* page_sensor(App& app);
GtkWidget* page_profiles(App& app);
GtkWidget* page_device(App& app);

// Backups: whole memory to/from JSON {"device":..,"time":..,"sectors":{"0x0001":"hex",..}}
std::string backup_dir();  // ~/.local/share/superstrike/backups
std::string backup_json(Device& d);
bool restore_json(Device& d, const std::string& json, std::string* err);
