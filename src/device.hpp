// PRO X2/X3 SUPERSTRIKE features over HID++ 2.0 (verified on a live PRO X3, receiver 046d:c54f).
//   0x1B0C HITS: fn0 caps, fn1 set [btn][act<<2][rt<<2|rt_on][hap<<2], fn2 get [btn]
//   0x8061 report rate: fn1 mask, fn2 get, fn3 set (Hz = 125 << idx)
//   0x2202 DPI: fn2 paged list (13 bytes/page from param byte 3, BE16, 0b111xxxxx = step range)
//   0x8100 onboard: fn0 info, fn1/2 mode, fn3/4 profile, fn5 read, fn6/7/8 write, fn11/12 stage
//   0x8090 surface (fn0 get [s0][s1] mode=(s1>>1)&3; fn1 set [0][mode<<1][0][6])
//   0x1004 battery fn1 [pct][level][charging]; 0x0003 firmware; 0x0005 name
#pragma once
#include <memory>
#include <map>
#include "hidpp.hpp"

constexpr uint16_t F_ROOT = 0x0000, F_FEATURESET = 0x0001, F_FWINFO = 0x0003, F_NAME = 0x0005,
                   F_BATTERY = 0x1004, F_DPI = 0x2202, F_RATE = 0x8061, F_MODESTATUS = 0x8090,
                   F_SCROLLFILTER = 0x80E0, F_HITS = 0x1B0C, F_ONBOARD = 0x8100;
constexpr int MODE_ONBOARD = 1, MODE_HOST = 2;
constexpr int RATES_HZ[7] = {125, 250, 500, 1000, 2000, 4000, 8000};

uint16_t crc16(const uint8_t* data, size_t n);  // CRC-16/CCITT-FALSE
bool sector_crc_ok(const Bytes& s);
std::string battery_state_text(const std::string& state);  // Device::battery() state, translated for display

struct Hits {
    int act = 5, rt = 2;
    bool rt_on = false;
    int hap = 3;
    bool operator==(const Hits&) const = default;
};

struct FeatureEntry { int index; uint16_t id; uint8_t flags, version; };
struct OnboardInfo { int format = 0, profiles = 0, buttons = 0, sectors = 0, sector_size = 255; };

class Device {
public:
    // Finds the first SUPERSTRIKE (any mouse with HITS) on any HID++ node; a sleeping mouse
    // gets a second, longer ping. Throws HidppError("no SUPERSTRIKE mouse found…").
    static std::unique_ptr<Device> open();

    const std::string& name() const { return name_; }
    const std::string& path() const { return t_->path(); }
    int index() const { return dev_; }  // 1..6 receiver slot, 0xFF = cable

    Bytes call(uint16_t fid, uint8_t func, const Bytes& params = {}, int retries = 3);
    int feature(uint16_t fid);  // feature index, 0 = absent (cached)
    std::vector<FeatureEntry> features();

    std::vector<std::string> firmware();
    std::pair<int, std::string> battery();  // percent, "discharging"/"charging"/...

    Hits hits_get(int button);
    void hits_set(int button, const Hits& h);

    std::vector<int> rate_supported();  // indexes into RATES_HZ
    int rate_get();
    void rate_set(int idx);
    std::vector<int> dpi_list();         // every accepted DPI value, sorted
    int surface_get();                   // 0 auto, 1 on, 2 off
    void surface_set(int mode);

    OnboardInfo onboard_info();
    int onboard_mode();
    void set_onboard_mode(int mode);
    int current_profile();               // sector
    void set_current_profile(int sector);  // retried; reloads the profile
    int current_stage();
    void set_current_stage(int stage);
    Bytes read_sector(int sector, int size = 255);   // CRC-checked (3 tries) below 0x100
    Bytes write_sector(int sector, Bytes data);      // sets CRC, writes, verifies read-back

private:
    Device(std::unique_ptr<Transport> t, int dev);
    std::unique_ptr<Transport> t_;
    int dev_;
    std::string name_;
    std::map<uint16_t, int> feat_;
};
