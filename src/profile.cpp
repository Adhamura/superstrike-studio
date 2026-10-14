#include "profile.hpp"

#include "i18n.hpp"

#include <glib.h>
#include <json-glib/json-glib.h>

#include <algorithm>
#include <cstdio>
#include <memory>

static std::string name_of(const std::vector<Named>& table, int code, const std::string& fallback) {
    for (const auto& n : table)
        if (n.code == code) return named_text(table, n);
    return fallback;
}

Kind Action::kind() const {
    if (raw == std::array<uint8_t, 4>{0xFF, 0xFF, 0xFF, 0xFF}) return Kind::Default;
    if (raw[0] >> 4 == 0x8) {
        switch (raw[1]) {
            case 0x00: return Kind::Disabled;
            case 0x01: return Kind::Mouse;
            case 0x02: return Kind::Key;
            case 0x03: return Kind::Media;
        }
    } else if (raw[0] >> 4 == 0x9) {
        return Kind::Function;
    }
    return Kind::Raw;
}

int Action::code() const {
    switch (kind()) {
        case Kind::Mouse: case Kind::Media: return raw[2] << 8 | raw[3];
        case Kind::Key: return raw[3];
        case Kind::Function: return raw[1];
        default: return 0;
    }
}

int Action::mods() const { return kind() == Kind::Key ? raw[2] : 0; }

Action Action::make(Kind k, int code, int mods) {
    Action a;
    const uint8_t hi = (uint8_t)(code >> 8), lo = (uint8_t)code;
    switch (k) {
        case Kind::Mouse: a.raw = {0x80, 0x01, hi, lo}; break;
        case Kind::Key: a.raw = {0x80, 0x02, (uint8_t)mods, lo}; break;
        case Kind::Media: a.raw = {0x80, 0x03, hi, lo}; break;
        case Kind::Function: a.raw = {0x90, lo, 0xFF, 0x00}; break;
        case Kind::Default: break;
        default: a.raw = {0x80, 0x00, 0xFF, 0xFF};
    }
    return a;
}

std::string Action::describe() const {
    switch (kind()) {
        // TRANSLATORS: a mouse button the app does not know; %04x is its hex bit mask
        case Kind::Mouse: return name_of(MOUSE_BUTTONS, code(), fmt(_("Mouse mask 0x%04x"), code()));
        case Kind::Key: {
            std::string s;
            for (const auto& m : MODIFIERS)
                if (mods() & m.code) s += std::string(named_text(MODIFIERS, m)) + "+";
            // TRANSLATORS: a keyboard key the app does not know; %02x is its hex HID usage code
            return s + name_of(KEYS, code(), fmt(_("key 0x%02x"), code()));
        }
        // TRANSLATORS: a media key the app does not know; %04x is its hex usage code
        case Kind::Media: return name_of(MEDIA, code(), fmt(_("Consumer 0x%04x"), code()));
        // TRANSLATORS: a mouse function the app does not know; %02x is its hex code
        case Kind::Function: return name_of(FUNCTIONS, code(), fmt(_("Function 0x%02x"), code()));
        case Kind::Disabled: return C_("button action", "Disabled");
        case Kind::Default: return _("Factory default");
        default: break;
    }
    char b[40];
    std::snprintf(b, sizeof b, "%02x %02x %02x %02x", raw[0], raw[1], raw[2], raw[3]);
    // TRANSLATORS: %s is the raw data as hex bytes
    return fmt(_("Macro / custom (%s)"), b);
}

static constexpr size_t SECTOR_SIZE = 255;

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

static std::string decode_name(const Bytes& r) {
    std::vector<gunichar2> u;
    for (size_t i = 0xA0; i < 0xD0 && (r[i] | r[i + 1] << 8) != 0 && (r[i] & r[i + 1]) != 0xFF; i += 2)
        u.push_back((gunichar2)(r[i] | r[i + 1] << 8));
    if (u.empty()) return "";
    gchar* t = g_utf16_to_utf8(u.data(), (glong)u.size(), nullptr, nullptr, nullptr);
    if (!t) {  // unpaired surrogate: show it as U+FFFD
        for (gunichar2& c : u)
            if (c >= 0xD800 && c < 0xE000) c = 0xFFFD;
        t = g_utf16_to_utf8(u.data(), (glong)u.size(), nullptr, nullptr, nullptr);
    }
    std::string out = t ? t : "";
    g_free(t);
    return trim(out);
}

static Bytes encode_name(const std::string& name) {
    gchar* valid = g_utf8_make_valid(name.c_str(), -1);
    glong n = 0;
    gunichar2* u = g_utf8_to_utf16(valid, -1, nullptr, &n, nullptr);
    g_free(valid);
    if (n > 24) n = (u[23] >= 0xD800 && u[23] < 0xDC00) ? 23 : 24;  // never split a surrogate pair
    Bytes out(48, 0);
    for (glong i = 0; i < n; i++) {
        out[2 * i] = (uint8_t)u[i];
        out[2 * i + 1] = (uint8_t)(u[i] >> 8);
    }
    g_free(u);
    return out;
}

Profile Profile::parse(int sector, const Bytes& raw_in) {
    Profile p;
    p.sector = sector;
    p.raw = raw_in;
    if (p.raw.size() < SECTOR_SIZE) p.raw.resize(SECTOR_SIZE, 0xFF);
    const Bytes& r = p.raw;
    p.rate_wireless = r[0];
    p.rate_wired = r[1];
    p.default_stage = r[2];
    p.shift_stage = r[3];
    for (int n = 0; n < 5; n++) {
        size_t o = 4 + 5 * n;
        p.stages[n] = {r[o] | r[o + 1] << 8, r[o + 2] | r[o + 3] << 8, r[o + 4]};
    }
    p.angle_snap = r[0x22];
    p.scroll_filter = r[0x25] == 0xFF ? 0 : r[0x25];
    for (int b = 0; b < 2; b++) {
        size_t o = 0x26 + 3 * b;
        p.hits[b] = {r[o] >> 2, r[o + 1] >> 2, (r[o + 1] & 1) != 0, r[o + 2] >> 2};
    }
    p.sleep_s = r[0x2C] | r[0x2D] << 8;
    p.off_s = r[0x2E] | r[0x2F] << 8;
    for (size_t i = 0; i < 16; i++) std::copy_n(&r[0x30 + 4 * i], 4, p.buttons[i].raw.begin());
    for (size_t i = 0; i < 12; i++) std::copy_n(&r[0x70 + 4 * i], 4, p.shift_buttons[i].raw.begin());
    p.name = decode_name(r);
    return p;
}

Bytes Profile::encode() const {
    Bytes r = raw;
    if (r.size() < SECTOR_SIZE) r.resize(SECTOR_SIZE, 0xFF);
    auto u8 = [](int v) { return (uint8_t)v; };
    r[0] = u8(rate_wireless);
    r[1] = u8(rate_wired);
    r[2] = u8(default_stage);
    r[3] = u8(shift_stage);
    for (int n = 0; n < 5; n++) {
        size_t o = 4 + 5 * n;
        const Stage& s = stages[n];
        r[o] = u8(s.x); r[o + 1] = u8(s.x >> 8); r[o + 2] = u8(s.y); r[o + 3] = u8(s.y >> 8);
        r[o + 4] = u8(s.lod);
    }
    r[0x22] = u8(angle_snap);
    r[0x25] = scroll_filter ? u8(scroll_filter) : (raw.size() > 0x25 && raw[0x25] == 0xFF ? 0xFF : 0);
    for (int b = 0; b < 2; b++) {
        size_t o = 0x26 + 3 * b;
        const Hits& h = hits[b];
        r[o] = u8((h.act << 2) | (r[o] & 3));
        r[o + 1] = u8((h.rt << 2) | (r[o + 1] & 2) | (h.rt_on ? 1 : 0));
        r[o + 2] = u8((h.hap << 2) | (r[o + 2] & 3));
    }
    r[0x2C] = u8(sleep_s); r[0x2D] = u8(sleep_s >> 8);
    r[0x2E] = u8(off_s); r[0x2F] = u8(off_s >> 8);
    for (size_t i = 0; i < 16; i++) std::copy(buttons[i].raw.begin(), buttons[i].raw.end(), &r[0x30 + 4 * i]);
    for (size_t i = 0; i < 12; i++) std::copy(shift_buttons[i].raw.begin(), shift_buttons[i].raw.end(), &r[0x70 + 4 * i]);
    if (name != decode_name(r)) {
        Bytes n = encode_name(name);
        std::copy(n.begin(), n.end(), r.begin() + 0xA0);
    }
    return r;
}

namespace {

using JsonPtr = std::unique_ptr<JsonGenerator, decltype(&g_object_unref)>;

void add_int(JsonBuilder* b, const char* key, int v) {
    json_builder_set_member_name(b, key);
    json_builder_add_int_value(b, v);
}

void add_hex_array(JsonBuilder* b, const char* key, const Action* acts, size_t n) {
    json_builder_set_member_name(b, key);
    json_builder_begin_array(b);
    for (size_t i = 0; i < n; i++) {
        char h[9];
        std::snprintf(h, sizeof h, "%02x%02x%02x%02x", acts[i].raw[0], acts[i].raw[1], acts[i].raw[2], acts[i].raw[3]);
        json_builder_add_string_value(b, h);
    }
    json_builder_end_array(b);
}

bool is_type(JsonNode* n, GType t) {
    return n && JSON_NODE_HOLDS_VALUE(n) && json_node_get_value_type(n) == t;
}

bool get_int(JsonObject* o, const char* key, int lo, int hi, int& out, bool& ok) {
    JsonNode* n = json_object_get_member(o, key);
    if (!n) return false;
    if (!is_type(n, G_TYPE_INT64) || json_node_get_int(n) < lo || json_node_get_int(n) > hi) return ok = false;
    out = (int)json_node_get_int(n);
    return true;
}

bool parse_hex_action(JsonNode* n, Action& a) {
    if (!is_type(n, G_TYPE_STRING)) return false;
    std::string s = json_node_get_string(n);
    if (s.size() != 8 || s.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) return false;
    for (int i = 0; i < 4; i++) a.raw[i] = (uint8_t)std::stoi(s.substr(2 * i, 2), nullptr, 16);
    return true;
}

// False when the member exists but is not an array of at most max entries.
bool member_array(JsonObject* o, const char* key, size_t max, JsonArray*& out) {
    JsonNode* n = json_object_get_member(o, key);
    if (!n) return true;
    if (!JSON_NODE_HOLDS_ARRAY(n) || json_array_get_length(json_node_get_array(n)) > max) return false;
    out = json_node_get_array(n);
    return true;
}

JsonObject* element_object(JsonArray* a, guint i) {
    JsonNode* e = json_array_get_element(a, i);
    return JSON_NODE_HOLDS_OBJECT(e) ? json_node_get_object(e) : nullptr;
}

template <size_t N>
void read_actions(JsonObject* o, const char* key, std::array<Action, N>& dst, bool& ok) {
    JsonArray* a = nullptr;
    ok = ok && member_array(o, key, N, a);
    for (guint i = 0; ok && a && i < json_array_get_length(a); i++) ok = parse_hex_action(json_array_get_element(a, i), dst[i]);
}

}  // namespace

std::string Profile::to_json() const {
    std::unique_ptr<JsonBuilder, decltype(&g_object_unref)> bp(json_builder_new(), g_object_unref);
    JsonBuilder* b = bp.get();
    auto open_array = [&](const char* key) {
        json_builder_set_member_name(b, key);
        json_builder_begin_array(b);
    };
    json_builder_begin_object(b);
    json_builder_set_member_name(b, "name");
    json_builder_add_string_value(b, name.c_str());
    for (auto [key, v] : {std::pair{"rate_wireless", rate_wireless}, {"rate_wired", rate_wired},
                          {"default_stage", default_stage}, {"shift_stage", shift_stage}})
        add_int(b, key, v);
    open_array("stages");
    for (const Stage& s : stages) {
        json_builder_begin_object(b);
        for (auto [key, v] : {std::pair{"x", s.x}, {"y", s.y}, {"lod", s.lod}}) add_int(b, key, v);
        json_builder_end_object(b);
    }
    json_builder_end_array(b);
    add_int(b, "angle_snap", angle_snap);
    add_int(b, "scroll_filter", scroll_filter);
    open_array("hits");
    for (const Hits& h : hits) {
        json_builder_begin_object(b);
        add_int(b, "act", h.act);
        add_int(b, "rt", h.rt);
        json_builder_set_member_name(b, "rt_on");
        json_builder_add_boolean_value(b, h.rt_on);
        add_int(b, "hap", h.hap);
        json_builder_end_object(b);
    }
    json_builder_end_array(b);
    add_int(b, "sleep_s", sleep_s);
    add_int(b, "off_s", off_s);
    add_hex_array(b, "buttons", buttons.data(), buttons.size());
    add_hex_array(b, "shift_buttons", shift_buttons.data(), shift_buttons.size());
    json_builder_end_object(b);

    JsonNode* root = json_builder_get_root(b);
    JsonPtr gen(json_generator_new(), g_object_unref);
    g_object_set(gen.get(), "pretty", TRUE, "indent", 2, nullptr);
    json_generator_set_root(gen.get(), root);
    gchar* text = json_generator_to_data(gen.get(), nullptr);
    std::string out = text;
    g_free(text);
    json_node_unref(root);
    return out;
}

bool Profile::from_json(const std::string& json) {
    std::unique_ptr<JsonParser, decltype(&g_object_unref)> parser(json_parser_new(), g_object_unref);
    if (!json_parser_load_from_data(parser.get(), json.c_str(), (gssize)json.size(), nullptr)) return false;
    JsonNode* root = json_parser_get_root(parser.get());
    if (!root || !JSON_NODE_HOLDS_OBJECT(root)) return false;
    JsonObject* o = json_node_get_object(root);

    Profile p = *this;
    bool ok = true;
    if (JsonNode* n = json_object_get_member(o, "name")) {
        if (!is_type(n, G_TYPE_STRING) || !g_utf8_validate(json_node_get_string(n), -1, nullptr)) return false;
        p.name = json_node_get_string(n);
    }
    const struct { const char* key; int lo, hi; int* dst; } ints[] = {
        {"rate_wireless", 0, 6, &p.rate_wireless}, {"rate_wired", 0, 6, &p.rate_wired},
        {"default_stage", 0, 4, &p.default_stage}, {"shift_stage", 0, 4, &p.shift_stage},
        {"angle_snap", 0, 255, &p.angle_snap}, {"scroll_filter", 0, 255, &p.scroll_filter},
        {"sleep_s", 0, 0xFFFF, &p.sleep_s}, {"off_s", 0, 0xFFFF, &p.off_s}};
    for (const auto& i : ints) get_int(o, i.key, i.lo, i.hi, *i.dst, ok);

    JsonArray* arr = nullptr;
    if (!member_array(o, "stages", p.stages.size(), arr)) return false;
    for (guint i = 0; arr && i < json_array_get_length(arr); i++) {
        JsonObject* so = element_object(arr, i);
        if (!so) return false;
        get_int(so, "x", 0, 0xFFFF, p.stages[i].x, ok);
        get_int(so, "y", 0, 0xFFFF, p.stages[i].y, ok);
        get_int(so, "lod", 0, 3, p.stages[i].lod, ok);
    }
    arr = nullptr;
    if (!member_array(o, "hits", p.hits.size(), arr)) return false;
    for (guint i = 0; arr && i < json_array_get_length(arr); i++) {
        JsonObject* ho = element_object(arr, i);
        if (!ho) return false;
        get_int(ho, "act", 0, 63, p.hits[i].act, ok);
        get_int(ho, "rt", 0, 63, p.hits[i].rt, ok);
        get_int(ho, "hap", 0, 63, p.hits[i].hap, ok);
        if (JsonNode* on = json_object_get_member(ho, "rt_on")) {
            if (!is_type(on, G_TYPE_BOOLEAN)) return false;
            p.hits[i].rt_on = json_node_get_boolean(on);
        }
    }
    read_actions(o, "buttons", p.buttons, ok);
    read_actions(o, "shift_buttons", p.shift_buttons, ok);
    if (!ok) return false;
    *this = std::move(p);
    return true;
}

std::vector<DirEntry> parse_directory(const Bytes& raw) {
    std::vector<DirEntry> out;
    for (size_t i = 0; i + 3 < raw.size(); i += 4) {
        int sec = raw[i] << 8 | raw[i + 1];
        if (sec == 0xFFFF) break;
        out.push_back({sec, raw[i + 2] == 1});
    }
    return out;
}

Bytes encode_directory(const Bytes& raw, const std::vector<DirEntry>& entries) {
    Bytes r = raw;
    r.resize(std::max(r.size(), 4 * entries.size()), 0xFF);
    for (size_t i = 0; i < entries.size(); i++) {
        const DirEntry& e = entries[i];
        r[4 * i] = (uint8_t)(e.sector >> 8);
        r[4 * i + 1] = (uint8_t)e.sector;
        r[4 * i + 2] = e.enabled ? 1 : 0;
        r[4 * i + 3] = 0xFF;
    }
    size_t end = 4 * entries.size();
    if (end + 1 < r.size() && !(r[end] == 0xFF && r[end + 1] == 0xFF)) r[end] = r[end + 1] = 0xFF;
    return r;
}

bool hits_code_decode(const std::string& code, std::array<Hits, 2>& out, std::string* err) {
    auto fail = [&](const char* m) {
        if (err) *err = m;
        return false;
    };
    std::string c = trim(code);
    static const char* const b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=";
    if (c.empty() || c.size() % 4 || c.find_first_not_of(b64) != std::string::npos) return fail(_("not a HITS code"));
    gsize len = 0;
    std::unique_ptr<guchar, decltype(&g_free)> dec(g_base64_decode(c.c_str(), &len), g_free);
    std::string s((const char*)dec.get(), len);
    if (s.size() != 16 || s.find_first_not_of("0123456789") != std::string::npos || s.compare(0, 2, "00") != 0)
        return fail(_("not a HITS code (unknown version or length)"));
    auto num = [&](int pos) { return (s[pos] - '0') * 10 + (s[pos + 1] - '0'); };
    std::array<Hits, 2> h;
    for (int b = 0; b < 2; b++) {
        h[b] = {num(2 + 2 * b), num(6 + 2 * b), s[10 + b] == '1', num(12 + 2 * b)};
        if (h[b].act < 1 || h[b].act > 10 || h[b].rt < 1 || h[b].rt > 5 || h[b].hap > 5)
            return fail(_("HITS code value out of range"));
    }
    out = h;
    return true;
}

std::string hits_code_encode(const std::array<Hits, 2>& h) {
    char s[24];
    std::snprintf(s, sizeof s, "00%02d%02d%02d%02d%d%d%02d%02d", h[0].act, h[1].act, h[0].rt, h[1].rt, h[0].rt_on, h[1].rt_on,
                  h[0].hap, h[1].hap);
    gchar* b = g_base64_encode((const guchar*)s, strlen(s));
    std::string out = b;
    g_free(b);
    return out;
}

const std::vector<Preset> PRO_PRESETS = {
    {"m0NESY (CS2)", "MDAwNjA2MDEwMTExMDUwMw=="}, {"INS (CS2)", "MDAwNDA1MDMwNTEwMDQwMw=="},
    {"KSCERATO (CS2)", "MDAwMjAyMDMwMjExMDMwMw=="}, {"Coach Joe (Valorant)", "MDAwMjAyMDIwMjExMDQwMw=="},
    {"McFaliN (Valorant)", "MDAwNDA1MDIwMjExMDMwNQ=="}, {"Yetujey (Valorant)", "MDAwNDA0MDMwMzExMDIwMg=="},
    {"ShowMaker (LoL)", "MDAwMjAyMDEwMTExMDIwMg=="}, {"Siwoo (LoL)", "MDAwMjAyMDEwMTExMDMwMw=="},
    {"Lucid (LoL)", "MDAwNTAxMDEwMTExMDMwMw=="}, {"Smash (LoL)", "MDAwMjAyMDIwMjExMDEwMQ=="},
    {"Career (LoL)", "MDAwMjAyMDIwMjExMDQwNA=="}, {"Blaz (R6)", "MDAwNjA2MDMwMzExMDAwMA=="},
};

const std::vector<Preset> BUILTIN_PRESETS = {
    {NC_("HITS preset", "Factory default"), "MDAwNTA1MDIwMjAwMDMwMw=="},
    {NC_("HITS preset", "Hair trigger"), "MDAwMTAxMDEwMTExMDIwMg=="},
    {NC_("HITS preset", "Fast FPS"), "MDAwMzAzMDIwMjExMDMwMw=="},
    {NC_("HITS preset", "Balanced"), "MDAwNTA1MDMwMzAwMDMwMw=="},
    {NC_("HITS preset", "Deep press"), "MDAwODA4MDQwNDAwMDQwNA=="},
    {NC_("HITS preset", "Strong feedback"), "MDAwNTA1MDIwMjAwMDUwNQ=="},
    {NC_("HITS preset", "Silent feel"), "MDAwNTA1MDIwMjAwMDAwMA=="},
};
