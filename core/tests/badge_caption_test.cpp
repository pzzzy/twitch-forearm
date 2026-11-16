#include "forearm/badge_caption.h"
#include <cassert>
#include <string>

int main() {
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
