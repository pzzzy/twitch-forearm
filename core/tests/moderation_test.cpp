#include "forearm/moderation.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

using namespace forearm;
namespace {
unsigned assertions = 0;
void check(bool value, const char* expression, unsigned line) {
    ++assertions;
    if (!value) { std::cerr << "line " << line << ": " << expression << '\n'; std::exit(1); }
}
#define CHECK(value) check((value), #value, __LINE__)
template <std::size_t N> void put(char (&out)[N], const char* value) {
    CHECK(std::strlen(value) < N); std::strcpy(out, value);
}
ModerationCapabilities caps() {
    ModerationCapabilities result{};
    result.authenticated = result.online = result.role_verified = result.can_delete = result.can_ban = true;
    result.generation = 7; put(result.broadcaster_id, "100"); put(result.moderator_id, "200");
    return result;
}
ModerationRequest target(ModerationAction action = ModerationAction::DeleteMessage) {
    ModerationRequest result{};
    result.request_id = 1; result.session_generation = 7; result.action = action; result.confirmed = true;
    put(result.channel_id, "100"); put(result.user_id, "300");
    put(result.message_id, "a1b2-1234-5678"); put(result.source_channel_id, "100");
    put(result.source_message_id, result.message_id);
    result.received_at_ms = 1000;
    return result;
}
void policy() {
    const auto allowed = caps();
    auto request = target();
    CHECK(moderation_validate(request, allowed, 2000) == ModerationDenial::None);
    request.confirmed = false;
    CHECK(moderation_validate(request, allowed, 2000) == ModerationDenial::ConfirmationRequired);
    request = target(); request.request_id = 0;
    CHECK(moderation_validate(request, allowed, 2000) == ModerationDenial::InvalidIdentifier);
    auto changed = allowed; changed.authenticated = false;
    CHECK(moderation_validate(target(), changed, 2000) == ModerationDenial::NotAuthenticated);
    changed = allowed; changed.online = false;
    CHECK(moderation_validate(target(), changed, 2000) == ModerationDenial::Offline);
    changed = allowed; ++changed.generation;
    CHECK(moderation_validate(target(), changed, 2000) == ModerationDenial::StaleSession);
    changed = allowed; changed.role_verified = false;
    CHECK(moderation_validate(target(), changed, 2000) == ModerationDenial::RoleMissing);
    changed = allowed; changed.can_delete = false;
    CHECK(moderation_validate(target(), changed, 2000) == ModerationDenial::ScopeMissing);
    changed = allowed; changed.can_ban = false;
    CHECK(moderation_validate(target(ModerationAction::Ban), changed, 2000) == ModerationDenial::ScopeMissing);
    request = target(); put(request.channel_id, "999");
    CHECK(moderation_validate(request, allowed, 2000) == ModerationDenial::WrongChannel);
    request = target(); put(request.source_channel_id, "999");
    CHECK(moderation_validate(request, allowed, 2000) == ModerationDenial::SharedSource);
    request = target(); request.source_channel_id[0] = 0;
    CHECK(moderation_validate(request, allowed, 2000) == ModerationDenial::SharedSource);
    for (const auto action : {ModerationAction::DeleteMessage, ModerationAction::Timeout, ModerationAction::Ban}) {
        request = target(action); request.timeout_seconds = action == ModerationAction::Timeout ? 30 : 0;
        put(request.source_message_id, "different-origin-message");
        CHECK(moderation_validate(request, allowed, 2000) == ModerationDenial::SharedSource);
    }
    request = target(); request.protected_target = true;
    CHECK(moderation_validate(request, allowed, 2000) == ModerationDenial::ProtectedTarget);
    request = target(); put(request.user_id, allowed.broadcaster_id);
    CHECK(moderation_validate(request, allowed, 2000) == ModerationDenial::ProtectedTarget);
    request = target(); put(request.user_id, allowed.moderator_id);
    CHECK(moderation_validate(request, allowed, 2000) == ModerationDenial::ProtectedTarget);
    request = target(); request.message_id[0] = 0;
    CHECK(moderation_validate(request, allowed, 2000) == ModerationDenial::InvalidIdentifier);
    request = target(); put(request.message_id, "id&message_id=");
    CHECK(moderation_validate(request, allowed, 2000) == ModerationDenial::InvalidIdentifier);
    request = target(); put(request.user_id, "300&user_id=400");
    CHECK(moderation_validate(request, allowed, 2000) == ModerationDenial::InvalidIdentifier);
    request = target(); std::memset(request.channel_id, '1', sizeof(request.channel_id));
    CHECK(moderation_validate(request, allowed, 2000) == ModerationDenial::InvalidIdentifier);
    request = target(); request.timeout_seconds = 30;
    CHECK(moderation_validate(request, allowed, 2000) == ModerationDenial::InvalidDuration);
    request = target(); request.message_deleted = true;
    CHECK(moderation_validate(request, allowed, 2000) == ModerationDenial::MessageDeleted);
    CHECK(moderation_validate(target(), allowed, 1000 + 6ULL * 3600000) == ModerationDenial::MessageTooOld);
    CHECK(moderation_validate(target(), allowed, 999 + 6ULL * 3600000) == ModerationDenial::None);
    for (const auto seconds : {0u, 1u, 29u, 31u, 1209600u, 0xffffffffu}) {
        request = target(ModerationAction::Timeout); request.timeout_seconds = seconds;
        CHECK(moderation_validate(request, allowed, 2000) == ModerationDenial::InvalidDuration);
    }
    for (const auto seconds : {30u, 60u, 600u}) {
        request = target(ModerationAction::Timeout); request.timeout_seconds = seconds;
        CHECK(moderation_validate(request, allowed, 2000) == ModerationDenial::None);
    }
    request = target(ModerationAction::Ban); request.timeout_seconds = 30;
    CHECK(moderation_validate(request, allowed, 2000) == ModerationDenial::InvalidDuration);
    request = target(ModerationAction::Unban); request.message_id[0] = request.source_message_id[0] = 0;
    CHECK(moderation_validate(request, allowed, 2000) == ModerationDenial::None);
}
void identity_and_wire_contract() {
    const auto allowed = caps();
    auto request = target();
    ModerationHttpRequest wire;
    CHECK(moderation_build_http(request, allowed, 2000, wire));
    CHECK(wire.method == ModerationHttpMethod::Delete);
    CHECK(std::string(wire.url) == "https://api.twitch.tv/helix/moderation/chat?broadcaster_id=100&moderator_id=200&message_id=a1b2-1234-5678");
    CHECK(wire.body[0] == 0);
    request.message_id[0] = 0;
    CHECK(!moderation_build_http(request, allowed, 2000, wire));
    CHECK(wire.url[0] == 0); // Never silently becomes Twitch's clear-room API.
    request = target(ModerationAction::Ban);
    const auto immutable = request; put(request.user_id, "999"); put(request.message_id, "new-row");
    CHECK(moderation_build_http(immutable, allowed, 2000, wire));
    CHECK(wire.method == ModerationHttpMethod::Post);
    CHECK(std::string(wire.url) == "https://api.twitch.tv/helix/moderation/bans?broadcaster_id=100&moderator_id=200");
    CHECK(std::string(wire.body) == "{\"data\":{\"user_id\":\"300\"}}");
    CHECK(std::string(wire.body).find("duration") == std::string::npos); // A ban is permanent, not duration zero.
    request = target(ModerationAction::Timeout); request.timeout_seconds = 600;
    CHECK(moderation_build_http(request, allowed, 2000, wire));
    CHECK(std::string(wire.body) == "{\"data\":{\"user_id\":\"300\",\"duration\":600}}");
    request = target(ModerationAction::Unban);
    CHECK(moderation_build_http(request, allowed, 2000, wire));
    CHECK(wire.method == ModerationHttpMethod::Delete);
    CHECK(std::string(wire.url) == "https://api.twitch.tv/helix/moderation/bans?broadcaster_id=100&moderator_id=200&user_id=300");
    CHECK(wire.body[0] == 0);
}
void outcomes_and_single_submission() {
    for (const auto action : {ModerationAction::DeleteMessage, ModerationAction::Timeout, ModerationAction::Ban, ModerationAction::Unban}) {
        const int expected = action == ModerationAction::DeleteMessage || action == ModerationAction::Unban ? 204 : 200;
        CHECK(moderation_classify(action, true, true, expected) == ModerationState::Succeeded);
        CHECK(moderation_classify(action, true, true, expected, false) == ModerationState::OutcomeUnknown);
        CHECK(moderation_classify(action, false, false, 0) == ModerationState::Failed);
        CHECK(moderation_classify(action, true, false, 0) == ModerationState::OutcomeUnknown);
        CHECK(moderation_classify(action, true, false, expected) == ModerationState::OutcomeUnknown);
        CHECK(moderation_classify(action, true, true, 401) == ModerationState::Denied);
        CHECK(moderation_classify(action, true, true, 403) == ModerationState::Denied);
        CHECK(moderation_classify(action, true, true, 429) == ModerationState::RateLimited);
        CHECK(moderation_classify(action, true, true, 400) == ModerationState::Failed);
        CHECK(moderation_classify(action, true, true, 404) == ModerationState::Failed);
        CHECK(moderation_classify(action, true, true, 409) == ModerationState::Failed);
        CHECK(moderation_classify(action, true, true, 500) == ModerationState::OutcomeUnknown);
        CHECK(moderation_classify(action, true, true, 202) == ModerationState::OutcomeUnknown);
    }
    ModerationLedger ledger;
    auto request = target(); const auto allowed = caps();
    CHECK(ledger.accept(request, allowed, 2000) == ModerationDenial::None);
    auto next = request; ++next.request_id;
    CHECK(ledger.accept(next, allowed, 2000) == ModerationDenial::Busy);
    ledger.finish(request.request_id, request.session_generation + 1);
    CHECK(ledger.accept(next, allowed, 2000) == ModerationDenial::Busy);
    ledger.finish(request.request_id, request.session_generation);
    // This remains consumed even if the HTTP result was timeout/unknown/401.
    CHECK(ledger.accept(request, allowed, 2000) == ModerationDenial::DuplicateRequest);
    next.confirmed = false;
    CHECK(ledger.accept(next, allowed, 2000) == ModerationDenial::ConfirmationRequired);
    next.confirmed = true;
    CHECK(ledger.accept(next, allowed, 2000) == ModerationDenial::None);
    ledger.finish(next.request_id, next.session_generation);
    CHECK(ledger.accept(next, allowed, 2000) == ModerationDenial::DuplicateRequest);
}
void read_only_profile() {
    auto broad_grants = caps();
    broad_grants.read_only = true;
    ModerationLedger ledger;
    for (const auto action : {ModerationAction::DeleteMessage, ModerationAction::Timeout, ModerationAction::Ban, ModerationAction::Unban}) {
        auto request = target(action);
        if (action == ModerationAction::Timeout) request.timeout_seconds = 60;
        CHECK(moderation_validate(request, broad_grants, 2000) == ModerationDenial::ReadOnlyProfile);
        ModerationHttpRequest wire;
        CHECK(!moderation_build_http(request, broad_grants, 2000, wire));
        CHECK(wire.url[0] == 0 && wire.body[0] == 0); // No mutation can reach HTTP.
        CHECK(ledger.accept(request, broad_grants, 2000) == ModerationDenial::ReadOnlyProfile);
    }
    // A later permission-enabled profile still requires a new selection under
    // its new session generation; old read-only targets cannot become actions.
    broad_grants.read_only = false; ++broad_grants.generation;
    CHECK(moderation_validate(target(), broad_grants, 2000) == ModerationDenial::StaleSession);
}
} // namespace
int main() {
    policy(); identity_and_wire_contract(); outcomes_and_single_submission(); read_only_profile();
    std::cout << "moderation contract: " << assertions << " assertions passed\n";
}
