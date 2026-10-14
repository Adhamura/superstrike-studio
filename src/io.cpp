// Config file and whole-memory backups.
#include <algorithm>
#include <ctime>
#include <glib/gstdio.h>
#include <json-glib/json-glib.h>
#include "app.hpp"
#include "i18n.hpp"

static std::string config_path() {
    return std::string(g_get_user_config_dir()) + "/superstrike/studio.ini";
}

void Config::load() {
    GKeyFile* kf = g_key_file_new();
    if (g_key_file_load_from_file(kf, config_path().c_str(), G_KEY_FILE_NONE, nullptr)) {
        auto get = [&](const char* g, const char* k, std::string& out) {
            if (gchar* v = g_key_file_get_string(kf, g, k, nullptr)) { out = v; g_free(v); }
        };
        get("look", "shell", shell);
        get("look", "accent", accent);
        get("look", "lang", lang);
        presets.clear();
        gsize n = 0;
        if (gchar** keys = g_key_file_get_keys(kf, "presets", &n, nullptr)) {
            for (gsize i = 0; i < n; i++) {
                std::string code;
                get("presets", keys[i], code);
                presets.emplace_back(keys[i], code);
            }
            g_strfreev(keys);
        }
    }
    g_key_file_free(kf);
}

void Config::save() const {
    GKeyFile* kf = g_key_file_new();
    g_key_file_set_string(kf, "look", "shell", shell.c_str());
    g_key_file_set_string(kf, "look", "accent", accent.c_str());
    g_key_file_set_string(kf, "look", "lang", lang.c_str());
    for (auto& [name, code] : presets) g_key_file_set_string(kf, "presets", name.c_str(), code.c_str());
    std::string path = config_path();
    gchar* dir = g_path_get_dirname(path.c_str());
    g_mkdir_with_parents(dir, 0700);
    g_free(dir);
    g_key_file_save_to_file(kf, path.c_str(), nullptr);
    g_key_file_free(kf);
}

std::string backup_dir() {
    std::string d = std::string(g_get_user_data_dir()) + "/superstrike/backups";
    g_mkdir_with_parents(d.c_str(), 0700);
    return d;
}

static std::string hex(const Bytes& b) {
    static const char* h = "0123456789abcdef";
    std::string s;
    for (uint8_t c : b) { s += h[c >> 4]; s += h[c & 15]; }
    return s;
}

std::string backup_json(Device& d) {
    std::vector<int> secs = {0};
    for (auto& e : parse_directory(d.read_sector(0))) secs.push_back(e.sector);
    secs.push_back(0x100);
    secs.push_back(0x101);
    JsonBuilder* b = json_builder_new();
    json_builder_begin_object(b);
    json_builder_set_member_name(b, "device");
    json_builder_add_string_value(b, d.name().c_str());
    char when[32];
    time_t t = time(nullptr);
    strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", localtime(&t));
    json_builder_set_member_name(b, "time");
    json_builder_add_string_value(b, when);
    json_builder_set_member_name(b, "sectors");
    json_builder_begin_object(b);
    for (int s : secs) {
        char key[8];
        snprintf(key, sizeof key, "0x%04x", s);
        json_builder_set_member_name(b, key);
        json_builder_add_string_value(b, hex(d.read_sector(s)).c_str());
    }
    json_builder_end_object(b);
    json_builder_end_object(b);
    JsonGenerator* g = json_generator_new();
    json_generator_set_pretty(g, TRUE);
    JsonNode* root = json_builder_get_root(b);
    json_generator_set_root(g, root);
    gchar* text = json_generator_to_data(g, nullptr);
    std::string out = text;
    g_free(text);
    json_node_unref(root);
    g_object_unref(g);
    g_object_unref(b);
    return out;
}

bool restore_json(Device& d, const std::string& json, std::string* err) {
    JsonParser* p = json_parser_new();
    std::vector<std::pair<int, Bytes>> secs;
    bool ok = json_parser_load_from_data(p, json.c_str(), -1, nullptr);
    JsonNode* root = ok ? json_parser_get_root(p) : nullptr;
    JsonObject* o = root && JSON_NODE_HOLDS_OBJECT(root) ? json_node_get_object(root) : nullptr;
    JsonObject* so = o && json_object_has_member(o, "sectors") ? json_object_get_object_member(o, "sectors") : nullptr;
    if (so) {
        GList* members = json_object_get_members(so);
        for (GList* m = members; m; m = m->next) {
            int sec = (int)strtol((const char*)m->data, nullptr, 16);
            const char* h = json_object_get_string_member(so, (const char*)m->data);
            Bytes raw;
            for (size_t i = 0; h && h[i] && h[i + 1]; i += 2) raw.push_back((uint8_t)strtol(std::string(h + i, 2).c_str(), nullptr, 16));
            if (sec < 0x100 && raw.size() >= 64 && sector_crc_ok(raw)) secs.emplace_back(sec, raw);
        }
        g_list_free(members);
    }
    g_object_unref(p);
    if (secs.empty()) {
        if (err) *err = _("not a SUPERSTRIKE backup file");
        return false;
    }
    std::sort(secs.begin(), secs.end(), [](auto& a, auto& b) { return a.first > b.first; });  // directory last
    for (auto& [sec, raw] : secs)
        if (d.read_sector(sec, (int)raw.size()) != raw) d.write_sector(sec, raw);
    d.set_current_profile(d.current_profile());
    return true;
}
