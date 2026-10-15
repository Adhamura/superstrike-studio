// https_get for macOS: the system libcurl (system TLS and certificates, no extra libraries).
#include <curl/curl.h>

#include "http.hpp"

static size_t append(char* data, size_t size, size_t n, void* user) {
    auto* body = static_cast<std::string*>(user);
    if (body->size() > (4u << 20)) return 0;  // 4 MB is plenty for one release
    body->append(data, size * n);
    return size * n;
}

HttpResult https_get(const std::string& host, const std::string& path, const std::string& user_agent) {
    HttpResult res;
    CURL* c = curl_easy_init();
    if (!c) {
        res.error = "curl_easy_init failed";
        return res;
    }
    std::string url = "https://" + host + path;
    curl_slist* headers = curl_slist_append(nullptr, "Accept: application/vnd.github+json");
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_USERAGENT, user_agent.c_str());
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);  // runs on a worker thread
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, append);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &res.body);
    CURLcode rc = curl_easy_perform(c);
    if (rc == CURLE_OK) {
        long status = 0;
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
        res.status = (int)status;
    } else {
        res.error = curl_easy_strerror(rc);
        res.body.clear();
    }
    curl_slist_free_all(headers);
    curl_easy_cleanup(c);
    return res;
}
