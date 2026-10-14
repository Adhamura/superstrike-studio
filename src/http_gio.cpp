// https_get for Linux: HTTP/1.1 (Connection: close) over a GIO TLS connection, up to 3 redirects.
#include <cstdlib>

#include <gio/gio.h>

#include "http.hpp"

// Transfer-Encoding: chunked -> plain body.
static std::string dechunk(const std::string& in) {
    std::string out;
    size_t at = 0;
    for (;;) {
        size_t eol = in.find("\r\n", at);
        if (eol == std::string::npos) break;
        unsigned long n = std::strtoul(in.c_str() + at, nullptr, 16);
        if (n == 0 || eol + 2 + n > in.size()) break;
        out.append(in, eol + 2, n);
        at = eol + 2 + n + 2;
    }
    return out;
}

HttpResult https_get(const std::string& host_in, const std::string& path_in, const std::string& user_agent) {
    HttpResult res;
    std::string host = host_in, path = path_in;
    for (int hop = 0; hop < 4; hop++) {
        GSocketClient* client = g_socket_client_new();
        g_socket_client_set_tls(client, TRUE);
        g_socket_client_set_timeout(client, 15);
        GError* e = nullptr;
        GSocketConnection* conn = g_socket_client_connect_to_host(client, host.c_str(), 443, nullptr, &e);
        g_object_unref(client);
        if (!conn) {
            res.error = e ? e->message : "no connection";
            g_clear_error(&e);
            return res;
        }
        std::string req = "GET " + path + " HTTP/1.1\r\nHost: " + host + "\r\nUser-Agent: " + user_agent +
                          "\r\nAccept: application/vnd.github+json\r\nConnection: close\r\n\r\n";
        GOutputStream* out = g_io_stream_get_output_stream(G_IO_STREAM(conn));
        std::string data;
        if (g_output_stream_write_all(out, req.data(), req.size(), nullptr, nullptr, &e)) {
            GInputStream* in = g_io_stream_get_input_stream(G_IO_STREAM(conn));
            char buf[8192];
            gssize n;
            while ((n = g_input_stream_read(in, buf, sizeof buf, nullptr, &e)) > 0 && data.size() < (4u << 20))
                data.append(buf, (size_t)n);
        }
        g_io_stream_close(G_IO_STREAM(conn), nullptr, nullptr);
        g_object_unref(conn);
        if (e && data.empty()) {
            res.error = e->message;
            g_clear_error(&e);
            return res;
        }
        g_clear_error(&e);  // a TLS close without close_notify after the body is fine

        size_t end = data.find("\r\n\r\n");
        if (end == std::string::npos || data.compare(0, 5, "HTTP/") != 0) {
            res.error = "bad answer from " + host;
            return res;
        }
        std::string head = data.substr(0, end);
        res.status = std::atoi(head.c_str() + head.find(' ') + 1);
        res.body = data.substr(end + 4);
        std::string lower = head;
        for (char& c : lower) c = (char)g_ascii_tolower(c);
        if (lower.find("\r\ntransfer-encoding: chunked") != std::string::npos) res.body = dechunk(res.body);
        if (res.status < 300 || res.status >= 400) return res;
        // redirect: follow an https Location
        size_t at = lower.find("\r\nlocation:");
        if (at == std::string::npos) return res;
        std::string loc = head.substr(at + 11, head.find("\r\n", at + 2) - at - 11);
        loc.erase(0, loc.find_first_not_of(" \t"));
        if (loc.rfind("https://", 0) == 0) {
            size_t slash = loc.find('/', 8);
            host = loc.substr(8, slash == std::string::npos ? std::string::npos : slash - 8);
            path = slash == std::string::npos ? "/" : loc.substr(slash);
        } else if (!loc.empty() && loc[0] == '/') {
            path = loc;
        } else {
            return res;
        }
    }
    res.error = "too many redirects";
    return res;
}
