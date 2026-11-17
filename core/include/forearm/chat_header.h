#pragma once
#include <array>
#include <cstdint>
#include <string_view>

namespace forearm {
enum class BadgeIcon : uint8_t { Host, Mod, Vip, Founder, Sub, Artist, Prime, Turbo, Bits, Other, More };
struct HeaderBadges {
    std::array<BadgeIcon, 3> icons{};
    unsigned count = 0;
};
// Compact display icons only; full transport captions remain in message details.
inline HeaderBadges header_badges(std::string_view caption) {
    uint16_t mask = 0;
    while (!caption.empty()) {
        const auto start = caption.find('[');
        if (start == caption.npos)
            break;
        caption.remove_prefix(start + 1);
        const auto end = caption.find(']');
        if (end == caption.npos)
            break;
        auto token = caption.substr(0, end);
        token = token.substr(0, token.find(' '));
        const auto icon = token == "HOST"      ? BadgeIcon::Host
                          : token == "MOD"     ? BadgeIcon::Mod
                          : token == "VIP"     ? BadgeIcon::Vip
                          : token == "FOUNDER" ? BadgeIcon::Founder
                          : token == "SUB"     ? BadgeIcon::Sub
                          : token == "ARTIST"  ? BadgeIcon::Artist
                          : token == "PRIME"   ? BadgeIcon::Prime
                          : token == "TURBO"   ? BadgeIcon::Turbo
                          : token == "bits"    ? BadgeIcon::Bits
                                               : BadgeIcon::Other;
        mask |= 1u << static_cast<unsigned>(icon);
        caption.remove_prefix(end + 1);
    }
    HeaderBadges result;
    for (unsigned i = 0; i < static_cast<unsigned>(BadgeIcon::More); ++i) {
        if (!(mask & (1u << i)))
            continue;
        if (result.count == result.icons.size()) {
            result.icons.back() = BadgeIcon::More;
            break;
        }
        result.icons[result.count++] = static_cast<BadgeIcon>(i);
    }
    return result;
}
inline uint32_t badge_color(BadgeIcon icon) {
    switch (icon) {
    case BadgeIcon::Host:
        return 0xE91916;
    case BadgeIcon::Mod:
        return 0x00AD03;
    case BadgeIcon::Vip:
        return 0xD900B3;
    case BadgeIcon::Sub:
    case BadgeIcon::Founder:
        return 0x9146FF;
    case BadgeIcon::Artist:
        return 0xE87728;
    case BadgeIcon::Prime:
        return 0x007FAF;
    case BadgeIcon::Turbo:
        return 0x6441A5;
    case BadgeIcon::Bits:
        return 0x007C8A;
    case BadgeIcon::More:
        return 0x007C8A;
    default:
        return 0x9A6700;
    }
}
inline uint32_t username_color(std::string_view supplied, std::string_view name) {
    uint32_t rgb = 0;
    bool valid = supplied.size() == 7 && supplied[0] == '#';
    if (valid)
        for (unsigned i = 1; i < 7; ++i) {
            const auto c = supplied[i];
            const int digit = c >= '0' && c <= '9'   ? c - '0'
                              : c >= 'a' && c <= 'f' ? c - 'a' + 10
                              : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                     : -1;
            if (digit < 0) {
                valid = false;
                break;
            }
            rgb = (rgb << 4) | digit;
        }
    if (!valid) {
        constexpr uint32_t palette[] = {0xC59CFF, 0x54D6C7, 0xFF9E8A, 0x7EB6FF, 0xEAA0E8, 0xA7D77B, 0xF3CA73, 0x81D4FA};
        uint32_t hash = 2166136261u;
        for (const unsigned char c : name)
            hash = (hash ^ c) * 16777619u;
        return palette[hash % 8];
    }
    // Lift dark custom colors without changing the relative channel ordering.
    unsigned r = rgb >> 16, g = (rgb >> 8) & 255, b = rgb & 255;
    const auto luminance = (54 * r + 183 * g + 19 * b) / 256;
    const auto lift = luminance < 128 ? 128 - luminance : 0;
    auto raised = [lift](unsigned c) { return c + lift > 255 ? 255 : c + lift; };
    return (raised(r) << 16) | (raised(g) << 8) | raised(b);
}
} // namespace forearm
