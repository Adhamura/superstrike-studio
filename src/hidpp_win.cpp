// Windows backend: native Win32 HID API (setupapi + hid.dll), overlapped I/O.
// Windows splits the short (7 byte, id 0x10) and long (20 byte, id 0x11) reports of a receiver
// into separate device paths, so we only open the long one and send every request as long.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>

#include <algorithm>
#include <memory>

#include "hid_backend.hpp"

namespace {

constexpr USHORT LOGITECH_VID = 0x046D, HIDPP_LONG_LEN = 20;
constexpr DWORD IO_TIMEOUT_MS = 1000;

std::string to_utf8(const wchar_t* w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string s((size_t)n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring to_wide(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 1) return {};
    std::wstring w((size_t)n - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

std::string with_error(const std::string& what, DWORD err = GetLastError()) {
    return what + " (Windows error " + std::to_string(err) + ")";
}

// Report sizes of the top-level collection behind an open handle; false when it is not a HID++ one.
bool hidpp_caps(HANDLE h, HIDP_CAPS& caps) {
    HIDD_ATTRIBUTES attr{};
    attr.Size = sizeof attr;
    if (!HidD_GetAttributes(h, &attr) || attr.VendorID != LOGITECH_VID) return false;
    PHIDP_PREPARSED_DATA pre = nullptr;
    if (!HidD_GetPreparsedData(h, &pre)) return false;
    bool ok = HidP_GetCaps(pre, &caps) == HIDP_STATUS_SUCCESS;
    HidD_FreePreparsedData(pre);
    return ok && (caps.UsagePage == 0xFF00 || caps.UsagePage == 0xFF43) &&
           caps.OutputReportByteLength == HIDPP_LONG_LEN;
}

constexpr DWORD SHARE = FILE_SHARE_READ | FILE_SHARE_WRITE;

}  // namespace

struct HidDev {
    HANDLE h = INVALID_HANDLE_VALUE;
    HANDLE event = nullptr;  // signalled by finished overlapped I/O
    std::string path;
    DWORD in_len = 0, out_len = 0;
};

std::vector<std::string> find_hidpp_nodes() {
    std::vector<std::string> out;
    GUID guid;
    HidD_GetHidGuid(&guid);
    HDEVINFO set = SetupDiGetClassDevsW(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) return out;
    SP_DEVICE_INTERFACE_DATA ifd{};
    ifd.cbSize = sizeof ifd;
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(set, nullptr, &guid, i, &ifd); i++) {
        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &ifd, nullptr, 0, &need, nullptr);
        if (need < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) continue;
        std::vector<char> buf(need);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buf.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(set, &ifd, detail, need, nullptr, nullptr)) continue;
        // Access 0 only queries: it works even for collections the OS keeps exclusive (mouse, keyboard).
        HANDLE h = CreateFileW(detail->DevicePath, 0, SHARE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;
        HIDP_CAPS caps{};
        if (hidpp_caps(h, caps)) out.push_back(to_utf8(detail->DevicePath));
        CloseHandle(h);
    }
    SetupDiDestroyDeviceInfoList(set);
    return out;
}

std::vector<std::pair<std::string, int>> find_mouse_input_nodes() { return {}; }

bool hid_long_only() { return true; }
unsigned hid_process_id() { return GetCurrentProcessId(); }

HidDev* hid_open(const std::string& path) {
    auto d = std::make_unique<HidDev>();
    d->path = path;
    d->h = CreateFileW(to_wide(path).c_str(), GENERIC_READ | GENERIC_WRITE, SHARE, nullptr, OPEN_EXISTING,
                       FILE_FLAG_OVERLAPPED, nullptr);
    if (d->h == INVALID_HANDLE_VALUE) throw HidppError(with_error(path + ": open failed"));
    HIDP_CAPS caps{};
    if (!hidpp_caps(d->h, caps)) {
        CloseHandle(d->h);
        throw HidppError(path + ": not a HID++ long report node");
    }
    d->in_len = caps.InputReportByteLength;
    d->out_len = caps.OutputReportByteLength;
    d->event = CreateEventW(nullptr, TRUE, FALSE, nullptr);  // manual reset
    if (!d->event) {
        CloseHandle(d->h);
        throw HidppError(with_error(path + ": no event"));
    }
    return d.release();
}

void hid_close(HidDev* d) {
    if (!d) return;
    CancelIo(d->h);
    CloseHandle(d->event);
    CloseHandle(d->h);
    delete d;
}

namespace {

// Waits for the overlapped call that just returned ERROR_IO_PENDING. On timeout the I/O is
// cancelled and reaped, so the buffer can go away. Returns the byte count or -1 on failure.
long finish_io(HidDev* d, OVERLAPPED& ov, DWORD timeout_ms) {
    if (WaitForSingleObject(d->event, timeout_ms) != WAIT_OBJECT_0) CancelIo(d->h);
    DWORD n = 0;
    return GetOverlappedResult(d->h, &ov, &n, TRUE) ? (long)n : -1;
}

}  // namespace

void hid_write(HidDev* d, const Bytes& report) {
    Bytes buf(d->out_len, 0);  // exactly OutputReportByteLength bytes
    std::copy_n(report.begin(), std::min<size_t>(report.size(), buf.size()), buf.begin());
    OVERLAPPED ov{};
    ov.hEvent = d->event;
    ResetEvent(d->event);
    DWORD n = 0;
    if (WriteFile(d->h, buf.data(), (DWORD)buf.size(), &n, &ov)) return;
    DWORD err = GetLastError();
    if (err == ERROR_IO_PENDING) {
        long done = finish_io(d, ov, IO_TIMEOUT_MS);
        if (done >= 0) return;
        err = GetLastError();
    }
    throw HidppError(with_error(d->path + ": write failed", err));
}

Bytes hid_read(HidDev* d, int timeout_ms) {
    Bytes buf(d->in_len);
    OVERLAPPED ov{};
    ov.hEvent = d->event;
    ResetEvent(d->event);
    DWORD n = 0;
    if (!ReadFile(d->h, buf.data(), (DWORD)buf.size(), &n, &ov)) {
        DWORD err = GetLastError();
        if (err != ERROR_IO_PENDING) throw HidppError(with_error(d->path + ": read failed", err));
        long done = finish_io(d, ov, (DWORD)std::max(timeout_ms, 0));
        if (done < 0 && GetLastError() != ERROR_OPERATION_ABORTED)
            throw HidppError(with_error(d->path + ": read failed"));
        n = done > 0 ? (DWORD)done : 0;
    }
    buf.resize(n);
    return buf;
}
