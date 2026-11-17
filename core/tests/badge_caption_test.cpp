#include "forearm/badge_caption.h"
#include "forearm/chat_header.h"
#include <cassert>
#include <string>

int main() {
    using Icon = forearm::BadgeIcon;
    const auto icons = forearm::header_badges("[SUB 12m] [VIP] [MOD] ");
    assert(icons.count == 3 && icons.icons[0] == Icon::Mod && icons.icons[1] == Icon::Vip &&
           icons.icons[2] == Icon::Sub);
    const auto overflow = forearm::header_badges("[MOD] [SUB 12m] [VIP] [bits 1000] ");
    assert(overflow.count == 3 && overflow.icons[2] == Icon::More);
    assert(forearm::header_badges("[MOD] [MOD] ").count == 1);
    assert(!forearm::header_badges("[unfinished").count);
    assert(forearm::username_color("#54D6C7", "a") == 0x54D6C7);
    assert(forearm::username_color("#000000", "a") == 0x808080);
    assert(forearm::username_color("garbage", "a") == forearm::username_color("", "a"));
    assert(forearm::username_color("", "a") != forearm::username_color("", "b"));

    char caption[96]{};
    forearm::append_badge_caption(caption, sizeof(caption), "moderator", "1", "");
    forearm::append_badge_caption(caption, sizeof(caption), "vip", "1", "");
    forearm::append_badge_caption(caption, sizeof(caption), "subscriber", "3012", "12");
    forearm::append_badge_caption(caption, sizeof(caption), "bits", "10000", "");
    assert(std::string(caption) == "[MOD] [VIP] [SUB 12m] [bits 10000] ");
    const std::string previous(caption);
    forearm::append_badge_caption(caption, sizeof(caption), "bad\nlabel", "1", "");
    assert(caption == previous);
    char small[7]{};
    forearm::append_badge_caption(small, sizeof(small), "moderator", "1", "");
    forearm::append_badge_caption(small, sizeof(small), "vip", "1", "");
    assert(std::string(small) == "[MOD] ");
    char invalid_months[96]{};
    forearm::append_badge_caption(invalid_months, sizeof(invalid_months), "subscriber", "1", "12evil");
    assert(std::string(invalid_months) == "[SUB] ");
    assert(forearm::badge_role("broadcaster") && !forearm::badge_role("bits"));
    char generic[96]{};
    forearm::append_badge_caption(generic, sizeof(generic), "long-channel-badge-name", "7", "");
    assert(std::string(generic) == "[long-channel 7] ");
}
