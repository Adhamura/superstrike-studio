// Name tables for button actions (mouse buttons, functions, media, modifiers, keys) and key code maps.
// Names are English msgids (marked with NC_); named_text() gives the translated text for display.
#include <deque>
#include <unordered_map>
#include "i18n.hpp"
#include "profile.hpp"

const std::vector<Named> MOUSE_BUTTONS = {
    {NC_("mouse button", "Left click"), 0x0001}, {NC_("mouse button", "Right click"), 0x0002},
    {NC_("mouse button", "Middle click"), 0x0004}, {NC_("mouse button", "Back"), 0x0008},
    {NC_("mouse button", "Forward"), 0x0010}, {NC_("mouse button", "Button 6"), 0x0020},
    {NC_("mouse button", "Button 7"), 0x0040}, {NC_("mouse button", "Button 8"), 0x0080}};
const std::vector<Named> FUNCTIONS = {
    {NC_("button function", "Next DPI"), 0x03}, {NC_("button function", "Previous DPI"), 0x04},
    {NC_("button function", "Cycle DPI"), 0x05}, {NC_("button function", "Default DPI"), 0x06},
    {NC_("button function", "DPI shift (hold)"), 0x07}, {NC_("button function", "Next profile"), 0x08},
    {NC_("button function", "Previous profile"), 0x09}, {NC_("button function", "Cycle profile"), 0x0A},
    {NC_("button function", "Battery status"), 0x0C}, {NC_("button function", "Scroll down"), 0x10},
    {NC_("button function", "Scroll up"), 0x11}, {NC_("button function", "Tilt left"), 0x01},
    {NC_("button function", "Tilt right"), 0x02}};
const std::vector<Named> MEDIA = {
    {NC_("media key", "Play / Pause"), 0x00CD}, {NC_("media key", "Next track"), 0x00B5},
    {NC_("media key", "Previous track"), 0x00B6}, {NC_("media key", "Stop"), 0x00B7},
    {NC_("media key", "Mute"), 0x00E2}, {NC_("media key", "Volume up"), 0x00E9},
    {NC_("media key", "Volume down"), 0x00EA}, {NC_("media key", "Brightness up"), 0x006F},
    {NC_("media key", "Brightness down"), 0x0070}, {NC_("media key", "Calculator"), 0x0192},
    {NC_("media key", "Browser home"), 0x0223}, {NC_("media key", "Browser back"), 0x0224},
    {NC_("media key", "Browser forward"), 0x0225}, {NC_("media key", "Browser refresh"), 0x0227},
    {NC_("media key", "Mail"), 0x018A}, {NC_("media key", "File manager"), 0x0194}};
const std::vector<Named> MODIFIERS = {
    {NC_("modifier key", "Ctrl"), 0x01}, {NC_("modifier key", "Shift"), 0x02}, {NC_("modifier key", "Alt"), 0x04},
    {NC_("modifier key", "Super"), 0x08}, {NC_("modifier key", "Right Ctrl"), 0x10},
    {NC_("modifier key", "Right Shift"), 0x20}, {NC_("modifier key", "AltGr"), 0x40},
    {NC_("modifier key", "Right Super"), 0x80}};

const char* const BUTTON_NAMES[5] = {N_("Left button"), N_("Right button"), N_("Middle button (wheel click)"),
                                     N_("Back (rear side)"), N_("Forward (front side)")};
const char* const BUTTON_IDS[5] = {"btn-left", "btn-right", "btn-middle", "btn-back", "btn-forward"};

static std::vector<Named> make_keys() {
    static std::deque<std::string> names;  // stable storage for the generated names (letters, digits, F-keys)
    std::vector<Named> k;
    auto add = [&](std::string n, int code) {
        names.push_back(std::move(n));
        k.push_back({names.back().c_str(), code});
    };
    for (int i = 0; i < 26; i++) add(std::string(1, (char)('A' + i)), 0x04 + i);
    for (int i = 0; i < 10; i++) add(std::to_string((i + 1) % 10), 0x1E + i);
    const Named fixed[] = {
        {NC_("keyboard key", "Enter"), 0x28}, {NC_("keyboard key", "Escape"), 0x29},
        {NC_("keyboard key", "Backspace"), 0x2A}, {NC_("keyboard key", "Tab"), 0x2B},
        {NC_("keyboard key", "Space"), 0x2C}, {"-", 0x2D}, {"=", 0x2E}, {"[", 0x2F}, {"]", 0x30}, {"\\", 0x31},
        {";", 0x33}, {"'", 0x34}, {"`", 0x35}, {",", 0x36}, {".", 0x37}, {"/", 0x38},
        {NC_("keyboard key", "Caps Lock"), 0x39}};
    for (const auto& n : fixed) k.push_back(n);
    for (int i = 0; i < 12; i++) add("F" + std::to_string(i + 1), 0x3A + i);
    const Named nav[] = {
        {NC_("keyboard key", "Print Screen"), 0x46}, {NC_("keyboard key", "Scroll Lock"), 0x47},
        {NC_("keyboard key", "Pause"), 0x48}, {NC_("keyboard key", "Insert"), 0x49},
        {NC_("keyboard key", "Home"), 0x4A}, {NC_("keyboard key", "Page Up"), 0x4B},
        {NC_("keyboard key", "Delete"), 0x4C}, {NC_("keyboard key", "End"), 0x4D},
        {NC_("keyboard key", "Page Down"), 0x4E}, {NC_("keyboard key", "Right arrow"), 0x4F},
        {NC_("keyboard key", "Left arrow"), 0x50}, {NC_("keyboard key", "Down arrow"), 0x51},
        {NC_("keyboard key", "Up arrow"), 0x52}, {NC_("keyboard key", "Menu"), 0x65}};
    for (const auto& n : nav) k.push_back(n);
    for (int i = 0; i < 12; i++) add("F" + std::to_string(13 + i), 0x68 + i);
    const Named keypad[] = {
        {NC_("keyboard key", "Keypad 1"), 0x59}, {NC_("keyboard key", "Keypad 2"), 0x5A},
        {NC_("keyboard key", "Keypad 3"), 0x5B}, {NC_("keyboard key", "Keypad 4"), 0x5C},
        {NC_("keyboard key", "Keypad 5"), 0x5D}, {NC_("keyboard key", "Keypad 6"), 0x5E},
        {NC_("keyboard key", "Keypad 7"), 0x5F}, {NC_("keyboard key", "Keypad 8"), 0x60},
        {NC_("keyboard key", "Keypad 9"), 0x61}, {NC_("keyboard key", "Keypad 0"), 0x62},
        {NC_("keyboard key", "Keypad Enter"), 0x58}, {NC_("keyboard key", "Ctrl (alone)"), 0xE0},
        {NC_("keyboard key", "Shift (alone)"), 0xE1}, {NC_("keyboard key", "Alt (alone)"), 0xE2}};
    for (const auto& n : keypad) k.push_back(n);
    return k;
}
const std::vector<Named> KEYS = make_keys();

// Translated display text of a table entry (the context is the table's kind of name).
const char* named_text(const std::vector<Named>& table, const Named& n) {
    const char* ctx = &table == &MOUSE_BUTTONS ? "mouse button"
                      : &table == &FUNCTIONS   ? "button function"
                      : &table == &MEDIA       ? "media key"
                      : &table == &MODIFIERS   ? "modifier key"
                                               : "keyboard key";
    return C_(ctx, n.name);
}

int evdev_to_hid(int c) {
    static const std::unordered_map<int, int> map = [] {
        std::unordered_map<int, int> m = {
            {1, 0x29}, {2, 0x1E}, {3, 0x1F}, {4, 0x20}, {5, 0x21}, {6, 0x22}, {7, 0x23}, {8, 0x24},
            {9, 0x25}, {10, 0x26}, {11, 0x27}, {12, 0x2D}, {13, 0x2E}, {14, 0x2A}, {15, 0x2B},
            {16, 0x14}, {17, 0x1A}, {18, 0x08}, {19, 0x15}, {20, 0x17}, {21, 0x1C}, {22, 0x18},
            {23, 0x0C}, {24, 0x12}, {25, 0x13}, {26, 0x2F}, {27, 0x30}, {28, 0x28}, {29, 0xE0},
            {30, 0x04}, {31, 0x16}, {32, 0x07}, {33, 0x09}, {34, 0x0A}, {35, 0x0B}, {36, 0x0D},
            {37, 0x0E}, {38, 0x0F}, {39, 0x33}, {40, 0x34}, {41, 0x35}, {42, 0xE1}, {43, 0x31},
            {44, 0x1D}, {45, 0x1B}, {46, 0x06}, {47, 0x19}, {48, 0x05}, {49, 0x11}, {50, 0x10},
            {51, 0x36}, {52, 0x37}, {53, 0x38}, {54, 0xE5}, {55, 0x55}, {56, 0xE2}, {57, 0x2C},
            {58, 0x39}, {59, 0x3A}, {60, 0x3B}, {61, 0x3C}, {62, 0x3D}, {63, 0x3E}, {64, 0x3F},
            {65, 0x40}, {66, 0x41}, {67, 0x42}, {68, 0x43}, {69, 0x53}, {70, 0x47}, {71, 0x5F},
            {72, 0x60}, {73, 0x61}, {74, 0x56}, {75, 0x5C}, {76, 0x5D}, {77, 0x5E}, {78, 0x57},
            {79, 0x59}, {80, 0x5A}, {81, 0x5B}, {82, 0x62}, {83, 0x63}, {86, 0x64}, {87, 0x44},
            {88, 0x45}, {96, 0x58}, {97, 0xE4}, {98, 0x54}, {99, 0x46}, {100, 0xE6}, {102, 0x4A},
            {103, 0x52}, {104, 0x4B}, {105, 0x50}, {106, 0x4F}, {107, 0x4D}, {108, 0x51},
            {109, 0x4E}, {110, 0x49}, {111, 0x4C}, {113, 0x7F}, {114, 0x81}, {115, 0x80},
            {119, 0x48}, {125, 0xE3}, {126, 0xE7}, {127, 0x65}};
        for (int i = 0; i < 12; i++) m[183 + i] = 0x68 + i;
        return m;
    }();
    auto it = map.find(c);
    return it == map.end() ? -1 : it->second;
}

int modifier_bit(int usage) { return usage >= 0xE0 && usage <= 0xE7 ? 1 << (usage - 0xE0) : 0; }
