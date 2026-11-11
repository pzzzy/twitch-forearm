#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace forearm {
struct FragmentByteRange {
    std::uint16_t byte_offset = 0;
    std::uint16_t byte_length = 0;
    bool position_valid = false;
};
// Twitch fragments are authoritative occurrences, not tokens to replace by
// name. Consume every fragment and require complete() before trusting any
// returned range. Offsets are UTF-8 bytes into the retained original text.
class MessageFragmentCursor {
public:
    MessageFragmentCursor(std::string_view message, std::size_t retained_bytes)
        : message_(message), retained_(retained_bytes) {}
    FragmentByteRange consume(std::string_view fragment) {
        FragmentByteRange result{};
        if (!matches_ || offset_ > message_.size() || fragment.size() > message_.size() - offset_ ||
            message_.substr(offset_, fragment.size()) != fragment || !boundary(offset_) || !boundary(offset_ + fragment.size())) {
            matches_ = false; return result;
        }
        const auto start = offset_;
        offset_ += fragment.size();
        if (!fragment.empty() && start <= 65535 && fragment.size() <= 65535 &&
            start <= retained_ && fragment.size() <= retained_ - start) {
            result.byte_offset = static_cast<std::uint16_t>(start);
            result.byte_length = static_cast<std::uint16_t>(fragment.size());
            result.position_valid = true;
        }
        return result;
    }
    bool complete() const { return matches_ && offset_ == message_.size() && retained_ <= message_.size(); }
private:
    bool boundary(std::size_t position) const {
        return position == message_.size() || (static_cast<unsigned char>(message_[position]) & 0xC0) != 0x80;
    }
    std::string_view message_;
    std::size_t retained_, offset_ = 0;
    bool matches_ = true;
};
} // namespace forearm
