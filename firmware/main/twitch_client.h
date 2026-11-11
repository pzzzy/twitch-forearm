#pragma once

#include <cstdint>
#include "forearm/moderation.h"
#include "forearm/device_login.h"
#include "forearm/media.h"

// These messages contain displayable state only. Passwords and OAuth tokens are
// intentionally absent. The network task never calls LVGL.
enum class TwitchEventKind : std::uint8_t {
    Status, Login, Authenticated, Chat, DeleteMessage, ClearUser, ClearChannel, Gap, ModerationResult
};

using TwitchEmote = forearm::MediaOccurrence;

struct TwitchEvent {
    TwitchEventKind kind = TwitchEventKind::Status;
    char channel_id[33]{};
    char message_id[65]{};
    char user_id[33]{};
    char source_channel_id[33]{};
    char source_message_id[65]{};
    char display_name[101]{};
    char user_login[26]{};
    char text[2049]{};
    char verification_url[512]{};
    char user_code[32]{};
    TwitchEmote emotes[8]{};
    std::uint8_t emote_count = 0;
    bool connected = false;
    bool moderator = false;
    bool moderation_metadata_valid = false;
    forearm::ModerationResult moderation{};
};

struct TwitchStatus {
    bool worker_running = false;
    bool configured = false;
    bool wifi_connected = false;
    bool time_synchronized = false;
    bool authenticated = false;
    bool chat_connected = false;
    bool login_pending = false;
    bool read_only = false;
    std::uint32_t received_messages = 0;
    std::uint32_t dropped_events = 0;
    std::uint32_t reconnects = 0;
    std::uint32_t malformed_frames = 0;
    std::uint32_t worker_stack_free = 0;
    std::uint32_t websocket_stack_free = 0;
    std::uint32_t moderation_generation = 0;
    std::uint32_t moderation_requests = 0;
    std::uint32_t moderation_succeeded = 0;
    std::uint32_t moderation_unknown = 0;
    std::uint32_t refresh_attempts = 0, refresh_succeeded = 0, refresh_failed = 0;
    std::uint32_t login_poll_count = 0, login_seconds_remaining = 0;
    int last_login_http_status = 0;
    forearm::LoginPollResult last_login_poll_result = forearm::LoginPollResult::None;
    bool moderation_delete_granted = false;
    bool moderation_ban_granted = false;
    bool moderation_role_verified = false;
    char channel[33]{};
    char broadcaster_id[33]{}; // Exact resolved destination, never a token.
    char login[33]{};
    char state[96]{};
};

// Initialize the network worker once. Does not start a desktop bridge.
bool twitch_start();

// All four values replace the previous configuration. Empty Client ID is valid
// for Wi-Fi-only provisioning; empty channel defaults to the authorized user.
// Wi-Fi secrets are persisted in the local NVS configuration, never emitted.
bool twitch_set_config(const char* ssid, const char* password,
                       const char* public_client_id, const char* channel);
// Queue only a target/profile; saved Wi-Fi, Client ID and tokens remain owned
// by the worker. A read-only profile denies mutations despite broader grants.
bool twitch_watch_channel(const char* channel, bool read_only = true);
void twitch_request_login();
void twitch_logout();
bool twitch_next_event(TwitchEvent& event, std::uint32_t timeout_ms = 0);
TwitchStatus twitch_status();
// Low-priority asset downloads may yield to authorization and queued chat.
bool twitch_network_busy();
forearm::ModerationCapabilities twitch_moderation_capabilities();
// Copies one confirmed immutable request. Pending means accepted for immediate
// processing, not successful moderation. It is never queued while offline.
forearm::ModerationResult twitch_moderate(const forearm::ModerationRequest& request);
// Local recovery diagnostics, with no chat/moderation sends or credential output.
void twitch_request_disconnect_for_test(std::uint32_t duration_ms);
void twitch_request_wifi_disconnect_for_test(std::uint32_t duration_ms);
// Accept one explicit refresh behind the worker's current bounded HTTP call.
// true means queued, never successful; no diagnostic is replayed after failure.
bool twitch_request_refresh_for_test();
void twitch_request_validation();
