#include "hid_sim.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <map>
#include <mutex>

#include "profile.hpp"

namespace {

constexpr uint8_t SLOT = 1;  // receiver slot the demo mouse answers on
constexpr const char* NAME = "PRO X3 SUPERSTRIKE";
// feature index -> id, as the real mouse lists them (plus a few it has that the app does not use)
constexpr uint16_t FEATURES[] = {F_ROOT, F_FEATURESET, F_FWINFO, F_NAME, 0x0020, 0x1D4B, F_BATTERY,
                                 F_DPI, F_RATE, F_MODESTATUS, F_HITS, F_ONBOARD, 0x00C3};

struct Hit { int act, rt; bool on; int hap; };

Bytes with_crc(Bytes r) {
    uint16_t c = crc16(r.data(), r.size() - 2);
    r[r.size() - 2] = (uint8_t)(c >> 8);
    r[r.size() - 1] = (uint8_t)c;
    return r;
}

Bytes sector_bytes(const Profile& p) { return with_crc(p.encode()); }

Profile make(const char* name, std::initializer_list<int> dpi, int def_stage, int rate, Hit l, Hit r) {
    Profile p;
    p.name = name;
    int n = 0;
    for (int d : dpi) p.stages[n++] = {d, d, 1};
    p.default_stage = def_stage;
    p.rate_wireless = rate;
    p.rate_wired = 3;
    p.hits[0] = {l.act, l.rt, l.on, l.hap};
    p.hits[1] = {r.act, r.rt, r.on, r.hap};
    for (int i = 0; i < 5; i++) p.buttons[i] = Action::make(Kind::Mouse, 1 << i);
    return p;
}

struct Sim {
    std::mutex mu;
    std::map<int, Bytes> sectors;
    Hits hits[2];
    int profile = 1, stage = 1, rate = 6, surface = 0, mode = MODE_ONBOARD;
    int write_sector = -1;
    Bytes write_buf;

    Sim() {
        Profile p[5] = {
            make("Tactical FPS", {400, 800, 1600, 3200}, 1, 6, {2, 1, true, 4}, {4, 2, false, 3}),
            make("Arena FPS", {800, 1600, 3200}, 1, 6, {1, 1, true, 5}, {3, 1, true, 4}),
            make("Battle royale", {800, 1200, 1600, 3200, 6400}, 2, 5, {3, 2, true, 3}, {5, 2, false, 3}),
            make("MMO / MOBA", {1200, 2400, 3200}, 1, 4, {5, 2, false, 2}, {5, 2, false, 2}),
            make("Desktop", {1600, 3200}, 0, 3, {6, 3, false, 1}, {6, 3, false, 1}),
        };
        p[1].stages[0].y = 720;  // separate X / Y on one stage
        p[2].angle_snap = 1;
        p[3].buttons[3] = Action::make(Kind::Key, 0x1E);  // Back: 1
        p[3].buttons[4] = Action::make(Kind::Key, 0x1F);  // Forward: 2
        p[3].buttons[2] = Action::make(Kind::Function, 0x05);  // Middle: cycle DPI
        p[4].buttons[3] = Action::make(Kind::Media, 0x00EA);
        p[4].buttons[4] = Action::make(Kind::Media, 0x00E9);
        p[4].scroll_filter = 3;
        Bytes dir(255, 0xFF);
        for (int i = 0; i < 5; i++) {
            sectors[i + 1] = sector_bytes(p[i]);
            dir[4 * i] = 0;
            dir[4 * i + 1] = (uint8_t)(i + 1);
            dir[4 * i + 2] = i < 4 ? 1 : 0;  // the last one is not in the profile cycle
        }
        sectors[0] = with_crc(dir);
        Bytes rom(255, 0xFF);  // ROM directory: the factory profile
        rom[0] = 0x01, rom[1] = 0x01, rom[2] = 1;
        sectors[0x100] = with_crc(rom);
        sectors[0x101] = sector_bytes(make("", {400, 800, 1600, 3200, 6400}, 1, 3, {5, 2, false, 3}, {5, 2, false, 3}));
        load_hits();
    }

    void load_hits() {
        Profile pr = Profile::parse(profile, sectors[profile]);
        hits[0] = pr.hits[0];
        hits[1] = pr.hits[1];
    }

    // Answer to feature id fid, function func. Returns false for an unknown function.
    bool answer(uint16_t fid, int func, const uint8_t* a, uint8_t* out) {
        switch (fid) {
            case F_ROOT:
                if (func == 0) {
                    int id = a[0] << 8 | a[1];
                    for (size_t i = 0; i < std::size(FEATURES); i++)
                        if (FEATURES[i] == id) out[0] = (uint8_t)i;
                } else {  // ping: protocol 4.5, echo
                    out[0] = 4, out[1] = 5, out[2] = a[2];
                }
                return true;
            case F_FEATURESET:
                if (func == 0) out[0] = (uint8_t)(std::size(FEATURES) - 1);
                else if (a[0] < std::size(FEATURES)) out[0] = (uint8_t)(FEATURES[a[0]] >> 8), out[1] = (uint8_t)FEATURES[a[0]], out[3] = 1;
                return true;
            case F_FWINFO: {
                static const uint8_t fw[2][8] = {{0, 'M', 'P', 'M', 0x27, 0x02, 0x00, 0x14}, {1, 'B', 'L', '1', 0x07, 0x00, 0x00, 0x03}};
                if (func == 0) out[0] = 2;
                else if (a[0] < 2) std::memcpy(out, fw[a[0]], 8);
                return true;
            }
            case F_NAME:
                if (func == 0) out[0] = (uint8_t)std::strlen(NAME);
                else for (size_t i = a[0], k = 0; i < std::strlen(NAME) && k < 16; i++, k++) out[k] = (uint8_t)NAME[i];
                return true;
            case F_BATTERY: out[0] = 82, out[1] = 4, out[2] = 0; return true;
            case F_DPI:  // 100, then every 50 up to 48000
                if (func == 2 && a[2] == 0) out[3] = 0x00, out[4] = 0x64, out[5] = 0xE0, out[6] = 0x32, out[7] = 0xBB, out[8] = 0x80;
                return true;
            case F_RATE:
                if (func == 1) out[1] = 0x7F;
                else if (func == 2) out[0] = (uint8_t)rate;
                else if (func == 3) rate = a[0];
                return true;
            case F_MODESTATUS:
                if (func == 0) out[1] = (uint8_t)(surface << 1);
                else surface = (a[1] >> 1) & 3;
                return true;
            case F_HITS:
                if (func == 1 && a[0] < 2) hits[a[0]] = {a[1] >> 2, a[2] >> 2, (a[2] & 1) != 0, a[3] >> 2};
                if (func == 2 && a[0] < 2) {
                    const Hits& h = hits[a[0]];
                    out[0] = a[0], out[1] = (uint8_t)(h.act << 2), out[2] = (uint8_t)(h.rt << 2 | h.rt_on), out[3] = (uint8_t)(h.hap << 2);
                }
                return true;
            case F_ONBOARD: return onboard(func, a, out);
            default: return fid == 0x0020 || fid == 0x1D4B || fid == 0x00C3;
        }
    }

    bool onboard(int func, const uint8_t* a, uint8_t* out) {
        switch (func) {
            case 0: out[1] = 8, out[3] = 5, out[5] = 16, out[6] = 16, out[7] = 0, out[8] = 255; return true;
            case 1: mode = a[0]; return true;
            case 2: out[0] = (uint8_t)mode; return true;
            case 3: if (sectors.count(a[0] << 8 | a[1])) profile = a[0] << 8 | a[1], load_hits(); return true;
            case 4: out[0] = (uint8_t)(profile >> 8), out[1] = (uint8_t)profile; return true;
            case 5: {
                int sec = a[0] << 8 | a[1], off = a[2] << 8 | a[3];
                auto it = sectors.find(sec);
                if (it == sectors.end() || off + 16 > (int)it->second.size()) return false;
                std::memcpy(out, it->second.data() + off, 16);
                return true;
            }
            case 6: write_sector = a[0] << 8 | a[1], write_buf.clear(); return true;
            case 7: write_buf.insert(write_buf.end(), a, a + 16); return true;
            case 8:
                if (write_sector >= 0) {
                    write_buf.resize(255);
                    sectors[write_sector] = write_buf;
                    if (write_sector == profile) load_hits();
                }
                write_sector = -1;
                return true;
            case 11: out[0] = (uint8_t)stage; return true;
            case 12: stage = a[0]; return true;
        }
        return false;
    }
};

bool g_on = false;

}  // namespace

void sim_enable() { g_on = true; }

bool sim_enabled() {
    const char* e = std::getenv("SUPERSTRIKE_DEMO");
    return g_on || (e && *e && std::strcmp(e, "0") != 0);
}

std::vector<Bytes> sim_request(const Bytes& req) {
    static Sim sim;
    if (req.size() < 7 || req[1] != SLOT) return {};  // other slots: nobody there
    std::lock_guard lk(sim.mu);
    uint8_t params[16] = {};
    std::copy(req.begin() + 4, req.begin() + std::min<size_t>(req.size(), 20), params);
    Bytes reply = {0x11, req[1], req[2], req[3]};
    reply.resize(20, 0);
    int idx = req[2], func = req[3] >> 4;
    bool ok = idx < (int)std::size(FEATURES) && sim.answer(FEATURES[idx], func, params, reply.data() + 4);
    if (!ok) reply = {0x11, req[1], 0xFF, req[2], req[3], 0x02 /* invalid argument */, 0, 0};
    return {reply};
}
