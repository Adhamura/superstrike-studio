// Linux backend: /dev/hidraw nodes found through sysfs.
#include <fcntl.h>
#include <sys/select.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "hid_backend.hpp"

namespace fs = std::filesystem;

namespace {

constexpr const char* LOGITECH_VID = "0000046D";

struct HidrawInfo { std::string path; std::string uevent; Bytes desc; };

std::string upper(std::string s) {
    for (char& c : s) c = (char)toupper((unsigned char)c);
    return s;
}

bool contains(const Bytes& hay, std::initializer_list<uint8_t> needle) {
    return std::search(hay.begin(), hay.end(), needle.begin(), needle.end()) != hay.end();
}

std::vector<HidrawInfo> logitech_hidraw() {
    std::vector<fs::path> dirs;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator("/sys/class/hidraw", ec)) dirs.push_back(e.path());
    std::sort(dirs.begin(), dirs.end(), [](const fs::path& a, const fs::path& b) {
        std::string x = a.filename(), y = b.filename();
        return x.size() != y.size() ? x.size() < y.size() : x < y;
    });
    std::vector<HidrawInfo> out;
    for (const auto& d : dirs) {
        std::ifstream u(d / "device/uevent"), r(d / "device/report_descriptor", std::ios::binary);
        if (!u || !r) continue;
        std::string uevent((std::istreambuf_iterator<char>(u)), {});
        if (upper(uevent).find(LOGITECH_VID) == std::string::npos) continue;
        Bytes desc((std::istreambuf_iterator<char>(r)), {});
        out.push_back({"/dev/" + d.filename().string(), uevent, std::move(desc)});
    }
    return out;
}

}  // namespace

struct HidDev { int fd; std::string path; };

std::vector<std::string> find_hidpp_nodes() {
    std::vector<std::string> out;
    for (const auto& n : logitech_hidraw())
        if (contains(n.desc, {0x85, 0x11}) &&
            (contains(n.desc, {0x06, 0x43, 0xff}) || contains(n.desc, {0x06, 0x00, 0xff})))
            out.push_back(n.path);
    return out;
}

std::vector<std::pair<std::string, int>> find_mouse_input_nodes() {
    static const Bytes head = {0x05, 0x01, 0x09, 0x02, 0xa1, 0x01};
    std::vector<std::pair<std::string, int>> out;
    for (const auto& n : logitech_hidraw()) {
        if (n.desc.size() < head.size() || !std::equal(head.begin(), head.end(), n.desc.begin())) continue;
        int rid = -1;
        for (size_t i = 6; i < 12 && i < n.desc.size(); i++)
            if (n.desc[i] == 0x85) {
                if (i + 1 < n.desc.size()) rid = n.desc[i + 1];
                break;
            }
        out.emplace_back(n.path, rid);
    }
    return out;
}

bool hid_long_only() { return false; }
unsigned hid_process_id() { return (unsigned)getpid(); }

HidDev* hid_open(const std::string& path) {
    int fd = ::open(path.c_str(), O_RDWR | O_NONBLOCK);
    if (fd < 0) throw HidppError(path + ": " + std::strerror(errno));
    return new HidDev{fd, path};
}

void hid_close(HidDev* d) {
    if (!d) return;
    ::close(d->fd);
    delete d;
}

void hid_write(HidDev* d, const Bytes& report) {
    ssize_t w;
    do w = ::write(d->fd, report.data(), report.size());
    while (w < 0 && errno == EINTR);
    if (w < 0) throw HidppError(d->path + ": write failed: " + std::strerror(errno));
}

Bytes hid_read(HidDev* d, int timeout_ms) {
    timeval tv{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
    fd_set rs;
    FD_ZERO(&rs);
    FD_SET(d->fd, &rs);
    if (select(d->fd + 1, &rs, nullptr, nullptr, &tv) <= 0) return {};
    uint8_t buf[64];
    ssize_t n = ::read(d->fd, buf, sizeof buf);
    return n > 0 ? Bytes(buf, buf + n) : Bytes{};
}
