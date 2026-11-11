#pragma once
#include "forearm/emoji_sequence.h"
#include <algorithm>

namespace forearm {
// Lookup returns a stable namespaced ID for an exact, case-sensitive catalog
// alias, or nullptr. Only available occurrence capacity is consumed; original
// text and pre-existing native occurrences are never rewritten.
template<class Lookup>
unsigned collect_media(std::string_view message, MediaOccurrence* output, unsigned count,
                       unsigned capacity, Lookup lookup) {
    capacity = std::min(capacity, 8u); count = std::min(count, capacity);
    if (!output || message.size() > 2048) return count;
    for (std::size_t offset = 0; offset < message.size() && count < capacity;) {
        MediaOccurrence emoji;
        std::size_t consumed = 1;
        if (emoji_at(message, offset, emoji, consumed)) {
            if (!spans_overlap(offset, consumed, output, count)) output[count++] = emoji;
            offset += consumed; continue;
        }
        if (consumed > 4) { offset += consumed; continue; }
        const char* best = nullptr; std::size_t length = 0;
        if (!media_space(static_cast<unsigned char>(message[offset])) &&
            (!offset || !media_word(static_cast<unsigned char>(message[offset - 1])))) {
            for (std::size_t bytes = 1; bytes <= 64 && bytes <= message.size() - offset; ++bytes) {
                if (media_space(static_cast<unsigned char>(message[offset + bytes - 1]))) break;
                if (!media_boundaries(message, offset, bytes)) continue;
                if (const auto* id = lookup(message.substr(offset, bytes))) { best = id; length = bytes; }
            }
        }
        if (best && std::strlen(best) < sizeof(output[0].id) && !spans_overlap(offset, length, output, count)) {
            auto& occurrence = output[count++]; occurrence = {};
            std::strcpy(occurrence.id, best); std::memcpy(occurrence.text, message.data() + offset, length);
            occurrence.byte_offset = offset; occurrence.byte_length = length; occurrence.position_valid = true;
            offset += length;
        } else offset += consumed;
    }
    return count;
}
} // namespace forearm
