// App logic: worker thread, connection, saving, live HITS, real-mouse input.
#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif
#include <algorithm>
#include <fstream>
#include <glib/gstdio.h>
#include "app_impl.hpp"
#include "i18n.hpp"
#include "ui.hpp"

static double now_s() { return g_get_monotonic_time() / 1e6; }
template <class V, class... A> static void fire(V& list, A... a) { for (auto& f : list) f(a...); }

// ------------------------------------------------------------------ state helpers
Profile* App::cur() {
    auto it = profiles.find(edit);
    return it == profiles.end() ? nullptr : &it->second;
}

int App::slot_of(int sector) const {
    for (size_t i = 0; i < dir.size(); i++)
        if (dir[i].sector == sector) return (int)i;
    return 0;
}

void App::set_edit(int sector) {
    edit = sector;
    reload_ui();
}

void App::mark_dirty(int sector) {
    dirty.insert(sector);
    fire(on_dirty);
}

void App::reload_ui() { fire(on_reload); }

void App::toast(const std::string& text) {
    AdwToast* t = adw_toast_new("");
    adw_toast_set_title(t, text.c_str());
    adw_toast_set_use_markup(t, FALSE);
    adw_toast_set_timeout(t, 4);
    adw_toast_overlay_add_toast(toasts, t);
}

// ------------------------------------------------------------------ worker
static void worker_loop(App* app) {
    auto& p = *app->p;
    for (;;) {
        Job job;
        {
            std::unique_lock lk(p.mu);
            p.cv.wait(lk, [&] { return !p.jobs.empty(); });
            job = std::move(p.jobs.front());
            p.jobs.pop_front();
        }
        if (!job.work) return;  // stop marker
        std::string error;
        try {
            if (job.connect) p.dev = Device::open();
            if (!p.dev) throw HidppError("not connected");
            job.work(*p.dev);
        } catch (const std::exception& e) {
            error = e.what();
            if (job.connect) p.dev.reset();
        }
        if (error.empty()) {
            if (job.done) ui::idle(std::move(job.done));
        } else {
            auto fail = job.fail;
            ui::idle([app, fail, error] {
                if (fail) fail(error);
                // TRANSLATORS: %s is a technical error message about the mouse connection
                else app->toast(fmt(_("Mouse: %s"), error.c_str()));
            });
        }
    }
}

void App::run(std::function<void(Device&)> job, std::function<void()> done, std::function<void(const std::string&)> fail) {
    if (!connected) {
        if (fail) fail("not connected");
        return;
    }
    std::lock_guard lk(p->mu);
    p->jobs.push_back({std::move(job), std::move(done), std::move(fail), false});
    p->cv.notify_one();
}

// ------------------------------------------------------------------ loading
struct Loaded {
    DeviceInfo info;
    Bytes dir_raw;
    std::vector<DirEntry> dir;
    std::map<int, Profile> profiles;
    Profile factory;
    int active = -1;
};

static void load_all(Device& d, Loaded& L) {
    L.info.name = d.name();
    L.info.path = d.path();
    L.info.index = d.index();
    L.info.firmware = d.firmware();
    std::tie(L.info.battery, L.info.battery_state) = d.battery();
    L.info.onboard = d.onboard_info();
    L.info.mode = d.onboard_mode();
    L.info.dpi_list = d.dpi_list();
    try { L.info.surface = d.surface_get(); } catch (const HidppError&) { L.info.surface = -1; }
    L.info.features = d.features();
    L.dir_raw = d.read_sector(0);
    L.dir = parse_directory(L.dir_raw);
    for (auto& e : L.dir) L.profiles[e.sector] = Profile::parse(e.sector, d.read_sector(e.sector));
    L.factory = Profile::parse(0x101, d.read_sector(0x101));
    L.active = d.current_profile();
    try { L.info.stage = d.current_stage(); } catch (const HidppError&) { L.info.stage = 0; }
}

static void apply_loaded(App& app, Loaded& L) {
    app.info = std::move(L.info);
    app.dir_raw = std::move(L.dir_raw);
    app.dir = std::move(L.dir);
    app.profiles = std::move(L.profiles);
    app.factory = std::move(L.factory);
    app.active = L.active;
    app.dirty.clear();
    app.connected = true;
    if (!app.profiles.count(app.edit)) app.edit = app.profiles.count(app.active) ? app.active : app.dir.empty() ? -1 : app.dir[0].sector;
    fire(app.on_status);
    fire(app.on_dirty);
    app.reload_ui();
}

void App::connect() {
    if (p->connecting.exchange(true)) return;
    auto L = std::make_shared<Loaded>();
    std::lock_guard lk(p->mu);
    p->jobs.push_back({[L](Device& d) { load_all(d, *L); },
                       [this, L] { p->connecting = false; apply_loaded(*this, *L); },
                       [this](const std::string& e) {
                           p->connecting = false;
                           connected = false;
                           info.battery = -1;
                           info.battery_state = e;
                           fire(on_status);
                       },
                       true});
    p->cv.notify_one();
}

void App::revert() {
    if (!connected) return connect();
    auto L = std::make_shared<Loaded>();
    run([L](Device& d) { load_all(d, *L); }, [this, L] { apply_loaded(*this, *L); toast(_("Loaded from the mouse")); });
}

// ------------------------------------------------------------------ actions
void App::live_hits(int button) {
    Profile* pr = cur();
    if (!pr || edit != active || !connected) return;
    Hits h = pr->hits[button];
    run([button, h](Device& d) { d.hits_set(button, h); });
}

void App::activate_profile(int sector) {
    if (dirty.count(sector)) return toast(_("Save this profile first"));
    auto cur_sec = std::make_shared<int>(sector);
    run([cur_sec](Device& d) { d.set_current_profile(*cur_sec); *cur_sec = d.current_profile(); },
        [this, cur_sec] {
            active = *cur_sec;
            edit = active;
            fire(on_status);
            reload_ui();
            // TRANSLATORS: %d is the profile number (1 to 5)
            toast(fmt(_("Profile %d is now active"), slot_of(active) + 1));
        });
}

static bool has_left_click(const Profile& pr) {
    for (int i = 0; i < 5; i++) {
        const Action& a = pr.buttons[i];
        if ((a.kind() == Kind::Mouse && (a.code() & 1)) || (a.kind() == Kind::Default && i == 0)) return true;
    }
    return false;
}

static void do_save(App& app) {
    struct W { int sector; Bytes raw; };
    auto writes = std::make_shared<std::vector<W>>();
    for (int s : app.dirty)
        if (s != DIR_SECTOR) writes->push_back({s, app.profiles[s].encode()});
    auto dir_new = std::make_shared<Bytes>(app.dirty.count(DIR_SECTOR) ? encode_directory(app.dir_raw, app.dir) : Bytes{});
    bool backup = !app.p->backed_up;
    int active = app.active, stage = app.info.stage;
    gtk_widget_set_sensitive(app.p->save_btn, FALSE);
    adw_window_title_set_subtitle(app.p->title, _("Writing to the mouse memory…"));
    app.run(
        [writes, dir_new, backup, active, stage](Device& d) {
            if (backup) {
                std::string path = backup_dir() + "/auto-" + std::to_string(time(nullptr)) + ".json";
                std::ofstream(path) << backup_json(d);
            }
            for (auto& w : *writes) w.raw = d.write_sector(w.sector, w.raw);
            if (!dir_new->empty()) *dir_new = d.write_sector(0, *dir_new);
            for (auto& w : *writes)
                if (w.sector == active) {
                    d.set_current_profile(active);  // reload so DPI, rate and buttons apply now
                    g_usleep(100000);
                    Profile pr = Profile::parse(active, w.raw);
                    for (int b = 0; b < 2; b++) d.hits_set(b, pr.hits[b]);
                    if (pr.stages[stage].x) d.set_current_stage(stage);  // keep the stage in use (reload resets it)
                }
        },
        [&app, writes, dir_new] {
            app.p->backed_up = true;
            for (auto& w : *writes) app.profiles[w.sector] = Profile::parse(w.sector, w.raw);
            if (!dir_new->empty()) app.dir_raw = *dir_new;
            app.dirty.clear();
            fire(app.on_dirty);
            fire(app.on_status);
            app.reload_ui();
            app.toast(_("Saved in the mouse — it now works without this app"));
        },
        [&app](const std::string& e) {
            fire(app.on_dirty);
            fire(app.on_status);
            // TRANSLATORS: first %s is the error message, second %s is a folder path
            app.toast(fmt(_("Save failed: %s. Backups: %s"), e.c_str(), backup_dir().c_str()));
        });
}

void App::save() {
    if (!connected || dirty.empty()) return;
    for (int s : dirty) {
        if (s == DIR_SECTOR || has_left_click(profiles[s])) continue;
        AdwDialog* d = adw_alert_dialog_new(_("No button does Left click"),
                                            _("After saving, no button on this profile sends a left click. Save anyway?"));
        adw_alert_dialog_add_responses(ADW_ALERT_DIALOG(d), "cancel", _("Cancel"), "save", _("Save anyway"), nullptr);
        adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(d), "save", ADW_RESPONSE_DESTRUCTIVE);
        ui::on1<const char*>(d, "response", [this](const char* r) { if (!g_strcmp0(r, "save")) do_save(*this); });
        adw_dialog_present(d, GTK_WIDGET(window));
        return;
    }
    do_save(*this);
}

// ------------------------------------------------------------------ real mouse input (hidraw, Linux only)
#ifndef _WIN32
static void input_loop(App* app, int fd, int rid) {
    uint16_t last = 0;
    uint8_t buf[64];
    static const char* zones[5] = {"btn-left", "btn-right", "btn-middle", "btn-back", "btn-forward"};
    for (;;) {
        ssize_t n = read(fd, buf, sizeof buf);
        if (n <= 0) return;
        const uint8_t* d = buf;
        if (rid >= 0) {
            if (d[0] != rid) continue;
            d++, n--;
        }
        if (n < 7) continue;
        app->p->last_input = now_s();
        uint16_t b = d[0] | d[1] << 8;
        int wheel = (int8_t)d[6];
        if (b == last && !wheel) continue;
        uint16_t changed = b ^ last;
        last = b;
        ui::idle([app, b, changed, wheel] {
            for (int i = 0; i < 5; i++)
                if (changed & (1 << i)) fire(app->on_button, std::string(zones[i]), bool(b & (1 << i)));
            if (wheel) fire(app->on_scroll, double(-wheel));
        });
    }
}
#endif

static gboolean on_window_event(GtkEventControllerLegacy*, GdkEvent* ev, gpointer data) {
    App& app = *static_cast<App*>(data);
    if (!ev || now_s() - app.p->last_input < 20) return FALSE;  // hidraw already reports
    auto t = gdk_event_get_event_type(ev);
    if (t == GDK_BUTTON_PRESS || t == GDK_BUTTON_RELEASE) {
        static const std::map<int, const char*> z = {{1, "btn-left"}, {3, "btn-right"}, {2, "btn-middle"}, {8, "btn-back"}, {9, "btn-forward"}};
        auto it = z.find((int)gdk_button_event_get_button(ev));
        if (it != z.end()) fire(app.on_button, std::string(it->second), t == GDK_BUTTON_PRESS);
    } else if (t == GDK_SCROLL) {
        double dx = 0, dy = 0;
        auto dir = gdk_scroll_event_get_direction(ev);
        if (dir == GDK_SCROLL_SMOOTH) gdk_scroll_event_get_deltas(ev, &dx, &dy);
        else dy = dir == GDK_SCROLL_DOWN ? 1 : dir == GDK_SCROLL_UP ? -1 : 0;
        if (dy) fire(app.on_scroll, std::clamp(dy, -1.0, 1.0));
    }
    return FALSE;
}

static void window_input_fallback(App& app) {
    GtkEventController* c = gtk_event_controller_legacy_new();
    gtk_event_controller_set_propagation_phase(c, GTK_PHASE_CAPTURE);
    g_signal_connect(c, "event", G_CALLBACK(on_window_event), &app);
    gtk_widget_add_controller(GTK_WIDGET(app.window), c);
}

// ------------------------------------------------------------------ start / stop
void app_start(App& app) {
    app.p->worker = std::thread(worker_loop, &app);
#ifndef _WIN32
    for (auto& [path, rid] : find_mouse_input_nodes()) {
        int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
        if (fd >= 0) std::thread(input_loop, &app, fd, rid).detach();
    }
#endif
    window_input_fallback(app);
    ui::every(5000, [&app] {  // reconnect every 5 s, battery every 60 s
        if (!app.connected) app.connect();
        else if (++app.p->poll_n % 12 == 0) {
            auto b = std::make_shared<std::pair<int, std::string>>();
            app.run([b](Device& d) { *b = d.battery(); },
                    [&app, b] { std::tie(app.info.battery, app.info.battery_state) = *b; fire(app.on_status); },
                    [&app](const std::string&) { app.connected = false; fire(app.on_status); });
        }
        return true;
    });
    app.connect();
}

void app_stop(App& app) {
    {
        std::lock_guard lk(app.p->mu);
        app.p->jobs.push_back({});
        app.p->cv.notify_one();
    }
    if (app.p->worker.joinable()) app.p->worker.join();
}
