#pragma once

// Portable preparation core. No live Twitch, network, display, or persistence code.
// Allocate large stores in PSRAM on the device; all storage here is fixed capacity.
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

namespace forearm {

template <std::size_t Bytes>
class FixedText {
  static_assert(Bytes > 0, "A terminator needs one byte");
 public:
  std::string_view view() const { return {data_.data(), size_}; }
  const char* c_str() const { return data_.data(); }
  bool empty() const { return size_ == 0; }
  bool truncated() const { return truncated_; }
  void clear() { data_.fill(0); size_ = 0; truncated_ = false; }

  // Reject malformed UTF-8, but safely truncate valid text at a codepoint edge.
  // Validation includes the truncated suffix. Failed assignment leaves no text.
  bool assignUtf8(std::string_view text) {
    clear();
    std::size_t kept = 0;
    bool full = false;
    for (std::size_t i = 0; i < text.size();) {
      const auto a = static_cast<unsigned char>(text[i]);
      std::size_t n = 0;
      if (a == 0) { clear(); return false; } // Keep c_str() and view() consistent.
      if (a < 0x80) n = 1;
      else if (a >= 0xC2 && a <= 0xDF) n = 2;
      else if (a >= 0xE0 && a <= 0xEF) n = 3;
      else if (a >= 0xF0 && a <= 0xF4) n = 4;
      else { clear(); return false; }
      if (i + n > text.size()) { clear(); return false; }
      for (std::size_t j = 1; j < n; ++j) {
        const auto b = static_cast<unsigned char>(text[i + j]);
        if ((b & 0xC0) != 0x80) { clear(); return false; }
      }
      if (n >= 3) {
        const auto b = static_cast<unsigned char>(text[i + 1]);
        if ((a == 0xE0 && b < 0xA0) || (a == 0xED && b >= 0xA0) ||
            (a == 0xF0 && b < 0x90) || (a == 0xF4 && b >= 0x90)) {
          clear(); return false;
        }
      }
      if (!full && kept + n < Bytes) {
        for (std::size_t j = 0; j < n; ++j) data_[kept++] = text[i + j];
      } else {
        full = true;  // Never skip a long codepoint then copy later bytes.
      }
      i += n;
    }
    size_ = kept;
    truncated_ = full;
    data_[kept] = '\0';
    return true;
  }

  // IDs must be exact: truncation could moderate a different message or person.
  bool assignId(std::string_view id) {
    clear();
    if (id.empty() || id.size() >= Bytes) return false;
    for (const unsigned char c : id) if (c < 0x21 || c > 0x7E) return false;
    for (std::size_t i = 0; i < id.size(); ++i) data_[i] = id[i];
    size_ = id.size();
    return true;
  }

 private:
  std::array<char, Bytes> data_{};
  std::size_t size_ = 0;
  bool truncated_ = false;
};

struct MessageInput {
  std::string_view channel_id;
  std::string_view message_id;
  std::string_view user_id;
  std::string_view display_name;
  std::string_view text;
  std::uint64_t received_at_ms = 0;
};

struct Message {
  FixedText<33> channel_id;
  FixedText<65> message_id;
  FixedText<33> user_id;
  FixedText<65> display_name;
  // Twitch permits 500 characters: even 500 four-byte Unicode codepoints fit.
  FixedText<2049> text;
  std::uint64_t sequence = 0;
  std::uint64_t received_at_ms = 0;
  bool deleted = false;
};

enum class InsertResult { inserted, duplicate, invalid, sequence_exhausted };

template <std::size_t Capacity>
class ChatStore {
  static_assert(Capacity > 0, "History must retain at least one message");
 public:
  static constexpr std::size_t capacity() { return Capacity; }
  std::size_t size() const { return size_; }
  bool paused() const { return paused_; }
  std::uint32_t unread() const { return unread_; }
  std::uint64_t anchorSequence() const { return anchor_; }
  bool anchorEvicted() const { return paused_ && anchor_ != 0 && !find(anchor_); }

  // Borrowed pointers are valid only until the next store mutation. The selected
  // target below is a separate snapshot and survives history eviction.
  const Message* at(std::size_t oldest_relative_index) const {
    return oldest_relative_index < size_
        ? &messages_[(start_ + oldest_relative_index) % Capacity] : nullptr;
  }
  const Message* find(std::uint64_t sequence) const {
    for (std::size_t i = 0; i < size_; ++i)
      if (at(i)->sequence == sequence) return at(i);
    return nullptr;
  }
  const Message* selected() const { return selected_ ? &selection_ : nullptr; }

  InsertResult append(const MessageInput& input) {
    Message message;
    if (!message.channel_id.assignId(input.channel_id) ||
        !message.message_id.assignId(input.message_id) ||
        !message.user_id.assignId(input.user_id) ||
        !message.display_name.assignUtf8(input.display_name) ||
        !message.text.assignUtf8(input.text)) return InsertResult::invalid;
    // Dedupe the retained window, including tombstones, namespaced by channel.
    // EventSub envelope dedupe/replay-gap reporting belongs to the transport.
    for (std::size_t i = 0; i < size_; ++i) {
      if (sameMessage(*at(i), input.channel_id, input.message_id))
        return InsertResult::duplicate;
    }
    // The selected snapshot must not be replaced by a late duplicate after its
    // history slot is evicted, including one received after a delete event.
    if (selected_ && sameMessage(selection_, input.channel_id, input.message_id))
      return InsertResult::duplicate;
    if (next_sequence_ == std::numeric_limits<std::uint64_t>::max())
      return InsertResult::sequence_exhausted;
    message.sequence = next_sequence_++;
    message.received_at_ms = input.received_at_ms;
    if (size_ == Capacity) {
      messages_[start_] = message;
      start_ = (start_ + 1) % Capacity;
    } else {
      messages_[(start_ + size_) % Capacity] = message;
      ++size_;
    }
    if (paused_) {
      if (unread_ != std::numeric_limits<std::uint32_t>::max()) ++unread_;
    } else {
      anchor_ = message.sequence;
    }
    return InsertResult::inserted;
  }

  // Pausing an empty store remains empty while arrivals accumulate as unread.
  void pause() {
    if (!paused_) { paused_ = true; unread_ = 0; }
  }
  bool pauseAt(std::uint64_t sequence) {
    if (!find(sequence)) return false;
    pause();
    anchor_ = sequence;
    return true;
  }
  // Explicitly exits selection, clears unread, and returns to the newest row.
  void jumpLive() {
    selected_ = false;
    selection_ = Message{};
    paused_ = false;
    unread_ = 0;
    anchor_ = size_ ? at(size_ - 1)->sequence : 0;
  }
  // Moderation may select a tombstone's retained identity. Its content remains
  // scrubbed, and message deletion must still be disabled by the action policy.
  bool select(std::uint64_t sequence, bool allow_deleted = false) {
    const auto* message = find(sequence);
    if (!message || (message->deleted && !allow_deleted)) return false;
    selection_ = *message;
    selected_ = true;
    pause();
    anchor_ = sequence;
    return true;
  }
  // Closing a sheet never unexpectedly starts autoscroll.
  void clearSelection() { selected_ = false; selection_ = Message{}; }

  std::size_t eraseMessage(std::string_view channel, std::string_view message) {
    return tombstone([&](const Message& m) { return sameMessage(m, channel, message); });
  }
  std::size_t clearUser(std::string_view channel, std::string_view user) {
    return tombstone([&](const Message& m) {
      return m.channel_id.view() == channel && m.user_id.view() == user;
    });
  }
  std::size_t clearChannel(std::string_view channel) {
    return tombstone([&](const Message& m) { return m.channel_id.view() == channel; });
  }

 private:
  static bool sameMessage(const Message& m, std::string_view channel,
                          std::string_view message) {
    return m.channel_id.view() == channel && m.message_id.view() == message;
  }
  template <typename Predicate>
  std::size_t tombstone(Predicate matches) {
    std::size_t changed = 0;
    for (std::size_t i = 0; i < size_; ++i) {
      auto& m = messages_[(start_ + i) % Capacity];
      if (matches(m) && !m.deleted) { m.deleted = true; m.text.clear(); ++changed; }
    }
    if (selected_ && matches(selection_)) {
      selection_.deleted = true;
      selection_.text.clear();
    }
    return changed;  // Counts retained rows, not the additional selection copy.
  }

  std::array<Message, Capacity> messages_{};
  std::size_t start_ = 0;
  std::size_t size_ = 0;
  std::uint64_t next_sequence_ = 1;
  bool paused_ = false;
  std::uint32_t unread_ = 0;  // Arrivals since pause, even if history later evicts.
  std::uint64_t anchor_ = 0;
  bool selected_ = false;
  Message selection_{};
};

}  // namespace forearm
