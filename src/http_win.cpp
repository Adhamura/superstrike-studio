// https_get for Windows: WinHTTP (system TLS, system proxy; follows redirects by itself).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>

#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY  // older MinGW headers
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif

#include "http.hpp"

static std::wstring wide(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? (size_t)n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

static std::string last_error(const char* what) {
    return std::string(what) + " failed (Windows error " + std::to_string(GetLastError()) + ")";
}

HttpResult https_get(const std::string& host, const std::string& path, const std::string& user_agent) {
    HttpResult res;
    HINTERNET s = WinHttpOpen(wide(user_agent).c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) s = WinHttpOpen(wide(user_agent).c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                            WINHTTP_NO_PROXY_BYPASS, 0);  // before Windows 8.1
    if (!s) { res.error = last_error("WinHttpOpen"); return res; }
    WinHttpSetTimeouts(s, 15000, 15000, 15000, 15000);
    HINTERNET c = WinHttpConnect(s, wide(host).c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", wide(path).c_str(), nullptr, WINHTTP_NO_REFERER,
                                         WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
                    : nullptr;
    if (!r) {
        res.error = last_error(c ? "WinHttpOpenRequest" : "WinHttpConnect");
    } else if (!WinHttpSendRequest(r, L"Accept: application/vnd.github+json\r\n", (DWORD)-1L, WINHTTP_NO_REQUEST_DATA,
                                   0, 0, 0) ||
               !WinHttpReceiveResponse(r, nullptr)) {
        res.error = last_error("HTTPS request");
    } else {
        DWORD code = 0, size = sizeof code;
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code,
                            &size, WINHTTP_NO_HEADER_INDEX);
        res.status = (int)code;
        for (;;) {
            DWORD avail = 0, got = 0;
            if (!WinHttpQueryDataAvailable(r, &avail) || avail == 0 || res.body.size() > (4u << 20)) break;
            std::string chunk(avail, '\0');
            if (!WinHttpReadData(r, chunk.data(), avail, &got) || got == 0) break;
            res.body.append(chunk.data(), got);
        }
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return res;
}
