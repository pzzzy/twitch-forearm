#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>

namespace forearm {
enum class ModerationAction : std::uint8_t { DeleteMessage, Timeout, Ban, Unban };
enum class ModerationState : std::uint8_t { Pending, Succeeded, Denied, RateLimited, Failed, OutcomeUnknown };
enum class ModerationDenial : std::uint8_t {
    None, ConfirmationRequired, InvalidIdentifier, NotAuthenticated, Offline,
    StaleSession, RoleMissing, ScopeMissing, WrongChannel, SharedSource,
    ProtectedTarget, InvalidDuration, MessageDeleted, MessageTooOld, Busy, DuplicateRequest
};
struct ModerationRequest {
    std::uint32_t request_id = 0;
    std::uint32_t session_generation = 0;
    ModerationAction action = ModerationAction::DeleteMessage;
    std::uint32_t timeout_seconds = 0;
    char channel_id[33]{};
    char message_id[65]{};
    char user_id[33]{};
    char source_channel_id[33]{};
    char source_message_id[65]{};
    bool protected_target = false;
    bool confirmed = false;
    bool message_deleted = false;
    std::uint64_t received_at_ms = 0;
};
struct ModerationCapabilities {
    bool authenticated = false;
    bool online = false;
    bool role_verified = false;
    bool can_delete = false;
    bool can_ban = false;
    std::uint32_t generation = 0;
    char broadcaster_id[33]{};
    char moderator_id[33]{};
    char reason[96]{};
};
struct ModerationResult {
    std::uint32_t request_id = 0;
    std::uint32_t session_generation = 0;
    ModerationAction action = ModerationAction::DeleteMessage;
    ModerationState state = ModerationState::Denied;
    ModerationDenial denial = ModerationDenial::None;
    int http_status = 0;
    char detail[128]{};
};

template <std::size_t N> inline std::string_view moderation_string(const char (&value)[N]) {
    std::size_t size = 0;
    while (size < N && value[size]) ++size;
    return size < N ? std::string_view(value, size) : std::string_view{};
}
inline bool moderation_numeric_id(std::string_view id) {
    if (id.empty() || id.size() > 32) return false;
    for (const char c : id) if (c < '0' || c > '9') return false;
    return true;
}
inline bool moderation_message_id(std::string_view id) {
    if (id.empty() || id.size() > 64) return false;
    for (const char c : id)
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
              (c >= 'a' && c <= 'z') || c == '-' || c == '_')) return false;
    return true;
}
inline const char* moderation_denial_text(ModerationDenial reason) {
    switch (reason) {
    case ModerationDenial::None: return "Ready";
    case ModerationDenial::ConfirmationRequired: return "A separate confirmation is required";
    case ModerationDenial::InvalidIdentifier: return "Missing or malformed target identity";
    case ModerationDenial::NotAuthenticated: return "Twitch login required";
    case ModerationDenial::Offline: return "Offline: no moderation was queued";
    case ModerationDenial::StaleSession: return "Account or channel changed; select again";
    case ModerationDenial::RoleMissing: return "Broadcaster/moderator role is not verified";
    case ModerationDenial::ScopeMissing: return "Reconnect Twitch to grant moderation permission";
    case ModerationDenial::WrongChannel: return "Selected message belongs to another channel";
    case ModerationDenial::SharedSource: return "Shared-source identity cannot be moderated here";
    case ModerationDenial::ProtectedTarget: return "Broadcaster, self, or moderator target is protected";
    case ModerationDenial::InvalidDuration: return "Timeout must be 30, 60, or 600 seconds";
    case ModerationDenial::MessageDeleted: return "The selected message has already been removed";
    case ModerationDenial::MessageTooOld: return "Selected message is older than six hours";
    case ModerationDenial::Busy: return "Another request is pending; nothing else was queued";
    case ModerationDenial::DuplicateRequest: return "This confirmation was already submitted";
    }
    return "Request denied";
}
inline ModerationDenial moderation_validate(const ModerationRequest& request,
                                           const ModerationCapabilities& capabilities,
                                           std::uint64_t now_ms) {
    if (!request.confirmed) return ModerationDenial::ConfirmationRequired;
    if (!request.request_id) return ModerationDenial::InvalidIdentifier;
    if (!capabilities.authenticated) return ModerationDenial::NotAuthenticated;
    if (!capabilities.online) return ModerationDenial::Offline;
    if (!request.session_generation || request.session_generation != capabilities.generation)
        return ModerationDenial::StaleSession;
    if (!capabilities.role_verified) return ModerationDenial::RoleMissing;
    const auto channel = moderation_string(request.channel_id);
    const auto author = moderation_string(request.user_id);
    const auto moderator = moderation_string(capabilities.moderator_id);
    if (!moderation_numeric_id(channel) || !moderation_numeric_id(author) || !moderation_numeric_id(moderator))
        return ModerationDenial::InvalidIdentifier;
    if (channel != moderation_string(capabilities.broadcaster_id)) return ModerationDenial::WrongChannel;
    const auto source = moderation_string(request.source_channel_id);
    if (!moderation_numeric_id(source) || source != channel) return ModerationDenial::SharedSource;
    if (request.protected_target || author == channel || author == moderator) return ModerationDenial::ProtectedTarget;
    // UI actions originate from a received chat line, including user actions.
    // Unban is also exposed for explicit cleanup by user ID, without a line.
    if (request.action != ModerationAction::Unban) {
        const auto message = moderation_string(request.message_id);
        if (!moderation_message_id(message)) return ModerationDenial::InvalidIdentifier;
        if (moderation_string(request.source_message_id) != message) return ModerationDenial::SharedSource;
    }
    switch (request.action) {
    case ModerationAction::DeleteMessage: {
        if (!capabilities.can_delete) return ModerationDenial::ScopeMissing;
        if (request.timeout_seconds) return ModerationDenial::InvalidDuration;
        if (request.message_deleted) return ModerationDenial::MessageDeleted;
        if (request.received_at_ms && now_ms >= request.received_at_ms &&
            now_ms - request.received_at_ms >= 6ULL * 3600000) return ModerationDenial::MessageTooOld;
        break;
    }
    case ModerationAction::Timeout:
        if (!capabilities.can_ban) return ModerationDenial::ScopeMissing;
        if (request.timeout_seconds != 30 && request.timeout_seconds != 60 && request.timeout_seconds != 600)
            return ModerationDenial::InvalidDuration;
        break;
    case ModerationAction::Ban:
    case ModerationAction::Unban:
        if (!capabilities.can_ban) return ModerationDenial::ScopeMissing;
        if (request.timeout_seconds) return ModerationDenial::InvalidDuration;
        break;
    default: return ModerationDenial::InvalidIdentifier;
    }
    return ModerationDenial::None;
}

enum class ModerationHttpMethod : std::uint8_t { Delete, Post };
struct ModerationHttpRequest {
    ModerationHttpMethod method = ModerationHttpMethod::Delete;
    char url[512]{};
    char body[192]{};
};
inline bool moderation_build_http(const ModerationRequest& target,
                                  const ModerationCapabilities& capabilities,
                                  std::uint64_t now_ms, ModerationHttpRequest& output) {
    output = ModerationHttpRequest{};
    if (moderation_validate(target, capabilities, now_ms) != ModerationDenial::None) return false;
    int length = 0;
    if (target.action == ModerationAction::DeleteMessage) {
        // This builder never has a clear-room variant. message_id is mandatory.
        length = std::snprintf(output.url, sizeof(output.url),
            "https://api.twitch.tv/helix/moderation/chat?broadcaster_id=%s&moderator_id=%s&message_id=%s",
            target.channel_id, capabilities.moderator_id, target.message_id);
    } else {
        length = std::snprintf(output.url, sizeof(output.url),
            "https://api.twitch.tv/helix/moderation/bans?broadcaster_id=%s&moderator_id=%s%s%s",
            target.channel_id, capabilities.moderator_id,
            target.action == ModerationAction::Unban ? "&user_id=" : "",
            target.action == ModerationAction::Unban ? target.user_id : "");
        if (target.action != ModerationAction::Unban) {
            output.method = ModerationHttpMethod::Post;
            const int body_length = target.action == ModerationAction::Timeout
                ? std::snprintf(output.body, sizeof(output.body), "{\"data\":{\"user_id\":\"%s\",\"duration\":%u}}", target.user_id, static_cast<unsigned>(target.timeout_seconds))
                : std::snprintf(output.body, sizeof(output.body), "{\"data\":{\"user_id\":\"%s\"}}", target.user_id);
            if (body_length < 0 || static_cast<std::size_t>(body_length) >= sizeof(output.body)) return false;
        }
    }
    return length > 0 && static_cast<std::size_t>(length) < sizeof(output.url);
}
inline ModerationState moderation_classify(ModerationAction action, bool attempted,
                                           bool transport_complete, int status,
                                           bool response_identity_matches = true) {
    if (!attempted) return ModerationState::Failed;
    if (!transport_complete) return ModerationState::OutcomeUnknown;
    const int expected = action == ModerationAction::DeleteMessage || action == ModerationAction::Unban ? 204 : 200;
    if (status == expected && response_identity_matches) return ModerationState::Succeeded;
    if (status == 401 || status == 403) return ModerationState::Denied;
    if (status == 429) return ModerationState::RateLimited;
    if (status >= 500 || (status >= 200 && status < 300)) return ModerationState::OutcomeUnknown;
    return ModerationState::Failed;
}

// The most recent 32 accepted confirmations stay consumed even after failure or
// unknown outcome. The controller submits once and never automatically retries.
// The caller must generate a new ID for a fresh, deliberate confirmation.
class ModerationLedger {
public:
    ModerationDenial accept(const ModerationRequest& request, const ModerationCapabilities& capabilities,
                            std::uint64_t now_ms) {
        const auto denial = moderation_validate(request, capabilities, now_ms);
        if (denial != ModerationDenial::None) return denial;
        if (pending_) return ModerationDenial::Busy;
        for (const auto& seen : seen_) if (seen.id == request.request_id && seen.generation == request.session_generation)
            return ModerationDenial::DuplicateRequest;
        seen_[cursor_++ % seen_.size()] = {request.request_id, request.session_generation};
        pending_ = true; pending_id_ = request.request_id; pending_generation_ = request.session_generation;
        return ModerationDenial::None;
    }
    void finish(std::uint32_t id, std::uint32_t generation) {
        if (pending_ && pending_id_ == id && pending_generation_ == generation) pending_ = false;
    }
private:
    struct Seen { std::uint32_t id = 0, generation = 0; };
    std::array<Seen, 32> seen_{};
    std::size_t cursor_ = 0;
    bool pending_ = false;
    std::uint32_t pending_id_ = 0, pending_generation_ = 0;
};
} // namespace forearm
