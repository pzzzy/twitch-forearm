// Explicit display-only bring-up target (-DFOREARM_NETWORK=OFF).
#include "twitch_client.h"
#include <cstdio>
bool twitch_start() { return true; }
bool twitch_set_config(const char*,const char*,const char*,const char*) { return false; }
void twitch_request_login() {}
void twitch_logout() {}
bool twitch_next_event(TwitchEvent&,std::uint32_t) { return false; }
bool twitch_network_busy() { return false; }
TwitchStatus twitch_status() { TwitchStatus s{}; std::snprintf(s.state,sizeof(s.state),"Display qualification build"); return s; }
forearm::ModerationCapabilities twitch_moderation_capabilities() { return {}; }
forearm::ModerationResult twitch_moderate(const forearm::ModerationRequest& request) {
    forearm::ModerationResult result{}; result.request_id=request.request_id;
    result.session_generation=request.session_generation; result.action=request.action;
    result.state=forearm::ModerationState::Denied;
    std::snprintf(result.detail,sizeof(result.detail),"Networking disabled in this build"); return result;
}
void twitch_request_disconnect_for_test(std::uint32_t) {}
void twitch_request_wifi_disconnect_for_test(std::uint32_t) {}
bool twitch_request_refresh_for_test() { return false; }
void twitch_request_validation() {}
