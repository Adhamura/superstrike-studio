// macOS backend: IOKit HID (IOHIDManager to find the device, IOHIDDevice to talk to it).
// A node is "DevSrvsID:<registry entry id>". macOS keeps the short (0x10) and long (0x11) HID++
// reports of a receiver in one device, but like on Windows every request goes out as long.
// Input reports arrive on a serial dispatch queue and wait in a small queue for hid_read.
#include <IOKit/IOKitLib.h>
#include <IOKit/hid/IOHIDDevice.h>
#include <IOKit/hid/IOHIDKeys.h>
#include <IOKit/hid/IOHIDManager.h>
#include <dispatch/dispatch.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>

#include "hid_backend.hpp"

namespace {

constexpr int LOGITECH_VID = 0x046D, HIDPP_LONG_LEN = 20;

int int_prop(IOHIDDeviceRef d, CFStringRef key) {
    int v = 0;
    if (auto n = (CFNumberRef)IOHIDDeviceGetProperty(d, key); n && CFGetTypeID(n) == CFNumberGetTypeID())
        CFNumberGetValue(n, kCFNumberIntType, &v);
    return v;
}

// A HID++ device: Logitech, with a vendor page 0xFF00 or 0xFF43 collection.
bool is_hidpp(IOHIDDeviceRef d) {
    if (int_prop(d, CFSTR(kIOHIDVendorIDKey)) != LOGITECH_VID) return false;
    auto pairs = (CFArrayRef)IOHIDDeviceGetProperty(d, CFSTR(kIOHIDDeviceUsagePairsKey));
    if (!pairs || CFGetTypeID(pairs) != CFArrayGetTypeID()) {
        int page = int_prop(d, CFSTR(kIOHIDPrimaryUsagePageKey));
        return page == 0xFF00 || page == 0xFF43;
    }
    for (CFIndex i = 0; i < CFArrayGetCount(pairs); i++) {
        auto pair = (CFDictionaryRef)CFArrayGetValueAtIndex(pairs, i);
        auto n = (CFNumberRef)CFDictionaryGetValue(pair, CFSTR(kIOHIDDeviceUsagePageKey));
        int page = 0;
        if (n) CFNumberGetValue(n, kCFNumberIntType, &page);
        if (page == 0xFF00 || page == 0xFF43) return true;
    }
    return false;
}

std::string node_name(IOHIDDeviceRef d) {
    uint64_t id = 0;
    if (IORegistryEntryGetRegistryEntryID(IOHIDDeviceGetService(d), &id) != KERN_SUCCESS) return {};
    char buf[64];
    std::snprintf(buf, sizeof buf, "DevSrvsID:%llu", (unsigned long long)id);
    return buf;
}

std::string with_error(const std::string& what, IOReturn r) {
    char buf[32];
    std::snprintf(buf, sizeof buf, " (IOKit error 0x%08x)", (unsigned)r);
    std::string s = what + buf;
    if (r == kIOReturnNotPermitted)
        s += ". Allow SuperStrike Studio in System Settings > Privacy & Security > Input Monitoring";
    return s;
}

}  // namespace

struct HidDev {
    IOHIDDeviceRef dev = nullptr;
    dispatch_queue_t queue = nullptr;
    dispatch_semaphore_t cancelled = nullptr;
    std::string path;
    std::vector<uint8_t> in_buf;
    std::mutex m;
    std::condition_variable cv;
    std::deque<Bytes> reports;
};

// Input report: macOS passes it with the report id first when the device numbers its reports.
static void on_report(void* ctx, IOReturn result, void*, IOHIDReportType, uint32_t id, uint8_t* data, CFIndex len) {
    auto* d = static_cast<HidDev*>(ctx);
    if (result != kIOReturnSuccess || len <= 0) return;
    Bytes r(data, data + len);
    if (id && r[0] != id) r.insert(r.begin(), (uint8_t)id);  // keep the report id first, as on the other OSes
    std::lock_guard<std::mutex> lock(d->m);
    if (d->reports.size() > 64) d->reports.pop_front();
    d->reports.push_back(std::move(r));
    d->cv.notify_one();
}

std::vector<std::string> find_hidpp_nodes() {
    std::vector<std::string> out;
    IOHIDManagerRef mgr = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    if (!mgr) return out;
    int vid = LOGITECH_VID;
    CFNumberRef n = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &vid);
    const void* keys[] = {CFSTR(kIOHIDVendorIDKey)};
    const void* vals[] = {n};
    CFDictionaryRef match = CFDictionaryCreate(kCFAllocatorDefault, keys, vals, 1, &kCFTypeDictionaryKeyCallBacks,
                                               &kCFTypeDictionaryValueCallBacks);
    IOHIDManagerSetDeviceMatching(mgr, match);
    if (CFSetRef set = IOHIDManagerCopyDevices(mgr)) {
        CFIndex count = CFSetGetCount(set);
        std::vector<const void*> devs((size_t)count);
        CFSetGetValues(set, devs.data());
        for (const void* p : devs) {
            auto d = (IOHIDDeviceRef)p;
            if (!is_hidpp(d)) continue;
            std::string name = node_name(d);
            if (!name.empty()) out.push_back(name);
        }
        CFRelease(set);
    }
    CFRelease(match);
    CFRelease(n);
    CFRelease(mgr);
    std::sort(out.begin(), out.end());
    return out;
}

// The live click view reads the mouse buttons in the window only (like on Windows).
std::vector<std::pair<std::string, int>> find_mouse_input_nodes() { return {}; }

bool hid_long_only() { return true; }
unsigned hid_process_id() { return (unsigned)getpid(); }

HidDev* hid_open(const std::string& path) {
    unsigned long long id = 0;
    if (std::sscanf(path.c_str(), "DevSrvsID:%llu", &id) != 1) throw HidppError(path + ": not a device");
    io_service_t svc = IOServiceGetMatchingService(MACH_PORT_NULL, IORegistryEntryIDMatching(id));
    if (!svc) throw HidppError(path + ": device is gone");
    IOHIDDeviceRef dev = IOHIDDeviceCreate(kCFAllocatorDefault, svc);
    IOObjectRelease(svc);
    if (!dev) throw HidppError(path + ": cannot create the device");
    if (!is_hidpp(dev)) {
        CFRelease(dev);
        throw HidppError(path + ": not a HID++ long report node");
    }
    IOReturn r = IOHIDDeviceOpen(dev, kIOHIDOptionsTypeNone);
    if (r != kIOReturnSuccess) {
        CFRelease(dev);
        throw HidppError(with_error(path + ": open failed", r));
    }
    auto* d = new HidDev;
    d->dev = dev;
    d->path = path;
    d->in_buf.resize((size_t)std::max(int_prop(dev, CFSTR(kIOHIDMaxInputReportSizeKey)), 64));
    d->queue = dispatch_queue_create("superstrike.hid", DISPATCH_QUEUE_SERIAL);
    d->cancelled = dispatch_semaphore_create(0);
    IOHIDDeviceRegisterInputReportCallback(dev, d->in_buf.data(), (CFIndex)d->in_buf.size(), on_report, d);
    IOHIDDeviceSetDispatchQueue(dev, d->queue);
    dispatch_semaphore_t sem = d->cancelled;
    IOHIDDeviceSetCancelHandler(dev, ^{ dispatch_semaphore_signal(sem); });
    IOHIDDeviceActivate(dev);
    return d;
}

void hid_close(HidDev* d) {
    if (!d) return;
    IOHIDDeviceCancel(d->dev);
    dispatch_semaphore_wait(d->cancelled, dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC));
    IOHIDDeviceClose(d->dev, kIOHIDOptionsTypeNone);
    CFRelease(d->dev);
    dispatch_release(d->queue);
    dispatch_release(d->cancelled);
    delete d;
}

void hid_write(HidDev* d, const Bytes& report) {
    if (report.empty()) return;
    Bytes buf = report;
    if (buf[0] == 0x11 && buf.size() < (size_t)HIDPP_LONG_LEN) buf.resize(HIDPP_LONG_LEN, 0);
    IOReturn r = IOHIDDeviceSetReport(d->dev, kIOHIDReportTypeOutput, buf[0], buf.data(), (CFIndex)buf.size());
    if (r != kIOReturnSuccess) throw HidppError(with_error(d->path + ": write failed", r));
}

Bytes hid_read(HidDev* d, int timeout_ms) {
    std::unique_lock<std::mutex> lock(d->m);
    if (!d->cv.wait_for(lock, std::chrono::milliseconds(std::max(timeout_ms, 0)), [d] { return !d->reports.empty(); }))
        return {};
    Bytes r = std::move(d->reports.front());
    d->reports.pop_front();
    return r;
}
