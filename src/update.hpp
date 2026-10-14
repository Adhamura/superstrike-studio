// Update check: asks GitHub for the latest release of SuperStrike Studio and offers its page.
// One HTTPS GET to api.github.com; nothing else is sent. See README "Updates".
#pragma once
#include <string>

struct App;

constexpr const char* UPDATE_REPO = "Adhamura/SuperStrike-Studio";

struct ReleaseInfo {
    std::string version;  // "1.2.0" (tag without the "v")
    std::string url;      // release page
    std::string notes;    // release notes as plain text, without the install part
};

bool version_newer(const std::string& a, const std::string& b);  // a > b, both "x.y.z"
// Blocking; 0 = got the latest release, 1 = no connection / TLS error, 2 = HTTP or data error.
int fetch_latest_release(ReleaseInfo& out, std::string& error);
// manual = false: the start-up check (only when on in Config, at most once a day, quiet on errors).
void update_check(App& app, bool manual);
