// Onboard profile sector model (format 8, 255-byte sectors) and G HUB HITS codes.
// Layout: 0x00 rate idx wireless, 0x01 rate idx wired, 0x02 default stage, 0x03 shift stage,
// 0x04+5n stage n [X LE16][Y LE16][LOD] (X==0 -> off), 0x22 angle snap, 0x25 scroll filter (ms/10,
// 0xFF off), 0x26 HITS L/R 3 bytes each [act<<2][rt<<2|on][hap<<2], 0x2C u16 LE sleep s,
// 0x2E u16 LE off s, 0x30 buttons 16x4, 0x70 shift buttons 12x4, 0xA0 name UTF-16LE 24 chars,
// last 2 bytes CRC BE. Directory sector 0: [secHi][secLo][enabled][0xFF]..., end 0xFFFF.
#pragma once
#include <array>
#include <string>
#include <vector>
#include "device.hpp"

struct Named { const char* name; int code; };
extern const std::vector<Named> MOUSE_BUTTONS, FUNCTIONS, MEDIA, MODIFIERS, KEYS;  // names are English msgids
const char* named_text(const std::vector<Named>& table, const Named& n);          // translated name for display
extern const char* const BUTTON_NAMES[5];  // msgids "Left button", ... "Forward (front side)"; show with _()
extern const char* const BUTTON_IDS[5];    // "btn-left","btn-right","btn-middle","btn-back","btn-forward"
int evdev_to_hid(int evdev_code);          // -1 when unknown
int modifier_bit(int hid_usage);           // 0xE0.. -> mask bit, else 0

enum class Kind { Default, Disabled, Mouse, Key, Media, Function, Raw };

struct Action {
    std::array<uint8_t, 4> raw{0xFF, 0xFF, 0xFF, 0xFF};
    Kind kind() const;
    int code() const;  // mouse mask / HID usage / consumer code / function id
    int mods() const;  // keyboard modifier mask
    std::string describe() const;  // translated: "Left click", "Ctrl+C", "Next DPI", "Disabled", ...
    static Action make(Kind k, int code = 0, int mods = 0);
};

struct Stage { int x = 0, y = 0, lod = 0; };  // x == 0: stage off; lod 1 low, 2 medium, 3 high

struct Profile {
    int sector = -1;
    Bytes raw;  // original 255 bytes (unknown fields kept)
    int rate_wireless = 3, rate_wired = 3, default_stage = 0, shift_stage = 0;
    std::array<Stage, 5> stages{};
    int angle_snap = 0, scroll_filter = 0;  // scroll_filter in 10 ms units, 0 off
    std::array<Hits, 2> hits{};
    int sleep_s = 60, off_s = 300;
    std::array<Action, 16> buttons{};
    std::array<Action, 12> shift_buttons{};
    std::string name;  // UTF-8

    static Profile parse(int sector, const Bytes& raw);
    Bytes encode() const;  // CRC bytes left for Device::write_sector
    std::string to_json() const;                 // export (readable)
    bool from_json(const std::string& json);     // import; false on bad input
};

struct DirEntry { int sector; bool enabled; };
std::vector<DirEntry> parse_directory(const Bytes& raw);
Bytes encode_directory(const Bytes& raw, const std::vector<DirEntry>& entries);

// G HUB HITS code: base64 of 16 digits "00" act L, act R, rt L, rt R, rt-on L, rt-on R (1 digit
// each), hap L, hap R. Example INS: MDAwNDA1MDMwNTEwMDQwMw== -> act 4/5, rt 3/5 (R off), hap 4/3.
bool hits_code_decode(const std::string& code, std::array<Hits, 2>& out, std::string* err = nullptr);
std::string hits_code_encode(const std::array<Hits, 2>& h);
struct Preset { const char* name; const char* code; };
extern const std::vector<Preset> PRO_PRESETS;      // Logitech-published pro HITS codes
extern const std::vector<Preset> BUILTIN_PRESETS;  // generic presets as codes; names are msgids (context "HITS preset")
