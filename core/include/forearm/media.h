#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace forearm {
constexpr std::size_t kMediaIdBytes = 129;
struct MediaOccurrence {
    char id[kMediaIdBytes]{};
    char text[65]{};
    std::uint16_t byte_offset = 0, byte_length = 0;
    bool position_valid = false;
};
inline bool ascii_identifier(std::string_view value, std::size_t limit) {
    if (value.empty() || value.size() > limit) return false;
    for (const unsigned char c : value)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    return true;
}
inline bool seventv_id(std::string_view value) {
    if (value.size() == 24) {
        for (const auto c : value) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
        return true;
    }
    if (value.size() != 26) return false;
    for (const auto c : value)
        if (std::string_view("0123456789ABCDEFGHJKMNPQRSTVWXYZ").find(c) == std::string_view::npos) return false;
    return true;
}
inline bool emoji_key(std::string_view value) {
    if (value.empty() || value.size() > 111 || value.front() == '-' || value.back() == '-') return false;
    unsigned digits = 0, components = 1;
    for (const auto c : value) {
        if (c == '-') { if (!digits || ++components > 16) return false; digits = 0; }
        else if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')) { if (++digits > 6) return false; }
        else return false;
    }
    return digits != 0;
}
// Fixed origins, version and paths. The caller can never supply a URL or query.
inline bool media_url(std::string_view id, char* output, std::size_t bytes) {
    int count = -1;
    if (id.substr(0, 4) == "7tv:") {
        auto object = id.substr(4); const bool animated = object.size() > 2 && object.substr(object.size() - 2) == ":a";
        if (animated) object.remove_suffix(2);
        if (!seventv_id(object)) return false;
        count = std::snprintf(output, bytes, "https://cdn.7tv.app/emote/%.*s/1x%s.png",
                              static_cast<int>(object.size()), object.data(), animated ? "_static" : "");
    } else if (id.substr(0, 6) == "emoji:") {
        const auto key = id.substr(6);
        if (!emoji_key(key)) return false;
        count = std::snprintf(output, bytes, "https://cdn.jsdelivr.net/gh/jdecked/twemoji@388f518b756404152f128a3539f5e7f9a6f1ebd6/assets/72x72/%.*s.png",
                              static_cast<int>(key.size()), key.data());
    } else {
        if (!ascii_identifier(id, 64)) return false;
        count = std::snprintf(output, bytes, "https://static-cdn.jtvnw.net/emoticons/v2/%.*s/static/dark/1.0",
                              static_cast<int>(id.size()), id.data());
    }
    return count > 0 && static_cast<std::size_t>(count) < bytes;
}
inline bool spans_overlap(std::size_t start, std::size_t length, const MediaOccurrence* existing, unsigned count) {
    for (unsigned i = 0; i < count; ++i) {
        const auto& e = existing[i];
        if (e.position_valid && start < e.byte_offset + e.byte_length && e.byte_offset < start + length) return true;
    }
    return false;
}
inline bool media_word(unsigned char c) {
    return c >= 128 || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}
inline bool media_space(unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
inline bool media_boundaries(std::string_view text, std::size_t offset, std::size_t length) {
    return length && offset <= text.size() && length <= text.size() - offset &&
           (!offset || !media_word(static_cast<unsigned char>(text[offset - 1]))) &&
           (offset + length == text.size() || !media_word(static_cast<unsigned char>(text[offset + length])));
}
inline void media_dimensions(unsigned source_width, unsigned source_height, unsigned& width, unsigned& height) {
    width = source_width; height = source_height;
    if (height > 48) { width = (source_width * 48 + source_height / 2) / source_height; height = 48; }
    if (width > 128) { height = (height * 128 + width / 2) / width; width = 128; }
    if (!width) width = 1;
    if (!height) height = 1;
}
} // namespace forearm
