#pragma once
#include "forearm/media.h"
#include "forearm/twemoji_keys.h"

namespace forearm {
inline bool media_codepoint(std::string_view text, std::size_t offset, std::uint32_t& cp, std::size_t& bytes) {
    if (offset >= text.size()) return false;
    const auto first = static_cast<unsigned char>(text[offset]);
    if (first < 128) { cp = first; bytes = 1; return true; }
    unsigned count = first >= 0xc2 && first <= 0xdf ? 2 : first >= 0xe0 && first <= 0xef ? 3 : first >= 0xf0 && first <= 0xf4 ? 4 : 0;
    if (!count || text.size() - offset < count) return false;
    cp = first & (count == 2 ? 31 : count == 3 ? 15 : 7);
    for (unsigned i = 1; i < count; ++i) {
        const auto c = static_cast<unsigned char>(text[offset + i]);
        if ((c & 0xc0) != 0x80) return false;
        cp = (cp << 6) | (c & 63);
    }
    if (cp < (count == 2 ? 0x80u : count == 3 ? 0x800u : 0x10000u) || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
    bytes = count; return true;
}
// Match one complete emoji cluster at an exact UTF-8 byte offset. Unsupported,
// malformed and >16-codepoint sequences stay wholly literal, never replacing a
// supported prefix of a ZWJ/modifier/tag cluster. consumed permits bounded scan.
inline bool emoji_at(std::string_view text, std::size_t offset, MediaOccurrence& output, std::size_t& consumed) {
    consumed = 1;
    std::uint32_t cp = 0; std::size_t bytes = 0;
    if (!media_codepoint(text, offset, cp, bytes)) return false;
    consumed = bytes;
    char initial[8]; std::snprintf(initial, sizeof(initial), "%x", static_cast<unsigned>(cp));
    const bool regional = cp >= 0x1f1e6 && cp <= 0x1f1ff;
    const bool keycap = cp == '#' || cp == '*' || (cp >= '0' && cp <= '9');
    if (!regional && !keycap && !twemoji_asset(initial)) {
        std::uint32_t next = 0; std::size_t next_bytes = 0;
        if (!media_codepoint(text, offset + bytes, next, next_bytes) ||
            !(next == 0x200d || next == 0xfe0e || next == 0xfe0f || next == 0x20e3 ||
              (next >= 0x1f3fb && next <= 0x1f3ff) || (next >= 0xe0020 && next <= 0xe007f) ||
              (next >= 0x300 && next <= 0x36f))) return false;
        // Even an unsupported/future base owns its complete syntactic chain;
        // never replace a supported suffix after the joiner independently.
    }
    std::uint32_t points[16]{}; points[0] = cp;
    unsigned count = 1; bool supported = true, join_next = false, has_vs = false;
    std::size_t end = offset + bytes;
    while (end < text.size()) {
        std::uint32_t next = 0; std::size_t size = 0;
        if (!media_codepoint(text, end, next, size)) { supported = false; ++end; break; }
        const bool extension = next == 0xfe0e || next == 0xfe0f || next == 0x200d || next == 0x20e3 ||
            (next >= 0x1f3fb && next <= 0x1f3ff) || (next >= 0xe0020 && next <= 0xe007f) ||
            (next >= 0x300 && next <= 0x36f) || (regional && count == 1 && next >= 0x1f1e6 && next <= 0x1f1ff);
        if (!extension && !join_next) break;
        if ((next == 0xfe0f && has_vs) || next == 0xfe0e || (join_next && next == 0x200d)) supported = false;
        if (next == 0xfe0f) has_vs = true;
        else if (next != 0x200d) has_vs = false;
        join_next = next == 0x200d;
        if (count < 16) points[count] = next; else supported = false;
        ++count; end += size;
    }
    consumed = end - offset;
    if (!supported || join_next || count > 16 || consumed > 64 || offset > 65535) return false;
    char canonical[112]{}; std::size_t used = 0;
    for (unsigned i = 0; i < count; ++i) {
        if (points[i] == 0xfe0f) continue;
        const auto written = std::snprintf(canonical + used, sizeof(canonical) - used, "%s%x", used ? "-" : "", static_cast<unsigned>(points[i]));
        if (written < 0 || static_cast<std::size_t>(written) >= sizeof(canonical) - used) return false;
        used += written;
    }
    const auto* asset = twemoji_asset(canonical);
    if (!asset) return false;
    output = {};
    std::snprintf(output.id, sizeof(output.id), "emoji:%s", asset);
    std::memcpy(output.text, text.data() + offset, consumed); output.text[consumed] = 0;
    output.byte_offset = static_cast<std::uint16_t>(offset); output.byte_length = static_cast<std::uint16_t>(consumed); output.position_valid = true;
    return true;
}
} // namespace forearm
