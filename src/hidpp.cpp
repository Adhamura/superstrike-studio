// HID++ 2.0 framing shared by every OS: builds requests, matches replies, maps errors.
// The raw HID access lives in hidpp_linux.cpp / hidpp_win.cpp (hid_backend.hpp).
#include "hidpp.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>

#include "hid_backend.hpp"

using Clock = std::chrono::steady_clock;

namespace {
constexpr uint8_t SHORT_REPORT = 0x10, LONG_REPORT = 0x11;
}

Transport::Transport(const std::string& path)
    : long_only(hid_long_only()), path_(path), swid_((uint8_t)(2 + hid_process_id() % 14)) {
    dev_ = hid_open(path);
}

Transport::~Transport() { hid_close(dev_); }

void Transport::drain() {
    while (!hid_read(dev_, 0).empty()) {}
}

Bytes Transport::request(uint8_t dev, uint8_t feat, uint8_t func, const Bytes& params, int timeout_ms,
                         bool long_) {
    const bool use_long = long_ || long_only || params.size() > 3;
    const uint8_t fs_byte = (uint8_t)((func << 4) | swid_);
    Bytes msg = {use_long ? LONG_REPORT : SHORT_REPORT, dev, feat, fs_byte};
    msg.resize(use_long ? 20 : 7, 0);
    std::copy_n(params.begin(), std::min(params.size(), msg.size() - 4), msg.begin() + 4);
    hid_write(dev_, msg);

    char tag[48];
    std::snprintf(tag, sizeof tag, "feat=%#x func=%d", feat, func);
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
    for (;;) {
        auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
        if (left <= 0) throw HidppError(std::string("timeout ") + tag, 0, true);
        Bytes r = hid_read(dev_, (int)left);
        if (r.size() < 4 || (r[0] != SHORT_REPORT && r[0] != LONG_REPORT) || r[1] != dev) continue;
        if (r.size() >= 6 && r[2] == 0xFF && r[3] == feat && r[4] == fs_byte)
            throw HidppError(std::string("hid++2 error ") + std::to_string(r[5]) + " " + tag, r[5]);
        if (r.size() >= 6 && r[2] == 0x8F && r[3] == feat)
            throw HidppError(std::string("hid++1 error ") + std::to_string(r[5]) + " " + tag, r[5]);
        if (r[2] == feat && r[3] == fs_byte) {
            Bytes out(r.begin() + 4, r.end());
            if (out.size() < 16) out.resize(16, 0);
            return out;
        }
    }
}
