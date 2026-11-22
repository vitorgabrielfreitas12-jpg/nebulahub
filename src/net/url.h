#pragma once

#include <string>

inline bool validHttpsUrl(const std::string& url) {
    if (url.size() <= 8 || url.size() > 2048 || url.compare(0, 8, "https://") != 0)
        return false;
    for (unsigned char c : url) if (c <= 32 || c == 127 || c == '\\') return false;
    const auto host = url.substr(8, url.find_first_of("/?#", 8) - 8);
    return !host.empty() && host.find('@') == std::string::npos;
}

// Resolve Location without allowing redirects to clear-text HTTP or userinfo.
inline std::string httpsRedirect(const std::string& current, const std::string& location) {
    if (location.empty() || !validHttpsUrl(current)) return {};
    if (validHttpsUrl(location)) return location;
    if (location.compare(0, 2, "//") == 0) {
        const auto absolute = "https:" + location;
        return validHttpsUrl(absolute) ? absolute : std::string{};
    }
    if (location.find(':') < location.find_first_of("/?#")) return {};
    const auto origin = current.substr(0, current.find_first_of("/?#", 8));
    const auto base = current.substr(0, current.find_first_of("?#", 8));
    std::string absolute;
    if (location.front() == '/') absolute = origin + location;
    else if (location.front() == '?') absolute = base + location;
    else if (location.front() == '#') return {};
    else absolute = (base.size() == origin.size() ? origin + "/" :
                     base.substr(0, base.rfind('/') + 1)) + location;
    return validHttpsUrl(absolute) ? absolute : std::string{};
}
