#pragma once
#include <cstddef>
#include <cstring>
#include <string_view>

namespace forearm {
enum class LoginPollResult : unsigned char { None, Pending, SlowDown, TransportError, RateLimited, Success, Denied };

inline int login_hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
// Decode only for comparisons; the returned URL preserves the server's query
// byte-for-byte. Reject control bytes and ambiguous/malformed percent encoding.
inline bool login_query_decode(std::string_view input, char* output, std::size_t capacity) {
    std::size_t used = 0;
    for (std::size_t i = 0; i < input.size(); ++i) {
        unsigned char c = input[i];
        if (c == '%') {
            if (i + 2 >= input.size() || login_hex(input[i+1]) < 0 || login_hex(input[i+2]) < 0) return false;
            c = static_cast<unsigned char>(login_hex(input[i+1]) * 16 + login_hex(input[i+2])); i += 2;
        } else if (c == '+') c = ' ';
        if (c < 0x20 || c == 0x7f || used + 1 >= capacity) return false;
        output[used++] = static_cast<char>(c);
    }
    output[used] = 0; return true;
}
// Public Device Code authorization requires the Public activation route. Some
// Twitch device responses omit that flag; append it without rewriting any
// provided code/query. Only the documented activation origin/path is accepted.
inline bool normalize_public_device_url(std::string_view input, char* output, std::size_t capacity) {
    if (!output || !capacity) return false;
    output[0] = 0;
    constexpr std::string_view base = "https://www.twitch.tv/activate";
    if (input.size() < base.size() || input.substr(0, base.size()) != base || input.size() >= capacity ||
        (input.size() > base.size() && input[base.size()] != '?')) return false;
    for (unsigned char c : input) if (c <= 0x20 || c >= 0x7f || c == '#' || c == '\\') return false;
    bool public_seen = false;
    auto query = input.size() == base.size() ? std::string_view{} : input.substr(base.size() + 1);
    while (!query.empty()) {
        const auto end = query.find('&');
        const auto parameter = query.substr(0, end);
        const auto equal = parameter.find('=');
        char key[512], value[512];
        if (!login_query_decode(parameter.substr(0, equal), key, sizeof(key)) ||
            !login_query_decode(equal == std::string_view::npos ? std::string_view{} : parameter.substr(equal+1), value, sizeof(value))) return false;
        bool public_key = std::strlen(key) == 6;
        constexpr char public_name[] = "public";
        for (unsigned i = 0; public_key && i < 6; ++i) {
            const char lower = key[i] >= 'A' && key[i] <= 'Z' ? key[i] - 'A' + 'a' : key[i];
            public_key = lower == public_name[i];
        }
        if (public_key) {
            if (std::string_view(key) != "public") return false;
            if (public_seen || std::string_view(value) != "true") return false;
            public_seen = true;
        }
        if (end == std::string_view::npos) break;
        query.remove_prefix(end+1);
    }
    const auto suffix = public_seen ? std::string_view{} :
        input.size() == base.size() ? std::string_view("?public=true") :
        (input.back() == '?' || input.back() == '&') ? std::string_view("public=true") : std::string_view("&public=true");
    if (input.size() + suffix.size() >= capacity) return false;
    std::memcpy(output, input.data(), input.size());
    if (!suffix.empty()) std::memcpy(output + input.size(), suffix.data(), suffix.size());
    output[input.size() + suffix.size()] = 0;
    return true;
}
} // namespace forearm
