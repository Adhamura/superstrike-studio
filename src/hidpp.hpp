// HID++ 2.0 transport (no external deps). Linux: hidraw (hidpp_linux.cpp). Windows: Win32 HID API (hidpp_win.cpp).
#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using Bytes = std::vector<uint8_t>;
struct HidDev;  // OS handle, see hid_backend.hpp

struct HidppError : std::runtime_error {
    int code = 0;          // HID++ error code (0 = none)
    bool timeout = false;  // no answer in time
    HidppError(const std::string& msg, int code_ = 0, bool timeout_ = false)
        : std::runtime_error(msg), code(code_), timeout(timeout_) {}
};

// Nodes (hidraw path / Windows device path) of Logitech devices that carry HID++ long reports (id 0x11).
std::vector<std::string> find_hidpp_nodes();
// hidraw nodes that carry plain mouse input reports; second = report id or -1 when none.
// Always empty on Windows (the OS owns the mouse collections; the app uses window events).
std::vector<std::pair<std::string, int>> find_mouse_input_nodes();

class Transport {
public:
    explicit Transport(const std::string& path);  // throws HidppError when it can not open
    ~Transport();
    Transport(const Transport&) = delete;
    Transport& operator=(const Transport&) = delete;

    bool long_only;  // send every request as a long report (set by the OS backend; Windows has no short node)
    const std::string& path() const { return path_; }
    void drain();
    // Sends a request (long report when params > 3 bytes or long_ is true) and returns the
    // 16 reply parameter bytes. Throws HidppError (timeout or device error).
    Bytes request(uint8_t dev, uint8_t feat, uint8_t func, const Bytes& params = {},
                  int timeout_ms = 1000, bool long_ = true);

private:
    HidDev* dev_ = nullptr;
    std::string path_;
    uint8_t swid_;  // per process, 2..15, so two programs on one receiver do not mix replies
};
