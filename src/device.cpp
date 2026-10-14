#include "device.hpp"
#include "i18n.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <thread>

namespace {

void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

std::string hex_id(uint16_t fid) {
    char b[8];
    std::snprintf(b, sizeof b, "%#06x", fid);
    return b;
}

Bytes sec_params(int sector, int off) {
    return {(uint8_t)(sector >> 8), (uint8_t)sector, (uint8_t)(off >> 8), (uint8_t)off};
}

// Bytes outside 7-bit ASCII stand for undecodable text, like Python's errors='replace'.
std::string clean_text(const uint8_t* p, size_t n) {
    std::string s;
    for (size_t i = 0; i < n; i++) {
        if (p[i] >= 0x80) s += "\xEF\xBF\xBD";
        else s += (char)p[i];
    }
    size_t a = s.find_first_not_of(std::string("\0 ", 2));
    if (a == std::string::npos) return "";
    return s.substr(a, s.find_last_not_of(std::string("\0 ", 2)) - a + 1);
}

std::string read_name(Device& d) {
    int n = d.call(F_NAME, 0)[0];
    Bytes name;
    while ((int)name.size() < n) {
        Bytes r = d.call(F_NAME, 1, {(uint8_t)name.size()});
        name.insert(name.end(), r.begin(), r.begin() + std::min<int>(16, n - (int)name.size()));
    }
    std::string s(name.begin(), name.end());
    s.erase(std::remove(s.begin(), s.end(), '\0'), s.end());
    size_t a = s.find_first_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

bool upper_has(const std::string& s, const std::string& key) {
    std::string u = s;
    for (char& c : u) c = (char)toupper((unsigned char)c);
    return u.find(key) != std::string::npos;
}

}  // namespace

uint16_t crc16(const uint8_t* data, size_t n) {
    uint16_t c = 0xFFFF;
    for (size_t i = 0; i < n; i++) {
        c ^= (uint16_t)(data[i] << 8);
        for (int k = 0; k < 8; k++) c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1);
    }
    return c;
}

bool sector_crc_ok(const Bytes& s) {
    return s.size() > 2 && crc16(s.data(), s.size() - 2) == (uint16_t)(s[s.size() - 2] << 8 | s.back());
}

Device::Device(std::unique_ptr<Transport> t, int dev) : t_(std::move(t)), dev_(dev) {}

std::unique_ptr<Device> Device::open() {
    std::string errors;
    for (const auto& path : find_hidpp_nodes()) {
        std::unique_ptr<Transport> t;
        try {
            t = std::make_unique<Transport>(path);
        } catch (const HidppError& e) {
            errors += (errors.empty() ? "" : "; ") + std::string(e.what());
            continue;
        }
        t->drain();
        for (int idx : {1, 2, 3, 4, 5, 6, 0xFF}) {
            bool alive = false;
            for (int wait : {600, 1500}) {  // a sleeping mouse needs a moment to wake up
                try {
                    t->request((uint8_t)idx, 0, 1, {0, 0, 0x5A}, wait, false);
                    alive = true;
                    break;
                } catch (const HidppError&) {}
            }
            if (!alive) continue;
            std::unique_ptr<Device> d(new Device(std::move(t), idx));
            try {
                d->name_ = read_name(*d);
                if (d->feature(F_HITS) || upper_has(d->name_, "SUPERSTRIKE")) return d;
            } catch (const HidppError& e) {
                errors += (errors.empty() ? "" : "; ") + path + "#" + std::to_string(idx) + ": " + e.what();
            }
            t = std::move(d->t_);
        }
    }
    throw HidppError("no SUPERSTRIKE mouse found" + (errors.empty() ? "" : ": " + errors));
}

Bytes Device::call(uint16_t fid, uint8_t func, const Bytes& params, int retries) {
    int idx = feature(fid);
    if (!idx) throw HidppError("feature " + hex_id(fid) + " not present");
    for (int attempt = 0;; attempt++) {
        try {
            return t_->request((uint8_t)dev_, (uint8_t)idx, func, params);
        } catch (const HidppError& e) {
            if (!e.timeout || attempt >= retries) throw;
            sleep_ms(40 * (attempt + 1));
        }
    }
}

int Device::feature(uint16_t fid) {
    auto it = feat_.find(fid);
    if (it != feat_.end()) return it->second;
    for (int attempt = 0;; attempt++) {
        try {
            Bytes r = t_->request((uint8_t)dev_, 0, 0, {(uint8_t)(fid >> 8), (uint8_t)fid});
            return feat_[fid] = r[0];
        } catch (const HidppError& e) {
            if (!e.timeout || attempt >= 2) throw;
            sleep_ms(50);
        }
    }
}

std::vector<FeatureEntry> Device::features() {
    int n = call(F_FEATURESET, 0)[0];
    std::vector<FeatureEntry> out;
    for (int i = 0; i <= n; i++) {
        Bytes r = call(F_FEATURESET, 1, {(uint8_t)i});
        out.push_back({i, (uint16_t)(r[0] << 8 | r[1]), r[2], r[3]});
    }
    return out;
}

std::vector<std::string> Device::firmware() {
    static const std::map<int, const char*> kinds = {
        {0, NC_("firmware part", "Main")}, {1, NC_("firmware part", "Bootloader")}, {2, NC_("firmware part", "Hardware")},
        {3, NC_("firmware part", "Touchpad")}, {4, NC_("firmware part", "Optical sensor")},
        {5, NC_("firmware part", "Softdevice")}, {6, "RF"}, {14, NC_("firmware part", "Sub-module")},
        {15, NC_("firmware part", "Module")}};
    std::vector<std::string> out;
    int cnt = call(F_FWINFO, 0)[0];
    for (int e = 0; e < cnt; e++) {
        Bytes r = call(F_FWINFO, 1, {(uint8_t)e});
        auto k = kinds.find(r[0] & 0x0F);
        // TRANSLATORS: unknown firmware part, %d is its numeric type
        std::string kind = k != kinds.end() ? C_("firmware part", k->second) : fmt(_("Type %d"), (int)r[0]);
        char ver[24];
        std::snprintf(ver, sizeof ver, "%02X.%02X.B%02X%02X", r[4], r[5], r[6], r[7]);
        out.push_back(kind + ": " + clean_text(&r[1], 3) + " " + ver);
    }
    return out;
}

std::pair<int, std::string> Device::battery() {
    static const char* const states[] = {"discharging", "charging", "charging (slow)", "full", "charge error"};
    Bytes r = call(F_BATTERY, 1);
    return {r[0], r[2] < 5 ? states[r[2]] : "state " + std::to_string(r[2])};
}

// The state names stay English inside the app (the window compares them); only the display is translated.
std::string battery_state_text(const std::string& state) {
    if (state == "discharging") return _("discharging");
    if (state == "charging") return _("charging");
    if (state == "charging (slow)") return _("charging (slow)");
    if (state == "full") return _("full");
    if (state == "charge error") return _("charge error");
    // TRANSLATORS: unknown battery state, %s is its numeric code
    if (state.rfind("state ", 0) == 0) return fmt(_("state %s"), state.c_str() + 6);
    return state;
}

Hits Device::hits_get(int button) {
    Bytes r = call(F_HITS, 2, {(uint8_t)button});
    return {r[1] >> 2, r[2] >> 2, (r[2] & 1) != 0, r[3] >> 2};
}

void Device::hits_set(int button, const Hits& h) {
    call(F_HITS, 1, {(uint8_t)button, (uint8_t)((h.act << 2) & 0xFC),
                     (uint8_t)(((h.rt << 2) & 0xFC) | (h.rt_on ? 1 : 0)), (uint8_t)((h.hap << 2) & 0xFC)});
}

std::vector<int> Device::rate_supported() {
    Bytes r = call(F_RATE, 1);
    int mask = r[0] << 8 | r[1];
    std::vector<int> out;
    for (int i = 0; i < 7; i++)
        if (mask & (1 << i)) out.push_back(i);
    return out;
}

int Device::rate_get() { return call(F_RATE, 2)[0]; }
void Device::rate_set(int idx) { call(F_RATE, 3, {(uint8_t)idx}); }

std::vector<int> Device::dpi_list() {
    Bytes raw;
    std::vector<int> words;
    for (int page = 0; page < 8; page++) {
        Bytes r = call(F_DPI, 2, {0, 0, (uint8_t)page});
        raw.insert(raw.end(), r.begin() + 3, r.begin() + 16);
        words.clear();
        for (size_t i = 0; i + 1 < raw.size(); i += 2) words.push_back(raw[i] << 8 | raw[i + 1]);
        if (std::find(words.begin(), words.end(), 0) != words.end()) break;
    }
    std::vector<int> out;
    for (size_t i = 0; i < words.size() && words[i];) {
        int w = words[i];
        if ((w >> 13) == 7 && !out.empty() && i + 1 < words.size()) {
            int step = w & 0x1FFF, end = words[i + 1];
            for (int v = out.back() + step; step > 0 && v <= end; v += step) out.push_back(v);
            i += 2;
        } else {
            out.push_back(w);
            i++;
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

int Device::surface_get() { return (call(F_MODESTATUS, 0)[1] >> 1) & 3; }
void Device::surface_set(int mode) { call(F_MODESTATUS, 1, {0, (uint8_t)((mode & 3) << 1), 0, 0x06}); }

OnboardInfo Device::onboard_info() {
    Bytes r = call(F_ONBOARD, 0);
    return {r[1], r[3], r[5], r[6], r[7] << 8 | r[8]};
}

int Device::onboard_mode() { return call(F_ONBOARD, 2)[0]; }
void Device::set_onboard_mode(int mode) { call(F_ONBOARD, 1, {(uint8_t)mode}); }

int Device::current_profile() {
    Bytes r = call(F_ONBOARD, 4);
    return r[0] << 8 | r[1];
}

void Device::set_current_profile(int sector) {
    for (int attempt = 0;; attempt++) {
        try {
            call(F_ONBOARD, 3, {(uint8_t)(sector >> 8), (uint8_t)sector}, 0);
            return;
        } catch (const HidppError& e) {
            if (!e.timeout || attempt >= 3) throw;
            sleep_ms(150);
        }
    }
}

int Device::current_stage() { return call(F_ONBOARD, 11)[0]; }
void Device::set_current_stage(int stage) { call(F_ONBOARD, 12, {(uint8_t)stage}); }

Bytes Device::read_sector(int sector, int size) {
    for (int attempt = 0; attempt < 3; attempt++) {
        Bytes out;
        int off = 0;
        for (; off + 16 <= size; off += 16) {
            Bytes c = call(F_ONBOARD, 5, sec_params(sector, off));
            out.insert(out.end(), c.begin(), c.begin() + 16);
        }
        if (off < size) {
            Bytes c = call(F_ONBOARD, 5, sec_params(sector, size - 16));
            out.insert(out.end(), c.begin() + 16 - (size - off), c.begin() + 16);
        }
        if (sector >= 0x100 || sector_crc_ok(out)) return out;
    }
    throw HidppError("sector " + hex_id((uint16_t)sector) + ": CRC mismatch after 3 reads");
}

Bytes Device::write_sector(int sector, Bytes data) {
    const int n = (int)data.size();
    uint16_t c = crc16(data.data(), data.size() - 2);
    data[n - 2] = (uint8_t)(c >> 8);
    data[n - 1] = (uint8_t)c;

    auto step = [&](uint8_t func, const Bytes& params) {
        try {
            call(F_ONBOARD, func, params, 0);
        } catch (const HidppError& e) {
            if (!e.timeout) throw;
        }
    };
    step(6, {(uint8_t)(sector >> 8), (uint8_t)sector, 0, 0, (uint8_t)(n >> 8), (uint8_t)n});
    for (int off = 0; off < n; off += 16)
        step(7, Bytes(data.begin() + off, data.begin() + std::min(off + 16, n)));
    step(8, {});
    sleep_ms(50);
    if (read_sector(sector, n) != data)
        throw HidppError("sector " + hex_id((uint16_t)sector) + ": verification failed after write");
    return data;
}
