// One HTTPS GET, blocking (call it off the main thread). Used only by the update check.
// Linux: GIO sockets + TLS (glib-networking). Windows: WinHTTP (system TLS and proxy settings).
#pragma once
#include <string>

struct HttpResult {
    int status = 0;     // HTTP status, 0 when no answer came
    std::string body;
    std::string error;  // transport error (no connection, TLS, timeout), empty otherwise
};

HttpResult https_get(const std::string& host, const std::string& path, const std::string& user_agent);
