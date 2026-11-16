#pragma once
#include "forearm/media.h"
#include <algorithm>

namespace forearm {
// Display metadata only. Never use these captions as moderation authority.
inline const char *badge_role(std::string_view set) {
    if (set == "moderator")
        return "MOD";
    if (set == "broadcaster")
        return "HOST";
    if (set == "vip")
        return "VIP";
    if (set == "subscriber")
        return "SUB";
    if (set == "founder")
        return "FOUNDER";
    if (set == "artist")
        return "ARTIST";
    if (set == "premium")
        return "PRIME";
    if (set == "turbo")
        return "TURBO";
    return nullptr;
}

inline void append_badge_caption(char *output, std::size_t bytes, std::string_view set, std::string_view version,
                                 std::string_view info) {
    if (!output || !bytes)
        return;
    const auto used = strnlen(output, bytes);
    if (used == bytes)
        return;
    const auto *role = badge_role(set);
    if (!role && !ascii_identifier(set, 32))
        return;
    bool months = set == "subscriber" && !info.empty() && info.size() <= 4;
    for (const auto c : info)
        if (c < '0' || c > '9')
            months = false;
    char item[48];
    if (months)
        std::snprintf(item, sizeof(item), "[SUB %.*sm] ", int(info.size()), info.data());
    else if (role)
        std::snprintf(item, sizeof(item), "[%s] ", role);
    else if (ascii_identifier(version, 12))
        std::snprintf(item, sizeof(item), "[%.*s %.*s] ", int(std::min(set.size(), std::size_t(12))), set.data(),
                      int(version.size()), version.data());
    else
        std::snprintf(item, sizeof(item), "[%.*s] ", int(std::min(set.size(), std::size_t(12))), set.data());
    // Keep complete labels, leaving room for the terminating NUL.
    if (std::strlen(item) < bytes - used)
        std::memcpy(output + used, item, std::strlen(item) + 1);
}
} // namespace forearm
