#include "forearm/chat_store.h"

#include <cstdlib>
#include <iostream>
#include <string>

#define CHECK(condition) do { if (!(condition)) { \
  std::cerr << __FILE__ << ':' << __LINE__ << ": " #condition "\n"; \
  std::exit(1); } } while (false)

using forearm::ChatStore;
using forearm::InsertResult;
using forearm::MessageInput;

static MessageInput input(std::string_view id, std::string_view channel = "room",
                          std::string_view user = "viewer") {
  return {channel, id, user, "Viewer", "A readable message", 1234};
}

static void pause_and_eviction() {
  ChatStore<3> store;
  CHECK(store.append(input("one")) == InsertResult::inserted);
  CHECK(store.append(input("two")) == InsertResult::inserted);
  CHECK(store.anchorSequence() == 2);
  CHECK(store.pauseAt(1));
  CHECK(store.append(input("three")) == InsertResult::inserted);
  CHECK(store.anchorSequence() == 1 && store.unread() == 1);
  CHECK(!store.anchorEvicted());
  CHECK(store.append(input("four")) == InsertResult::inserted);
  CHECK(store.size() == 3 && store.at(0)->sequence == 2);
  CHECK(store.anchorSequence() == 1 && store.anchorEvicted());
  CHECK(store.unread() == 2);
  CHECK(!store.pauseAt(999));
  CHECK(store.pauseAt(2));
  CHECK(!store.anchorEvicted() && store.unread() == 2);
  store.jumpLive();
  CHECK(!store.paused() && store.unread() == 0 && store.anchorSequence() == 4);
  CHECK(store.append(input("five")) == InsertResult::inserted);
  CHECK(store.anchorSequence() == 5 && store.unread() == 0);
  CHECK(store.at(3) == nullptr);

  ChatStore<1> empty;
  empty.pause();
  empty.append(input("first"));
  CHECK(empty.paused() && empty.anchorSequence() == 0 && empty.unread() == 1);
  CHECK(!empty.anchorEvicted());
  empty.jumpLive();
  CHECK(empty.anchorSequence() == 1);
}

static void duplicate_and_tombstone() {
  ChatStore<8> store;
  CHECK(store.append(input("same")) == InsertResult::inserted);
  store.pause();
  CHECK(store.append(input("same")) == InsertResult::duplicate);
  CHECK(store.size() == 1 && store.unread() == 0);
  CHECK(store.append(input("same", "other")) == InsertResult::inserted);
  CHECK(store.append(input("second", "room", "other-viewer")) == InsertResult::inserted);
  CHECK(store.eraseMessage("room", "same") == 1);
  CHECK(store.at(0)->deleted && store.at(0)->text.empty());
  CHECK(!store.at(1)->deleted);
  CHECK(store.append(input("same")) == InsertResult::duplicate);
  CHECK(store.eraseMessage("room", "same") == 0);
  CHECK(store.clearUser("room", "other-viewer") == 1);
  CHECK(store.at(2)->deleted && !store.at(1)->deleted);
  CHECK(store.clearChannel("other") == 1);
  CHECK(store.size() == 3 && store.unread() == 2);
  CHECK(!store.select(1));
}

static void stable_selection() {
  ChatStore<2> store;
  store.append(input("target"));
  CHECK(store.select(1));
  CHECK(store.paused() && store.selected()->message_id.view() == "target");
  store.append(input("next"));
  store.append(input("newest"));
  CHECK(store.find(1) == nullptr && store.anchorEvicted());
  CHECK(store.selected()->message_id.view() == "target");
  CHECK(store.selected()->user_id.view() == "viewer");
  CHECK(store.append(input("target")) == InsertResult::duplicate);
  CHECK(store.size() == 2 && store.unread() == 2);
  CHECK(store.eraseMessage("room", "target") == 0); // Evicted, but snapshot updates.
  CHECK(store.selected()->deleted && store.selected()->text.empty());
  CHECK(!store.select(999));
  CHECK(store.selected()->message_id.view() == "target");
  store.clearSelection();
  CHECK(store.selected() == nullptr && store.paused());
  CHECK(store.select(3));
  store.clearChannel("room");
  CHECK(store.selected()->deleted && store.selected()->text.empty());
  store.jumpLive();
  CHECK(!store.paused() && store.selected() == nullptr);
}

static void unicode_and_identity_bounds() {
  forearm::FixedText<6> text;
  CHECK(text.assignUtf8("ab\xF0\x9F\x98\x80" "z")); // Emoji cannot fit, nor later z.
  CHECK(text.view() == "ab");
  CHECK(text.truncated());
  CHECK(text.assignUtf8("a\xF0\x9F\x98\x80"));
  CHECK(text.view().size() == 5 && text.c_str()[5] == '\0');
  CHECK(!text.truncated());
  CHECK(!text.assignUtf8("\xED\xA0\x80")); // Surrogate.
  CHECK(!text.assignUtf8("\xC0\xAF")); // Overlong.
  CHECK(!text.assignUtf8("\xF4\x90\x80\x80")); // Above U+10FFFF.
  CHECK(!text.assignUtf8("\xE2\x82")); // Incomplete.
  CHECK(!text.assignUtf8("a\x80"));
  CHECK(!text.assignUtf8("abcdef\x80")); // Invalid even in truncated suffix.
  CHECK(!text.assignUtf8(std::string_view("a\0b", 3)));
  CHECK(text.empty());

  ChatStore<2> store;
  std::string long_id(65, 'x');
  auto bad = input(long_id);
  CHECK(store.append(bad) == InsertResult::invalid && store.size() == 0);
  CHECK(store.append(input("")) == InsertResult::invalid);
  CHECK(store.append(input("with space")) == InsertResult::invalid);
  auto null_id = input(std::string_view("a\0b", 3));
  CHECK(store.append(null_id) == InsertResult::invalid);
  auto unicode = input("valid");
  std::string long_name(63, 'a');
  long_name += "\xC3\xA9";
  std::string long_text(2047, 'x');
  long_text += "\xF0\x9F\x98\x80";
  unicode.display_name = long_name;
  unicode.text = long_text;
  CHECK(store.append(unicode) == InsertResult::inserted);
  CHECK(store.at(0)->display_name.view().size() == 63);
  CHECK(store.at(0)->display_name.truncated());
  CHECK(store.at(0)->text.view().size() == 2047);
  CHECK(store.at(0)->text.truncated());
  CHECK(store.at(0)->sequence == 1); // Rejected inputs consume no sequence.
  CHECK(store.at(0)->received_at_ms == 1234);

  std::string emoji_message;
  for (int i = 0; i < 500; ++i) emoji_message += "\xF0\x9F\x98\x80";
  auto maximum = input("max-unicode");
  maximum.text = emoji_message;
  CHECK(store.append(maximum) == InsertResult::inserted);
  CHECK(store.at(1)->text.view() == emoji_message);
  CHECK(!store.at(1)->text.truncated());
}

static void explicit_tombstone_identity_selection() {
  ChatStore<2> store;
  CHECK(store.append(input("removed", "channel", "author")) == InsertResult::inserted);
  CHECK(store.eraseMessage("channel", "removed") == 1);
  CHECK(!store.select(1));
  CHECK(store.select(1, true));
  CHECK(store.selected()->deleted && store.selected()->text.empty());
  CHECK(store.selected()->channel_id.view() == "channel");
  CHECK(store.selected()->message_id.view() == "removed");
  CHECK(store.selected()->user_id.view() == "author");
  store.append(input("two"));
  store.append(input("three"));
  CHECK(store.selected()->user_id.view() == "author" && store.selected()->text.empty());
  store.clearSelection();
  CHECK(!store.select(1, true)); // Evicted identities cannot be guessed.
}

int main() {
  pause_and_eviction();
  duplicate_and_tombstone();
  stable_selection();
  unicode_and_identity_bounds();
  explicit_tombstone_identity_selection();
  std::cout << "Chat store checks passed; no live Twitch operations.\n";
}
