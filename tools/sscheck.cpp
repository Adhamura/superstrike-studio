// Read-only self test: never calls hits_set, write_sector or any set_* function.
#include <cstdio>
#include <string>

#include "../src/profile.hpp"

static int fails = 0;

static void check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what.c_str());
    if (!ok) fails++;
}

static std::string hits_text(const Hits& h) {
    return "act " + std::to_string(h.act) + " rt " + std::to_string(h.rt) + (h.rt_on ? " on" : " off") +
           " hap " + std::to_string(h.hap);
}

static void check_profile(Device& d, int sector) {
    char label[48];
    std::snprintf(label, sizeof label, "sector 0x%04x round trip", sector);
    Bytes raw = d.read_sector(sector);
    Profile p = Profile::parse(sector, raw);
    check(p.encode() == raw, std::string(label) + " (name \"" + p.name + "\")");
    Profile r = p;
    check(r.from_json(p.to_json()) && r.encode() == raw, std::string(label) + " via JSON");
}

int main() {
    try {
        auto d = Device::open();
        std::printf("name:     %s\npath:     %s (index %d)\n", d->name().c_str(), d->path().c_str(), d->index());
        for (const auto& f : d->firmware()) std::printf("firmware: %s\n", f.c_str());
        auto bat = d->battery();
        std::printf("battery:  %d%% %s\n", bat.first, bat.second.c_str());
        for (int b = 0; b < 2; b++) std::printf("HITS %c:   %s\n", "LR"[b], hits_text(d->hits_get(b)).c_str());
        std::string rates;
        for (int i : d->rate_supported()) rates += std::to_string(RATES_HZ[i]) + " ";
        std::printf("rates:    %s(current %d Hz)\n", rates.c_str(), RATES_HZ[d->rate_get()]);
        auto dpi = d->dpi_list();
        std::printf("dpi:      %zu values, min %d, max %d\n", dpi.size(), dpi.empty() ? 0 : dpi.front(),
                    dpi.empty() ? 0 : dpi.back());
        std::printf("mode:     %d\nprofile:  0x%04x\nstage:    %d\n", d->onboard_mode(), d->current_profile(),
                    d->current_stage());
        OnboardInfo oi = d->onboard_info();
        std::printf("onboard:  format %d, %d profiles, %d buttons, %d sectors, sector size %d\n", oi.format,
                    oi.profiles, oi.buttons, oi.sectors, oi.sector_size);

        std::printf("directory:\n");
        Bytes dir = d->read_sector(0);
        auto entries = parse_directory(dir);
        check(encode_directory(dir, entries) == dir, "directory round trip");
        for (const auto& e : entries) std::printf("  sector 0x%04x %s\n", e.sector, e.enabled ? "enabled" : "disabled");
        std::printf("profiles:\n");
        for (const auto& e : entries) check_profile(*d, e.sector);
        check_profile(*d, 0x101);

        std::printf("hits codes:\n");
        for (const auto* list : {&PRO_PRESETS, &BUILTIN_PRESETS})
            for (const auto& pr : *list) {
                std::array<Hits, 2> h;
                std::string err;
                bool ok = hits_code_decode(pr.code, h, &err) && hits_code_encode(h) == pr.code;
                check(ok, std::string(pr.name) + (ok ? ": " + hits_text(h[0]) + " / " + hits_text(h[1]) : ": " + err));
            }
    } catch (const std::exception& e) {
        std::printf("error: %s\n", e.what());
        fails++;
    }
    std::printf("%s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
