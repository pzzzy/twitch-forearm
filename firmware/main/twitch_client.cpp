// Standalone installed-client integration. OAuth and EventSub behavior follows
// https://dev.twitch.tv/docs/authentication/getting-tokens-oauth/#device-code-grant-flow
// https://dev.twitch.tv/docs/eventsub/handling-websocket-events/
#include "twitch_client.h"
#include "forearm/device_config.h"
#include "forearm/emote_fragments.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <utility>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace {
constexpr char kScope[] = "user:read:chat";
constexpr char kDeleteScope[] = "moderator:manage:chat_messages";
constexpr char kBanScope[] = "moderator:manage:banned_users";
constexpr char kRoleScope[] = "user:read:moderated_channels";
constexpr char kConsentScopes[] = "user:read:chat moderator:manage:chat_messages moderator:manage:banned_users user:read:moderated_channels";
constexpr char kWsUrl[] = "wss://eventsub.wss.twitch.tv/ws?keepalive_timeout_seconds=30";
constexpr std::size_t kMaxJson = 32768;
constexpr std::size_t kTokenBytes = 2049;

using Configuration = forearm::DeviceConfiguration;
enum class CommandKind { Configure, Login, Logout, Moderate, RefreshTest, WatchChannel };
struct Command {
    CommandKind kind{};
    Configuration config{};
    forearm::ModerationRequest moderation{};
    std::int64_t submitted_ms = 0;
};
struct TokenRecord {
    char access[kTokenBytes]{};
    char refresh[kTokenBytes]{};
    char client_id[65]{};
    std::int64_t expires_epoch = 0;
};
struct RawFrame { int slot = 0; unsigned generation = 0; char* json = nullptr; };
struct WsContext {
    esp_websocket_client_handle_t handle = nullptr;
    std::atomic<bool> disconnected{false};
    std::atomic<std::int64_t> last_received_ms{0};
    std::atomic<std::uint32_t> stack_free{0};
    char* buffer = nullptr; // Owned by this client's callback task until queued.
    std::size_t used = 0;
    bool reading_text = false;
    int slot = 0;
    unsigned generation = 0;
    bool welcomed = false;
    bool handoff = false;
    unsigned keepalive_seconds = 30;
    std::int64_t started_ms = 0;
};

QueueHandle_t commands = nullptr;
QueueHandle_t events = nullptr;
QueueHandle_t frames = nullptr;
QueueHandle_t moderation_results = nullptr;
// Queue control/locks stay in internal memory; large displayable event storage
// explicitly lives in PSRAM. FreeRTOS pvPortMalloc() uses internal memory even
// when ordinary large malloc() calls are configured to prefer external RAM.
StaticQueue_t event_queue_control;
std::uint8_t* event_queue_storage = nullptr;
constexpr UBaseType_t kEventQueueLength = 32;
portMUX_TYPE state_mux = portMUX_INITIALIZER_UNLOCKED;
TwitchStatus visible_state{};
forearm::ModerationCapabilities moderation_caps{};
forearm::ModerationLedger moderation_ledger;
Configuration configuration{};
TokenRecord tokens{};
std::atomic<bool> wifi_connected{false};
std::atomic<std::uint32_t> queue_drops{0};
std::atomic<std::uint32_t> malformed{0};
std::atomic<bool> http_active{false};
std::atomic<bool> refresh_test_pending{false};
std::atomic<bool> read_only_enforced{false};
std::atomic<bool> force_validation{false};
std::atomic<std::uint32_t> disconnect_test_ms{0};
std::atomic<std::uint32_t> wifi_disconnect_test_ms{0};
std::atomic<bool> wifi_test_blocked{false};
WsContext sockets[2];
int active_socket = -1;
int handoff_socket = -1;
bool wifi_started = false;
bool sntp_started = false;
bool login_requested = false;
bool logged_out = false;
bool account_validated = false;
bool grant_delete = false, grant_ban = false, grant_role = false;
char account_id[33]{};
char broadcaster_id[33]{};
char session_id[256]{};
char device_code[256]{};
char pending_user_code[32]{};
std::int64_t device_deadline_ms = 0;
std::int64_t next_poll_ms = 0;
unsigned poll_seconds = 5;
std::int64_t next_wifi_attempt_ms = 0;
std::int64_t next_network_attempt_ms = 0;
std::int64_t last_validation_ms = 0;
std::int64_t api_backoff_until_ms = 0;
std::int64_t disconnect_until_ms = 0;
std::int64_t wifi_disconnect_until_ms = 0;
unsigned reconnect_backoff_seconds = 1;
char dedupe_ids[128][65]{};
unsigned dedupe_cursor = 0;

std::int64_t now_ms() { return esp_timer_get_time() / 1000; }

template <std::size_t N> bool copy_exact(char (&out)[N], const char* source) {
    if (!source || std::strlen(source) >= N) { out[0] = 0; return false; }
    std::memcpy(out, source, std::strlen(source) + 1);
    return true;
}

// Twitch message text can be longer than the UI's stored excerpt. Never leave
// a partial UTF-8 codepoint in the bounded transfer event.
template <std::size_t N> void copy_text(char (&out)[N], const char* source) {
    if (!source) { out[0] = 0; return; }
    std::size_t count = std::min(std::strlen(source), N - 1);
    if (source[count]) {
        while (count && (static_cast<unsigned char>(source[count]) & 0xC0) == 0x80) --count;
    }
    std::memcpy(out, source, count); out[count] = 0;
}

const char* string_value(const cJSON* object, const char* key) {
    const auto* item = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(item) && item->valuestring ? item->valuestring : "";
}
int int_value(const cJSON* object, const char* key, int fallback = 0) {
    const auto* item = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsNumber(item) ? item->valueint : fallback;
}
bool array_contains(const cJSON* array, const char* value) {
    const cJSON* entry = nullptr;
    cJSON_ArrayForEach(entry, array) {
        if (cJSON_IsString(entry) && !std::strcmp(entry->valuestring, value)) return true;
    }
    return false;
}

void emit(TwitchEvent& event) {
    if (events && xQueueSend(events, &event, 0) != pdTRUE) ++queue_drops;
}
void state_message(const char* text) {
    bool changed = false;
    portENTER_CRITICAL(&state_mux);
    if (std::strcmp(visible_state.state, text)) {
        copy_text(visible_state.state, text);
        changed = true;
    }
    portEXIT_CRITICAL(&state_mux);
    if (changed) {
        TwitchEvent event;
        event.kind = TwitchEventKind::Status;
        copy_text(event.text, text);
        event.connected = wifi_connected.load();
        emit(event);
    }
}
void startup_failure(const char* stage, esp_err_t error) {
    char description[sizeof(visible_state.state)];
    std::snprintf(description, sizeof(description), "Network %s: %.48s", stage, esp_err_to_name(error));
    state_message(description);
    ESP_LOGE("twitch", "%s", description); // Fixed operation/error names only.
}
void set_auth_state(bool value) {
    portENTER_CRITICAL(&state_mux);
    visible_state.authenticated = value;
    portEXIT_CRITICAL(&state_mux);
}
void invalidate_moderation(const char* reason) {
    portENTER_CRITICAL(&state_mux);
    if (++moderation_caps.generation == 0) ++moderation_caps.generation;
    moderation_caps.authenticated = moderation_caps.online = moderation_caps.role_verified = false;
    moderation_caps.can_delete = moderation_caps.can_ban = false;
    moderation_caps.read_only = read_only_enforced.load();
    copy_text(moderation_caps.reason, reason);
    visible_state.moderation_generation = moderation_caps.generation;
    visible_state.moderation_role_verified = false;
    portEXIT_CRITICAL(&state_mux);
}
void set_chat_state(bool value) {
    portENTER_CRITICAL(&state_mux);
    visible_state.chat_connected = value;
    const bool online = value && wifi_connected.load() && moderation_caps.authenticated;
    if (!online && moderation_caps.online) {
        if (++moderation_caps.generation == 0) ++moderation_caps.generation;
        visible_state.moderation_generation = moderation_caps.generation;
    }
    moderation_caps.online = online;
    portEXIT_CRITICAL(&state_mux);
}
void gap(const char* reason) {
    TwitchEvent event;
    event.kind = TwitchEventKind::Gap;
    copy_text(event.text, reason);
    copy_exact(event.channel_id, broadcaster_id);
    emit(event);
}

std::string form_escape(const char* value) {
    constexpr char hex[] = "0123456789ABCDEF";
    std::string result;
    for (auto* p = reinterpret_cast<const unsigned char*>(value); *p; ++p) {
        if (std::isalnum(*p) || *p == '-' || *p == '.' || *p == '_' || *p == '~') result += static_cast<char>(*p);
        else { result += '%'; result += hex[*p >> 4]; result += hex[*p & 15]; }
    }
    return result;
}
bool login_name(const char* value, bool allow_empty) {
    if (!value || std::strlen(value) > 25) return false;
    if (!*value) return allow_empty;
    for (const auto* p = value; *p; ++p)
        if (!std::isalnum(static_cast<unsigned char>(*p)) && *p != '_') return false;
    return true;
}
bool client_name(const char* value) {
    if (!value || std::strlen(value) > 64) return false;
    for (const auto* p = value; *p; ++p)
        if (!std::isalnum(static_cast<unsigned char>(*p))) return false;
    return true;
}

struct HttpResult {
    bool attempted = false;
    bool ok = false;
    bool overflow = false;
    int status = 0;
    int retry_after = 0;
    std::int64_t rate_reset_epoch = 0;
    std::string body;
};
esp_err_t http_event(esp_http_client_event_t* event) {
    auto* result = static_cast<HttpResult*>(event->user_data);
    if (event->event_id == HTTP_EVENT_ON_DATA && event->data_len > 0) {
        if (result->overflow || result->body.size() + static_cast<std::size_t>(event->data_len) > kMaxJson) {
            result->overflow = true;
            return ESP_FAIL;
        }
        result->body.append(static_cast<const char*>(event->data), event->data_len);
    } else if (event->event_id == HTTP_EVENT_ON_HEADER && event->header_key && event->header_value &&
               !strcasecmp(event->header_key, "Retry-After")) {
        result->retry_after = std::clamp(std::atoi(event->header_value), 1, 3600);
    } else if (event->event_id == HTTP_EVENT_ON_HEADER && event->header_key && event->header_value &&
               !strcasecmp(event->header_key, "Ratelimit-Reset")) {
        result->rate_reset_epoch = std::strtoll(event->header_value, nullptr, 10);
    }
    return ESP_OK;
}
HttpResult request(const char* url, esp_http_client_method_t method, const std::string& body = {},
                   bool authenticated = false, bool validation = false) {
    HttpResult result;
    if (!wifi_connected.load()) return result;
    struct Activity {
        Activity() { http_active.store(true); }
        ~Activity() { http_active.store(false); }
    } activity;
    esp_http_client_config_t config{};
    config.url = url;
    config.method = method;
    config.timeout_ms = 7000;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.disable_auto_redirect = true;
    config.event_handler = http_event;
    config.user_data = &result;
    config.buffer_size = 2048;
    config.buffer_size_tx = 1024;
    config.user_agent = "ForearmChat/0.2";
    auto client = esp_http_client_init(&config);
    if (!client) return result;
    if (method == HTTP_METHOD_POST) {
        esp_http_client_set_header(client, "Content-Type", authenticated ? "application/json" : "application/x-www-form-urlencoded");
        esp_http_client_set_post_field(client, body.data(), body.size());
    }
    if (authenticated) {
        const std::string authorization = std::string(validation ? "OAuth " : "Bearer ") + tokens.access;
        esp_http_client_set_header(client, "Authorization", authorization.c_str());
        if (!validation) esp_http_client_set_header(client, "Client-Id", configuration.client_id);
    }
    result.attempted = true;
    result.ok = esp_http_client_perform(client) == ESP_OK && !result.overflow;
    result.status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (result.status == 429) {
        const auto reset_wait = std::clamp<std::int64_t>(result.rate_reset_epoch - std::time(nullptr), 0, 3600);
        const auto wait = std::max<std::int64_t>(std::max(result.retry_after, 30), reset_wait);
        api_backoff_until_ms = now_ms() + wait * 1000;
    }
    return result;
}

bool config_store(const Configuration& value) {
    nvs_handle_t handle;
    if (nvs_open("forearm", NVS_READWRITE, &handle) != ESP_OK) return false;
    bool okay = nvs_set_blob(handle, "config", &value, sizeof(value)) == ESP_OK && nvs_commit(handle) == ESP_OK;
    Configuration verified{}; std::size_t size = sizeof(verified);
    okay = okay && nvs_get_blob(handle, "config", &verified, &size) == ESP_OK && size == sizeof(verified) &&
           !std::memcmp(&value, &verified, sizeof(value));
    nvs_close(handle);
    return okay;
}
void config_load() {
    nvs_handle_t handle;
    if (nvs_open("forearm", NVS_READONLY, &handle) != ESP_OK) return;
    std::uint8_t stored[sizeof(Configuration)]{};
    std::size_t bytes = sizeof(stored);
    Configuration value{};
    if (nvs_get_blob(handle, "config", stored, &bytes) == ESP_OK && forearm::decode_device_configuration(stored, bytes, value))
        configuration = value;
    nvs_close(handle);
}
void tokens_forget() {
    invalidate_moderation("Twitch login required");
    tokens = TokenRecord{};
    account_validated = false;
    account_id[0] = broadcaster_id[0] = 0;
    set_auth_state(false);
    nvs_handle_t handle;
    if (nvs_open_from_partition("tokens", "twitch", NVS_READWRITE, &handle) == ESP_OK) {
        nvs_erase_key(handle, "pair");
        nvs_erase_key(handle, "pending");
        nvs_commit(handle);
        nvs_close(handle);
    }
}
bool tokens_save(const TokenRecord& value) {
    nvs_handle_t handle;
    if (nvs_open_from_partition("tokens", "twitch", NVS_READWRITE, &handle) != ESP_OK) return false;
    bool okay = nvs_set_blob(handle, "pair", &value, sizeof(value)) == ESP_OK && nvs_commit(handle) == ESP_OK;
    TokenRecord checked{};
    std::size_t bytes = sizeof(checked);
    okay = okay && nvs_get_blob(handle, "pair", &checked, &bytes) == ESP_OK && bytes == sizeof(checked) &&
           !std::memcmp(&value, &checked, sizeof(value));
    if (okay) {
        const auto erased = nvs_erase_key(handle, "pending");
        okay = (erased == ESP_OK || erased == ESP_ERR_NVS_NOT_FOUND) && nvs_commit(handle) == ESP_OK;
    }
    nvs_close(handle);
    return okay;
}
bool mark_refresh_pending() {
    nvs_handle_t handle;
    if (nvs_open_from_partition("tokens", "twitch", NVS_READWRITE, &handle) != ESP_OK) return false;
    const bool okay = nvs_set_u8(handle, "pending", 1) == ESP_OK && nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    return okay;
}
void tokens_load() {
    nvs_handle_t handle;
    if (nvs_open_from_partition("tokens", "twitch", NVS_READONLY, &handle) != ESP_OK) return;
    std::uint8_t pending = 0;
    const bool interrupted = nvs_get_u8(handle, "pending", &pending) == ESP_OK && pending;
    TokenRecord value{};
    std::size_t bytes = sizeof(value);
    const bool valid = nvs_get_blob(handle, "pair", &value, &bytes) == ESP_OK && bytes == sizeof(value) &&
        value.access[kTokenBytes-1] == 0 && value.refresh[kTokenBytes-1] == 0 && value.client_id[64] == 0;
    nvs_close(handle);
    if (valid && !interrupted && !std::strcmp(value.client_id, configuration.client_id)) tokens = value;
    else if (interrupted) state_message("Login needed after interrupted token refresh");
}
bool token_response(const HttpResult& response) {
    if (!response.ok || response.status != 200) return false;
    auto* json = cJSON_ParseWithLength(response.body.c_str(), response.body.size() + 1);
    if (!json) return false;
    TokenRecord value{};
    const int expiry = int_value(json, "expires_in");
    const bool scopes_ok = array_contains(cJSON_GetObjectItemCaseSensitive(json, "scope"), kScope);
    bool okay = copy_exact(value.access, string_value(json, "access_token")) && *value.access &&
        copy_exact(value.refresh, string_value(json, "refresh_token")) && *value.refresh &&
        copy_exact(value.client_id, configuration.client_id) && expiry > 0 && scopes_ok;
    value.expires_epoch = static_cast<std::int64_t>(std::time(nullptr)) + expiry;
    cJSON_Delete(json);
    // New pair is a single NVS blob; read it back before accepting the new token.
    if (okay) okay = tokens_save(value);
    if (okay) { tokens = value; account_validated = false; invalidate_moderation("Validating granted permissions"); }
    return okay;
}

void wifi_event(void*, esp_event_base_t base, std::int32_t id, void*) {
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP && !wifi_test_blocked.load()) wifi_connected.store(true);
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) wifi_connected.store(false);
}
bool wifi_setup() {
    if (!wifi_started) {
        if (esp_netif_init() != ESP_OK) return false;
        const auto event_result = esp_event_loop_create_default();
        if (event_result != ESP_OK && event_result != ESP_ERR_INVALID_STATE) return false;
        if (!esp_netif_create_default_wifi_sta()) return false;
        wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
        if (esp_wifi_init(&init) != ESP_OK || esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK ||
            esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) return false;
        if (esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, nullptr) != ESP_OK ||
            esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, nullptr) != ESP_OK) return false;
        wifi_started = true;
    } else {
        esp_wifi_disconnect();
        esp_wifi_stop();
    }
    wifi_connected.store(false);
    wifi_config_t station{};
    std::memcpy(station.sta.ssid, configuration.ssid, std::strlen(configuration.ssid));
    std::memcpy(station.sta.password, configuration.password, std::strlen(configuration.password));
    station.sta.pmf_cfg.capable = true;
    station.sta.pmf_cfg.required = false;
    station.sta.threshold.authmode = *configuration.password ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    if (esp_wifi_set_config(WIFI_IF_STA, &station) != ESP_OK || esp_wifi_start() != ESP_OK) return false;
    esp_wifi_set_ps(WIFI_PS_NONE); // Keep touch/chat timing deterministic during qualification.
    esp_wifi_connect();
    next_wifi_attempt_ms = now_ms() + 15000;
    return true;
}

void stop_socket(int slot) {
    if (slot < 0 || slot > 1) return;
    auto& context = sockets[slot];
    if (context.handle) {
        esp_websocket_client_stop(context.handle);
        esp_websocket_client_destroy(context.handle);
        context.handle = nullptr;
    }
    std::free(context.buffer);
    context.buffer = nullptr;
    context.used = 0;
    context.reading_text = false;
    context.welcomed = false;
    context.disconnected.store(false);
    ++context.generation;
}
void stop_sockets() {
    stop_socket(0); stop_socket(1);
    active_socket = handoff_socket = -1;
    session_id[0] = 0;
    set_chat_state(false);
}
void websocket_event(void* argument, esp_event_base_t, std::int32_t id, void* event_data) {
    auto& context = *static_cast<WsContext*>(argument);
    context.stack_free.store(uxTaskGetStackHighWaterMark(nullptr));
    auto* data = static_cast<esp_websocket_event_data_t*>(event_data);
    if (id == WEBSOCKET_EVENT_DISCONNECTED || id == WEBSOCKET_EVENT_CLOSED || id == WEBSOCKET_EVENT_ERROR) {
        context.disconnected.store(true); return;
    }
    if (id != WEBSOCKET_EVENT_DATA || !data) return;
    // Ping/pong is handled by esp_websocket_client. Twitch forbids sending
    // unsolicited application messages; this integration never sends any.
    if (data->op_code != 1 && data->op_code != 0) return;
    if (data->payload_offset == 0 && data->op_code == 1) {
        if (context.reading_text) { std::free(context.buffer); context.buffer = nullptr; ++malformed; }
        context.reading_text = true; context.used = 0;
        context.buffer = static_cast<char*>(heap_caps_malloc(kMaxJson + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    if (!context.reading_text || !context.buffer || data->data_len < 0 || data->payload_len < 0 ||
        data->payload_offset < 0 || data->payload_offset + data->data_len > data->payload_len ||
        context.used + static_cast<std::size_t>(data->data_len) > kMaxJson) {
        std::free(context.buffer); context.buffer = nullptr; context.used = 0; context.reading_text = false;
        ++malformed; return;
    }
    std::memcpy(context.buffer + context.used, data->data_ptr, data->data_len);
    context.used += data->data_len;
    if (data->fin && data->payload_offset + data->data_len == data->payload_len) {
        context.buffer[context.used] = 0;
        RawFrame frame{context.slot, context.generation, context.buffer};
        context.buffer = nullptr; context.used = 0; context.reading_text = false;
        context.last_received_ms.store(now_ms());
        if (xQueueSend(frames, &frame, 0) != pdTRUE) { std::free(frame.json); ++queue_drops; }
    }
}
bool allowed_reconnect(const char* url) {
    constexpr char host[] = "wss://eventsub.wss.twitch.tv";
    const auto count = sizeof(host) - 1;
    return std::strlen(url) < 1024 && !std::strncmp(url, host, count) &&
        (url[count] == '/' || url[count] == '?' || url[count] == 0);
}
bool start_socket(int slot, const char* url, bool handoff) {
    stop_socket(slot);
    auto& context = sockets[slot];
    context.slot = slot; context.handoff = handoff; context.started_ms = now_ms();
    context.last_received_ms.store(context.started_ms);
    esp_websocket_client_config_t config{};
    config.uri = url;
    config.disable_auto_reconnect = true;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.buffer_size = 2048;
    config.task_stack = 12288;
    config.task_prio = 4;
    config.network_timeout_ms = 7000;
    // v1.8.0 treats zero as its default ten-second client PING. Twitch permits
    // PONG only, so keep this past our bounded twelve-hour connection lifetime.
    // Server pings are still answered automatically by the transport.
    config.ping_interval_sec = 86400;
    config.pingpong_timeout_sec = 20;
    config.disable_pingpong_discon = true;
    config.keep_alive_enable = true;
    config.user_agent = "ForearmChat/0.2";
    context.handle = esp_websocket_client_init(&config);
    if (!context.handle) return false;
    if (esp_websocket_register_events(context.handle, WEBSOCKET_EVENT_ANY, websocket_event, &context) != ESP_OK ||
        esp_websocket_client_start(context.handle) != ESP_OK) { stop_socket(slot); return false; }
    return true;
}

void reconnect_later(const char* message) {
    const bool had_connection = active_socket >= 0 && sockets[active_socket].welcomed;
    stop_sockets();
    if (had_connection) gap("Connection gap — Twitch does not replay missed chat");
    state_message(message);
    next_network_attempt_ms = now_ms() + reconnect_backoff_seconds * 1000LL + esp_random() % 1000;
    reconnect_backoff_seconds = std::min(reconnect_backoff_seconds * 2, 60u);
    portENTER_CRITICAL(&state_mux); ++visible_state.reconnects; portEXIT_CRITICAL(&state_mux);
}
bool refresh_token() {
    portENTER_CRITICAL(&state_mux); ++visible_state.refresh_attempts; portEXIT_CRITICAL(&state_mux);
    if (!*tokens.refresh || !mark_refresh_pending()) {
        portENTER_CRITICAL(&state_mux); ++visible_state.refresh_failed; portEXIT_CRITICAL(&state_mux);
        state_message("Login needed: credential storage unavailable"); return false;
    }
    // Set persistent pending before the request because Public refresh tokens
    // are single-use. An ambiguous transport failure requires reauthorization.
    const auto response = request("https://id.twitch.tv/oauth2/token", HTTP_METHOD_POST,
        "client_id=" + form_escape(configuration.client_id) + "&grant_type=refresh_token&refresh_token=" + form_escape(tokens.refresh));
    if (!token_response(response)) {
        portENTER_CRITICAL(&state_mux); ++visible_state.refresh_failed; portEXIT_CRITICAL(&state_mux);
        tokens_forget();
        state_message("Login needed: refresh was denied or unconfirmed");
        return false;
    }
    portENTER_CRITICAL(&state_mux); ++visible_state.refresh_succeeded; portEXIT_CRITICAL(&state_mux);
    return true;
}
// This endpoint proves the authorized account is a moderator of the selected
// destination channel. Bounded pagination fails closed; a broadcaster proves
// its role directly with the /validate user ID.
// https://dev.twitch.tv/docs/api/reference/#get-moderated-channels
bool verify_moderator_role() {
    if (read_only_enforced.load()) return false;
    if (!std::strcmp(account_id, broadcaster_id)) return true;
    if (!grant_role) return false;
    std::string cursor;
    for (unsigned page = 0; page < 10; ++page) {
        const auto url = "https://api.twitch.tv/helix/moderation/channels?user_id=" +
            form_escape(account_id) + "&first=100" + (cursor.empty() ? "" : "&after=" + form_escape(cursor.c_str()));
        const auto response = request(url.c_str(), HTTP_METHOD_GET, {}, true);
        if (response.status == 401) {
            // This read-only preflight can discover revocation/expiry before
            // the mutation endpoint does. Revalidate/refresh on the ordinary
            // serialized worker path; the confirmed action is never replayed.
            account_validated = false; last_validation_ms = 0; next_network_attempt_ms = 0;
            invalidate_moderation("Revalidating Twitch permission");
        }
        if (!response.ok || response.status != 200) return false;
        auto* json = cJSON_ParseWithLength(response.body.c_str(), response.body.size() + 1);
        if (!json) return false;
        bool found = false;
        const cJSON* item = nullptr;
        cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(json, "data")) {
            if (!std::strcmp(string_value(item, "broadcaster_id"), broadcaster_id)) { found = true; break; }
        }
        const auto* pagination = cJSON_GetObjectItemCaseSensitive(json, "pagination");
        const std::string next = string_value(pagination, "cursor");
        cJSON_Delete(json);
        if (found) return true;
        if (next.empty() || next.size() > 512 || next == cursor) return false;
        cursor = next;
    }
    return false;
}
void publish_moderation_capabilities(bool role_verified) {
    forearm::ModerationCapabilities next{};
    next.read_only = read_only_enforced.load();
    next.authenticated = account_validated;
    next.role_verified = role_verified;
    next.can_delete = grant_delete && role_verified && !next.read_only;
    next.can_ban = grant_ban && role_verified && !next.read_only;
    copy_exact(next.broadcaster_id, broadcaster_id);
    copy_exact(next.moderator_id, account_id);
    copy_text(next.reason, next.read_only ? "Read-only channel profile: moderation is disabled" :
        !grant_delete || !grant_ban ? "Reconnect Twitch to grant moderation permission" :
        !role_verified ? "Broadcaster/moderator role is not verified" : "Moderation ready");
    portENTER_CRITICAL(&state_mux);
    const bool changed = moderation_caps.authenticated != next.authenticated ||
        moderation_caps.role_verified != next.role_verified || moderation_caps.can_delete != next.can_delete ||
        moderation_caps.can_ban != next.can_ban || std::strcmp(moderation_caps.broadcaster_id, next.broadcaster_id) ||
        std::strcmp(moderation_caps.moderator_id, next.moderator_id);
    next.generation = moderation_caps.generation;
    if ((changed || !next.generation) && ++next.generation == 0) ++next.generation;
    next.online = visible_state.chat_connected && wifi_connected.load();
    moderation_caps = next;
    visible_state.moderation_generation = next.generation;
    visible_state.moderation_delete_granted = grant_delete;
    visible_state.moderation_ban_granted = grant_ban;
    visible_state.moderation_role_verified = role_verified;
    portEXIT_CRITICAL(&state_mux);
}
enum class Validation { Valid, TemporaryFailure, NeedsLogin };
Validation validate_account(bool may_refresh = true) {
    auto response = request("https://id.twitch.tv/oauth2/validate", HTTP_METHOD_GET, {}, true, true);
    if (response.ok && response.status == 401 && may_refresh && refresh_token()) return validate_account(false);
    if (!response.ok || response.status == 429 || response.status >= 500) return Validation::TemporaryFailure;
    if (response.status != 200) return Validation::NeedsLogin;
    auto* json = cJSON_ParseWithLength(response.body.c_str(), response.body.size() + 1);
    if (!json) return Validation::TemporaryFailure;
    const auto* scopes = cJSON_GetObjectItemCaseSensitive(json, "scopes");
    const bool matches = !std::strcmp(string_value(json, "client_id"), configuration.client_id) &&
        array_contains(scopes, kScope) && copy_exact(account_id, string_value(json, "user_id")) &&
        forearm::moderation_numeric_id(account_id);
    grant_delete = array_contains(scopes, kDeleteScope);
    grant_ban = array_contains(scopes, kBanScope);
    grant_role = array_contains(scopes, kRoleScope);
    char login[33]{};
    const bool user_ok = copy_exact(login, string_value(json, "login")) && *login;
    const int expires = int_value(json, "expires_in");
    cJSON_Delete(json);
    if (!matches || !user_ok) return Validation::NeedsLogin;
    if (expires <= 90 && may_refresh) return refresh_token() ? validate_account(false) : Validation::NeedsLogin;
    const char* target = *configuration.channel ? configuration.channel : login;
    const auto channel_response = request(("https://api.twitch.tv/helix/users?login=" + form_escape(target)).c_str(), HTTP_METHOD_GET, {}, true);
    if (!channel_response.ok || channel_response.status == 429 || channel_response.status >= 500) return Validation::TemporaryFailure;
    if (channel_response.status == 401) return may_refresh && refresh_token() ? validate_account(false) : Validation::NeedsLogin;
    auto* channel_json = cJSON_ParseWithLength(channel_response.body.c_str(), channel_response.body.size() + 1);
    const auto* channel_data = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(channel_json, "data"), 0);
    const bool channel_ok = channel_response.status == 200 && channel_data &&
        copy_exact(broadcaster_id, string_value(channel_data, "id")) && forearm::moderation_numeric_id(broadcaster_id);
    cJSON_Delete(channel_json);
    if (!channel_ok) { state_message("Channel not found; update channel configuration"); return Validation::TemporaryFailure; }
    account_validated = true; last_validation_ms = now_ms();
    publish_moderation_capabilities(verify_moderator_role());
    portENTER_CRITICAL(&state_mux);
    visible_state.authenticated = true;
    copy_exact(visible_state.login, login);
    copy_exact(visible_state.channel, target);
    portEXIT_CRITICAL(&state_mux);
    TwitchEvent event;
    event.kind = TwitchEventKind::Authenticated;
    copy_exact(event.user_id, account_id); copy_exact(event.channel_id, broadcaster_id);
    copy_exact(event.display_name, login); copy_text(event.text, target); event.connected = true;
    emit(event);
    return Validation::Valid;
}

void login_begin() {
    if (!*configuration.client_id) { state_message("Twitch Client ID needed in setup"); login_requested = false; return; }
    auto response = request("https://id.twitch.tv/oauth2/device", HTTP_METHOD_POST,
        "client_id=" + form_escape(configuration.client_id) + "&scopes=" + form_escape(read_only_enforced.load() ? kScope : kConsentScopes));
    if (!response.ok || response.status == 429 || response.status >= 500) {
        state_message("Login service unavailable; retrying"); next_network_attempt_ms = now_ms() + 15000; return;
    }
    auto* json = cJSON_ParseWithLength(response.body.c_str(), response.body.size() + 1);
    const int expiry = int_value(json, "expires_in");
    poll_seconds = std::clamp(int_value(json, "interval", 5), 1, 60);
    TwitchEvent event;
    event.kind = TwitchEventKind::Login;
    const bool url_valid = forearm::normalize_public_device_url(string_value(json, "verification_uri_complete"), event.verification_url, sizeof(event.verification_url)) ||
        forearm::normalize_public_device_url(string_value(json, "verification_uri"), event.verification_url, sizeof(event.verification_url));
    bool valid = response.status == 200 && copy_exact(device_code, string_value(json, "device_code")) && *device_code &&
        copy_exact(event.user_code, string_value(json, "user_code")) && *event.user_code && url_valid && expiry > 0 && expiry <= 86400;
    cJSON_Delete(json);
    if (!valid) { device_code[0] = 0; login_requested = false; state_message("Twitch login rejected; check Public Client ID"); return; }
    copy_exact(pending_user_code, event.user_code);
    device_deadline_ms = now_ms() + expiry * 1000LL;
    next_poll_ms = now_ms() + poll_seconds * 1000LL;
    portENTER_CRITICAL(&state_mux);
    visible_state.login_pending = true;
    visible_state.login_seconds_remaining = expiry;
    visible_state.login_poll_count = 0; visible_state.last_login_http_status = 0;
    visible_state.last_login_poll_result = forearm::LoginPollResult::None;
    portEXIT_CRITICAL(&state_mux);
    emit(event); state_message("Authorize Twitch in your phone browser");
    login_requested = false;
}
void login_cancel(const char* reason) {
    device_code[0] = pending_user_code[0] = 0;
    device_deadline_ms = 0;
    portENTER_CRITICAL(&state_mux); visible_state.login_pending = false; visible_state.login_seconds_remaining = 0; portEXIT_CRITICAL(&state_mux);
    state_message(reason);
}
void login_poll() {
    if (now_ms() >= device_deadline_ms) { login_cancel("Login code expired; tap Connect again"); return; }
    if (now_ms() < next_poll_ms || now_ms() < api_backoff_until_ms) return;
    auto response = request("https://id.twitch.tv/oauth2/token", HTTP_METHOD_POST,
        "client_id=" + form_escape(configuration.client_id) + "&scopes=" + form_escape(read_only_enforced.load() ? kScope : kConsentScopes) +
        "&device_code=" + form_escape(device_code) + "&grant_type=" + form_escape("urn:ietf:params:oauth:grant-type:device_code"));
    portENTER_CRITICAL(&state_mux);
    ++visible_state.login_poll_count; visible_state.last_login_http_status = response.status;
    visible_state.last_login_poll_result = !response.ok || response.status >= 500 ? forearm::LoginPollResult::TransportError :
        response.status == 429 ? forearm::LoginPollResult::RateLimited : forearm::LoginPollResult::Denied;
    portEXIT_CRITICAL(&state_mux);
    next_poll_ms = now_ms() + poll_seconds * 1000LL;
    if (response.ok && response.status == 200) {
        if (token_response(response)) {
            portENTER_CRITICAL(&state_mux); visible_state.last_login_poll_result = forearm::LoginPollResult::Success; portEXIT_CRITICAL(&state_mux);
            login_cancel("Twitch authorized; opening chat"); logged_out = false; next_network_attempt_ms = 0;
        }
        else login_cancel("Login received but credentials could not be saved");
        return;
    }
    if (!response.ok || response.status >= 500 || response.status == 429) {
        next_poll_ms = std::max(next_poll_ms, now_ms() + 10000); return;
    }
    auto* json = cJSON_ParseWithLength(response.body.c_str(), response.body.size() + 1);
    const char* error = string_value(json, "message");
    if (!*error) error = string_value(json, "error");
    if (!std::strcmp(error, "authorization_pending")) {
        portENTER_CRITICAL(&state_mux); visible_state.last_login_poll_result = forearm::LoginPollResult::Pending; portEXIT_CRITICAL(&state_mux);
        cJSON_Delete(json); return;
    }
    if (!std::strcmp(error, "slow_down")) {
        portENTER_CRITICAL(&state_mux); visible_state.last_login_poll_result = forearm::LoginPollResult::SlowDown; portEXIT_CRITICAL(&state_mux);
        poll_seconds = std::min(poll_seconds + 5, 120u);
        next_poll_ms = now_ms() + poll_seconds * 1000LL;
        cJSON_Delete(json); return;
    }
    cJSON_Delete(json);
    login_cancel("Login denied or code invalid; tap Connect again");
}

bool subscribe(const char* type) {
    auto* json = cJSON_CreateObject();
    if (!json) return false;
    cJSON_AddStringToObject(json, "type", type);
    cJSON_AddStringToObject(json, "version", "1");
    auto* condition = cJSON_AddObjectToObject(json, "condition");
    cJSON_AddStringToObject(condition, "broadcaster_user_id", broadcaster_id);
    cJSON_AddStringToObject(condition, "user_id", account_id);
    auto* transport = cJSON_AddObjectToObject(json, "transport");
    cJSON_AddStringToObject(transport, "method", "websocket");
    cJSON_AddStringToObject(transport, "session_id", session_id);
    char* body = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (!body) return false;
    auto response = request("https://api.twitch.tv/helix/eventsub/subscriptions", HTTP_METHOD_POST, body, true);
    cJSON_free(body);
    if (response.status == 401) { account_validated = false; last_validation_ms = 0; }
    // A conflict may belong to a still-closing older socket. Do not claim this
    // new transport is receiving chat without Twitch accepting its session ID.
    return response.ok && response.status == 202;
}
bool duplicate_event(const char* id) {
    if (!*id || std::strlen(id) >= sizeof(dedupe_ids[0])) return true;
    for (const auto& previous : dedupe_ids) if (!std::strcmp(previous, id)) return true;
    copy_exact(dedupe_ids[dedupe_cursor++ % 128], id);
    return false;
}
forearm::ModerationResult result_for(const forearm::ModerationRequest& target,
                                    forearm::ModerationState state,
                                    forearm::ModerationDenial denial = forearm::ModerationDenial::None) {
    forearm::ModerationResult result{};
    result.request_id = target.request_id; result.session_generation = target.session_generation;
    result.action = target.action; result.state = state; result.denial = denial;
    if (denial != forearm::ModerationDenial::None) copy_text(result.detail, forearm::moderation_denial_text(denial));
    return result;
}
void finish_moderation(const forearm::ModerationResult& result) {
    // Keep the ledger pending until this mailbox is consumed. Admission and
    // outcome delivery stay serialized even for callers outside the UI.
    if (moderation_results) xQueueOverwrite(moderation_results, &result);
    portENTER_CRITICAL(&state_mux);
    if (result.state == forearm::ModerationState::Succeeded) ++visible_state.moderation_succeeded;
    if (result.state == forearm::ModerationState::OutcomeUnknown) ++visible_state.moderation_unknown;
    portEXIT_CRITICAL(&state_mux);
    // Terminal action outcomes have a separate mailbox, so a chat burst cannot
    // lose the only result of a confirmed mutation. Only one request is pending.
}
void execute_moderation(const Command& command) {
    const auto& target = command.moderation; // Immutable copy, never a UI row pointer.
    if (read_only_enforced.load()) {
        finish_moderation(result_for(target, forearm::ModerationState::Denied, forearm::ModerationDenial::ReadOnlyProfile)); return;
    }
    auto capabilities = twitch_moderation_capabilities();
    auto denial = forearm::moderation_validate(target, capabilities, now_ms());
    if (denial == forearm::ModerationDenial::None &&
        (now_ms() - command.submitted_ms > 1500 || http_active.load() ||
         active_socket < 0 || sockets[active_socket].disconnected.load() ||
         now_ms() < disconnect_until_ms)) denial = forearm::ModerationDenial::Offline;
    if (denial != forearm::ModerationDenial::None) {
        finish_moderation(result_for(target, forearm::ModerationState::Denied, denial)); return;
    }
    if (now_ms() < api_backoff_until_ms) {
        auto result = result_for(target, forearm::ModerationState::RateLimited);
        copy_text(result.detail, "Twitch rate limit; no action was sent"); finish_moderation(result); return;
    }
    // Other-channel roles are checked again immediately before each mutation.
    if (!verify_moderator_role()) {
        publish_moderation_capabilities(false);
        finish_moderation(result_for(target, forearm::ModerationState::Denied, forearm::ModerationDenial::RoleMissing)); return;
    }
    // Session/config can change while the read-only role lookup is in flight.
    capabilities = twitch_moderation_capabilities();
    if (read_only_enforced.load()) capabilities.read_only = true;
    forearm::ModerationHttpRequest http{};
    if (!forearm::moderation_build_http(target, capabilities, now_ms(), http)) {
        finish_moderation(result_for(target, forearm::ModerationState::Denied,
            forearm::moderation_validate(target, capabilities, now_ms()))); return;
    }
    // Exactly one mutation attempt. In particular a 401, timeout, broken TLS
    // response, 5xx, or unknown result never replays this confirmed request.
    // https://dev.twitch.tv/docs/api/reference/#delete-chat-messages
    // https://dev.twitch.tv/docs/api/reference/#ban-user
    const auto response = request(http.url, http.method == forearm::ModerationHttpMethod::Post ? HTTP_METHOD_POST : HTTP_METHOD_DELETE,
                                  http.body, true);
    bool response_identity = true;
    if (response.ok && response.status == 200 && http.method == forearm::ModerationHttpMethod::Post) {
        auto* json = cJSON_ParseWithLength(response.body.c_str(), response.body.size() + 1);
        const auto* data = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(json, "data"), 0);
        response_identity = data && !std::strcmp(string_value(data, "broadcaster_id"), target.channel_id) &&
            !std::strcmp(string_value(data, "moderator_id"), capabilities.moderator_id) &&
            !std::strcmp(string_value(data, "user_id"), target.user_id);
        cJSON_Delete(json);
    }
    auto result = result_for(target, forearm::moderation_classify(target.action, response.attempted,
                                                               response.ok, response.status, response_identity));
    result.http_status = response.status;
    switch (result.state) {
    case forearm::ModerationState::Succeeded: copy_text(result.detail, "Twitch confirmed the selected action"); break;
    case forearm::ModerationState::Denied: copy_text(result.detail, "Twitch denied permission; no automatic retry"); break;
    case forearm::ModerationState::RateLimited: copy_text(result.detail, "Twitch rate limit; action was not retried"); break;
    case forearm::ModerationState::OutcomeUnknown: copy_text(result.detail, "Outcome unknown. Check Twitch before any new action"); break;
    default: copy_text(result.detail, "Action was not confirmed by Twitch; no automatic retry"); break;
    }
    if (response.status == 401) {
        account_validated = false; last_validation_ms = 0; next_network_attempt_ms = 0;
        invalidate_moderation("Revalidating Twitch permission");
    }
    if (response.status == 403) publish_moderation_capabilities(false);
    finish_moderation(result);
}
void process_frame(RawFrame& frame) {
    if (frame.slot < 0 || frame.slot > 1 || sockets[frame.slot].generation != frame.generation) return;
    auto& context = sockets[frame.slot];
    auto* json = cJSON_Parse(frame.json);
    if (!json) { ++malformed; return; }
    const auto* metadata = cJSON_GetObjectItemCaseSensitive(json, "metadata");
    const auto* payload = cJSON_GetObjectItemCaseSensitive(json, "payload");
    const char* kind = string_value(metadata, "message_type");
    if (!std::strcmp(kind, "session_welcome")) {
        if (context.welcomed) { cJSON_Delete(json); return; }
        const auto* session = cJSON_GetObjectItemCaseSensitive(payload, "session");
        if (!copy_exact(session_id, string_value(session, "id")) || !*session_id) { cJSON_Delete(json); return; }
        context.keepalive_seconds = std::clamp(int_value(session, "keepalive_timeout_seconds", 30), 10, 600);
        if (context.handoff) {
            const int old = active_socket;
            active_socket = frame.slot; handoff_socket = -1;
            context.welcomed = true; context.handoff = false;
            if (old >= 0 && old != active_socket) stop_socket(old);
            set_chat_state(true); state_message("Live Twitch chat");
        } else {
            // First subscription is installed promptly within Twitch's welcome
            // deadline. The remaining clear/delete subscriptions follow it.
            if (subscribe("channel.chat.message")) {
                context.welcomed = true; reconnect_backoff_seconds = 1;
                set_chat_state(true); state_message("Live Twitch chat");
                const bool maintenance = subscribe("channel.chat.message_delete") &&
                    subscribe("channel.chat.clear_user_messages") && subscribe("channel.chat.clear");
                if (!maintenance) reconnect_later("Chat maintenance subscription failed; reconnecting");
            } else reconnect_later("Twitch subscription rejected; reconnecting");
        }
    } else if (!std::strcmp(kind, "session_reconnect")) {
        const auto* session = cJSON_GetObjectItemCaseSensitive(payload, "session");
        const char* url = string_value(session, "reconnect_url");
        if (allowed_reconnect(url) && handoff_socket < 0) {
            const int slot = frame.slot == 0 ? 1 : 0;
            if (start_socket(slot, url, true)) handoff_socket = slot;
        }
    } else if (!std::strcmp(kind, "revocation")) {
        const auto* subscription = cJSON_GetObjectItemCaseSensitive(payload, "subscription");
        const bool revoked = !std::strcmp(string_value(subscription, "status"), "authorization_revoked");
        stop_sockets();
        if (revoked) { tokens_forget(); logged_out = true; state_message("Twitch authorization revoked; login again"); }
        else { state_message("Twitch subscription revoked; reconnecting"); next_network_attempt_ms = now_ms() + 15000; }
        gap("Chat subscription ended — messages may be missing");
    } else if (!std::strcmp(kind, "notification") && !duplicate_event(string_value(metadata, "message_id"))) {
        const auto* notification = cJSON_GetObjectItemCaseSensitive(payload, "event");
        const char* type = string_value(metadata, "subscription_type");
        TwitchEvent event;
        bool recognized = true;
        const bool ids_ok = copy_exact(event.channel_id, string_value(notification, "broadcaster_user_id"));
        if (!std::strcmp(type, "channel.chat.message")) {
            event.kind = TwitchEventKind::Chat;
            const auto* message = cJSON_GetObjectItemCaseSensitive(notification, "message");
            recognized = copy_exact(event.message_id, string_value(notification, "message_id")) && *event.message_id &&
                copy_exact(event.user_id, string_value(notification, "chatter_user_id")) && *event.user_id;
            if (!copy_exact(event.user_login, string_value(notification, "chatter_user_login")) ||
                !login_name(event.user_login, false)) event.user_login[0] = 0;
            // Never truncate a localized sender into a different-looking
            // identity. Preserve the full supported name or exact login.
            if (!copy_exact(event.display_name, string_value(notification, "chatter_user_name")) || !*event.display_name)
                copy_exact(event.display_name, event.user_login);
            copy_text(event.text, string_value(message, "text"));
            forearm::MessageFragmentCursor fragment_cursor(string_value(message, "text"), std::strlen(event.text));
            const auto* fragments = cJSON_GetObjectItemCaseSensitive(message, "fragments");
            const cJSON* fragment = nullptr;
            cJSON_ArrayForEach(fragment, fragments) {
                const auto range = fragment_cursor.consume(string_value(fragment, "text"));
                if (event.emote_count == 8) continue;
                if (std::strcmp(string_value(fragment, "type"), "emote")) continue;
                const auto* emote = cJSON_GetObjectItemCaseSensitive(fragment, "emote");
                auto& output = event.emotes[event.emote_count];
                if (copy_exact(output.id, string_value(emote, "id")) && *output.id &&
                    copy_exact(output.text, string_value(fragment, "text")) && *output.text) {
                    output.byte_offset = range.byte_offset; output.byte_length = range.byte_length;
                    output.position_valid = range.position_valid;
                    ++event.emote_count;
                }
            }
            if (!cJSON_IsArray(fragments) || !fragment_cursor.complete())
                for (unsigned i = 0; i < event.emote_count; ++i) event.emotes[i].position_valid = false;
            const auto* badges = cJSON_GetObjectItemCaseSensitive(notification, "badges");
            const cJSON* badge = nullptr;
            cJSON_ArrayForEach(badge, badges) {
                const char* set = string_value(badge, "set_id");
                if (!std::strcmp(set, "moderator") || !std::strcmp(set, "broadcaster")) event.moderator = true;
            }
            // Preserve exact Shared Chat origin identities; destination-only
            // IDs must never be used to moderate a different source channel.
            const auto* source_channel = cJSON_GetObjectItemCaseSensitive(notification, "source_broadcaster_user_id");
            const auto* source_message = cJSON_GetObjectItemCaseSensitive(notification, "source_message_id");
            const bool ordinary_source = (!source_channel || cJSON_IsNull(source_channel)) &&
                                         (!source_message || cJSON_IsNull(source_message));
            if (ordinary_source) {
                copy_exact(event.source_channel_id, event.channel_id);
                copy_exact(event.source_message_id, event.message_id);
                event.moderation_metadata_valid = true;
            } else if (cJSON_IsString(source_channel) && cJSON_IsString(source_message)) {
                event.moderation_metadata_valid = copy_exact(event.source_channel_id, source_channel->valuestring) &&
                    copy_exact(event.source_message_id, source_message->valuestring) &&
                    forearm::moderation_numeric_id(event.source_channel_id) &&
                    forearm::moderation_message_id(event.source_message_id);
            }
            event.moderation_metadata_valid = event.moderation_metadata_valid && ids_ok &&
                forearm::moderation_numeric_id(event.channel_id) && forearm::moderation_numeric_id(event.user_id) &&
                forearm::moderation_message_id(event.message_id);
            portENTER_CRITICAL(&state_mux); ++visible_state.received_messages; portEXIT_CRITICAL(&state_mux);
        } else if (!std::strcmp(type, "channel.chat.message_delete")) {
            event.kind = TwitchEventKind::DeleteMessage;
            recognized = copy_exact(event.message_id, string_value(notification, "message_id")) && *event.message_id;
            copy_exact(event.user_id, string_value(notification, "target_user_id"));
        } else if (!std::strcmp(type, "channel.chat.clear_user_messages")) {
            event.kind = TwitchEventKind::ClearUser;
            recognized = copy_exact(event.user_id, string_value(notification, "target_user_id")) && *event.user_id;
        } else if (!std::strcmp(type, "channel.chat.clear")) event.kind = TwitchEventKind::ClearChannel;
        else recognized = false;
        if (recognized && ids_ok && *event.channel_id) emit(event); else ++malformed;
    }
    cJSON_Delete(json);
}

void configure(const Configuration& value) {
    Configuration next = value;
    next.read_only = configuration.read_only; // Wi-Fi setup does not silently change profile permission.
    if (!config_store(next)) { state_message("Setup could not be saved to device NVS"); return; }
    const bool changed_client = std::strcmp(configuration.client_id, next.client_id);
    stop_sockets(); login_cancel("Applying device setup");
    configuration = next;
    wifi_test_blocked.store(false); wifi_disconnect_until_ms = 0;
    logged_out = false; account_validated = false; login_requested = false;
    if (changed_client) tokens_forget();
    portENTER_CRITICAL(&state_mux);
    visible_state.configured = *configuration.ssid && *configuration.client_id;
    copy_exact(visible_state.channel, configuration.channel);
    visible_state.login[0] = 0;
    visible_state.authenticated = false;
    portEXIT_CRITICAL(&state_mux);
    if (wifi_setup()) state_message("Connecting to Wi-Fi");
    else state_message("Wi-Fi initialization failed");
    next_network_attempt_ms = 0;
}

void watch_channel(const Configuration& target) {
    Configuration next = configuration; // Only the worker copies saved secrets.
    copy_exact(next.channel, target.channel); next.read_only = target.read_only;
    if (!config_store(next)) {
        if (target.read_only) read_only_enforced.store(true); // Fail closed even if persistence failed.
        portENTER_CRITICAL(&state_mux); visible_state.read_only = read_only_enforced.load(); portEXIT_CRITICAL(&state_mux);
        force_validation.store(true);
        state_message("Channel profile could not be saved; previous channel retained"); return;
    }
    stop_sockets(); login_cancel("Switching channel profile");
    configuration = next;
    read_only_enforced.store(configuration.read_only != 0);
    account_validated = false; last_validation_ms = 0; login_requested = false;
    next_network_attempt_ms = 0;
    portENTER_CRITICAL(&state_mux);
    visible_state.read_only = configuration.read_only != 0;
    copy_exact(visible_state.channel, configuration.channel);
    portEXIT_CRITICAL(&state_mux);
}

void worker(void*) {
    portENTER_CRITICAL(&state_mux); visible_state.worker_running = true; portEXIT_CRITICAL(&state_mux);
    config_load(); tokens_load();
    read_only_enforced.store(configuration.read_only != 0);
    portENTER_CRITICAL(&state_mux);
    visible_state.configured = *configuration.ssid && *configuration.client_id;
    visible_state.read_only = configuration.read_only != 0;
    copy_exact(visible_state.channel, configuration.channel);
    portEXIT_CRITICAL(&state_mux);
    if (*configuration.ssid) {
        if (wifi_setup()) state_message("Connecting to Wi-Fi"); else state_message("Wi-Fi initialization failed");
    } else state_message("Set up Wi-Fi and Twitch Client ID over USB");
    std::uint32_t observed_drops = 0;
    while (true) {
        if (wifi_test_blocked.load() && now_ms() >= wifi_disconnect_until_ms) wifi_test_blocked.store(false);
        if (force_validation.exchange(false)) {
            account_validated = false; last_validation_ms = 0; next_network_attempt_ms = 0;
        }
        if (const auto duration = disconnect_test_ms.exchange(0)) {
            const bool had_connection = active_socket >= 0;
            stop_sockets();
            disconnect_until_ms = now_ms() + duration;
            next_network_attempt_ms = disconnect_until_ms;
            if (had_connection) gap("Diagnostic connection gap — Twitch does not replay missed chat");
            state_message("Testing network recovery; retained chat remains available");
        }
        if (const auto duration = wifi_disconnect_test_ms.exchange(0)) {
            const bool had_connection = active_socket >= 0;
            stop_sockets();
            wifi_disconnect_until_ms = now_ms() + duration;
            wifi_test_blocked.store(true);
            next_wifi_attempt_ms = wifi_disconnect_until_ms;
            next_network_attempt_ms = wifi_disconnect_until_ms;
            if (wifi_started) esp_wifi_disconnect();
            wifi_connected.store(false);
            if (had_connection) gap("Diagnostic Wi-Fi gap — Twitch does not replay missed chat");
            state_message("Testing Wi-Fi recovery; retained chat remains available");
        }
        Command command;
        while (xQueueReceive(commands, &command, 0) == pdTRUE) {
            if (command.kind == CommandKind::Configure) configure(command.config);
            else if (command.kind == CommandKind::WatchChannel) watch_channel(command.config);
            else if (command.kind == CommandKind::Login) {
                stop_sockets(); login_cancel("Preparing Twitch login");
                account_validated = false; last_validation_ms = 0;
                login_requested = true; logged_out = false; next_network_attempt_ms = 0;
            } else if (command.kind == CommandKind::Moderate) execute_moderation(command);
            else if (command.kind == CommandKind::RefreshTest) {
                invalidate_moderation("Refreshing saved Twitch authorization");
                account_validated = false; last_validation_ms = 0;
                if (wifi_connected.load() && now_ms() >= api_backoff_until_ms && *tokens.refresh) {
                    refresh_token(); next_network_attempt_ms = 0;
                } else state_message("Refresh diagnostic was not attempted while offline or rate limited");
                refresh_test_pending.store(false);
            }
            else {
                stop_sockets(); tokens_forget(); login_cancel("Signed out of Twitch");
                logged_out = true; login_requested = false;
            }
            command = Command{};
        }
        RawFrame frame;
        while (xQueueReceive(frames, &frame, 0) == pdTRUE) {
            process_frame(frame); std::free(frame.json);
        }
        const auto dropped = queue_drops.load();
        if (dropped != observed_drops) { observed_drops = dropped; gap("Chat buffer overflow — some messages were dropped"); }
        const auto stack_free = uxTaskGetStackHighWaterMark(nullptr);
        portENTER_CRITICAL(&state_mux);
        visible_state.wifi_connected = wifi_connected.load();
        visible_state.login_seconds_remaining = device_deadline_ms > now_ms() ? static_cast<std::uint32_t>((device_deadline_ms - now_ms() + 999) / 1000) : 0;
        visible_state.dropped_events = dropped;
        visible_state.malformed_frames = malformed.load();
        visible_state.worker_stack_free = stack_free;
        visible_state.websocket_stack_free = active_socket >= 0 ? sockets[active_socket].stack_free.load() : 0;
        portEXIT_CRITICAL(&state_mux);
        if (!wifi_connected.load()) {
            if (active_socket >= 0 || handoff_socket >= 0) reconnect_later("Wi-Fi disconnected; retained chat remains available");
            if (*configuration.ssid && wifi_started && now_ms() >= next_wifi_attempt_ms && now_ms() >= wifi_disconnect_until_ms) {
                esp_wifi_connect(); next_wifi_attempt_ms = now_ms() + 15000 + esp_random() % 5000;
                state_message("Reconnecting to Wi-Fi");
            }
            vTaskDelay(pdMS_TO_TICKS(50)); continue;
        }
        if (!sntp_started) {
            if (!esp_sntp_enabled()) {
                esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
                esp_sntp_setservername(0, "time.cloudflare.com");
                esp_sntp_setservername(1, "pool.ntp.org");
                esp_sntp_init();
            }
            sntp_started = true;
        }
        const bool time_ok = std::time(nullptr) > 1700000000;
        portENTER_CRITICAL(&state_mux); visible_state.time_synchronized = time_ok; portEXIT_CRITICAL(&state_mux);
        if (!time_ok) { state_message("Synchronizing time for secure Twitch login"); vTaskDelay(pdMS_TO_TICKS(100)); continue; }
        if (now_ms() < disconnect_until_ms) { vTaskDelay(pdMS_TO_TICKS(30)); continue; }
        if (!*configuration.client_id) { state_message("Wi-Fi ready; configure Public Twitch Client ID"); vTaskDelay(pdMS_TO_TICKS(100)); continue; }
        if (now_ms() < api_backoff_until_ms) { state_message("Twitch rate limit; waiting before retry"); vTaskDelay(pdMS_TO_TICKS(50)); continue; }
        if (*device_code) { login_poll(); vTaskDelay(pdMS_TO_TICKS(30)); continue; }
        if (login_requested && now_ms() >= next_network_attempt_ms) { login_begin(); vTaskDelay(pdMS_TO_TICKS(30)); continue; }
        if (!*tokens.access) {
            state_message(logged_out ? "Signed out; tap Connect to log in" : "Wi-Fi ready; tap Connect for Twitch login");
            vTaskDelay(pdMS_TO_TICKS(100)); continue;
        }
        if ((!account_validated || now_ms() - last_validation_ms >= 3600000 ||
             static_cast<std::int64_t>(std::time(nullptr)) + 90 >= tokens.expires_epoch) &&
            now_ms() >= next_network_attempt_ms) {
            const auto validation = validate_account();
            if (validation == Validation::NeedsLogin) {
                stop_sockets(); tokens_forget(); logged_out = true;
                state_message("Twitch login expired or revoked; connect again");
            } else if (validation == Validation::TemporaryFailure) {
                invalidate_moderation("Twitch permission could not be revalidated");
                next_network_attempt_ms = now_ms() + 15000;
                state_message("Cannot validate Twitch session; retrying");
            }
        }
        if (account_validated && active_socket < 0 && handoff_socket < 0 && now_ms() >= next_network_attempt_ms) {
            state_message("Connecting to Twitch chat");
            if (start_socket(0, kWsUrl, false)) active_socket = 0;
            else reconnect_later("Twitch connection unavailable; retrying");
        }
        if (active_socket >= 0) {
            auto& context = sockets[active_socket];
            if (context.disconnected.load()) {
                if (handoff_socket >= 0) {
                    stop_socket(active_socket); active_socket = -1;
                    gap("Twitch handoff interrupted — messages may be missing");
                }
                else reconnect_later("Twitch disconnected; reconnecting");
            } else if (context.welcomed && now_ms() - context.last_received_ms.load() > (context.keepalive_seconds + 3) * 1000LL) {
                reconnect_later("Twitch keepalive missed; reconnecting");
            } else if (!context.welcomed && now_ms() - context.started_ms > 20000) reconnect_later("Twitch welcome timed out; retrying");
            else if (now_ms() - context.started_ms > 12LL * 3600000)
                reconnect_later("Refreshing long-running Twitch connection");
        }
        if (handoff_socket >= 0) {
            auto& candidate = sockets[handoff_socket];
            if (candidate.disconnected.load() || now_ms() - candidate.started_ms > 20000) {
                stop_socket(handoff_socket); handoff_socket = -1;
                gap("Twitch server handoff failed — reconnecting");
                reconnect_later("Twitch handoff failed; reconnecting");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
} // namespace

bool twitch_start() {
    if (commands) return true;
    // Never erase existing NVS automatically on an initialization error.
    const auto default_nvs = nvs_flash_init();
    if (default_nvs != ESP_OK) { startup_failure("configuration NVS", default_nvs); return false; }
    const auto token_nvs = nvs_flash_init_partition("tokens");
    if (token_nvs != ESP_OK) { startup_failure("token NVS", token_nvs); return false; }
    state_message("Network worker starting");
    commands = xQueueCreate(3, sizeof(Command));
    event_queue_storage = static_cast<std::uint8_t*>(heap_caps_malloc(
        kEventQueueLength * sizeof(TwitchEvent), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (event_queue_storage) events = xQueueCreateStatic(
        kEventQueueLength, sizeof(TwitchEvent), event_queue_storage, &event_queue_control);
    frames = xQueueCreate(8, sizeof(RawFrame));
    moderation_results = xQueueCreate(1, sizeof(forearm::ModerationResult));
    if (commands && events && frames && moderation_results) {
        if (xTaskCreatePinnedToCore(worker, "twitch", 20480, nullptr, 3, nullptr, 0) == pdPASS) return true;
        startup_failure("worker task allocation", ESP_ERR_NO_MEM);
    } else {
        startup_failure(event_queue_storage ? "queue allocation" : "PSRAM event queue allocation", ESP_ERR_NO_MEM);
    }
    if (commands) vQueueDelete(commands);
    if (events) vQueueDelete(events);
    if (frames) vQueueDelete(frames);
    if (moderation_results) vQueueDelete(moderation_results);
    heap_caps_free(event_queue_storage); event_queue_storage = nullptr;
    commands = events = frames = moderation_results = nullptr;
    return false;
}
bool twitch_set_config(const char* ssid, const char* password, const char* client_id, const char* channel) {
    if (!commands || !ssid || !password || !*ssid || std::strlen(ssid) > 32 || std::strlen(password) > 64 ||
        !client_name(client_id) || !login_name(channel, true)) return false;
    const auto password_length = std::strlen(password);
    if (password_length && password_length < 8) return false;
    if (password_length == 64) {
        for (const auto* p = password; *p; ++p)
            if (!std::isxdigit(static_cast<unsigned char>(*p))) return false;
    }
    Command command;
    command.kind = CommandKind::Configure;
    if (!copy_exact(command.config.ssid, ssid) || !copy_exact(command.config.password, password) ||
        !copy_exact(command.config.client_id, client_id) || !copy_exact(command.config.channel, channel)) return false;
    for (auto& c : command.config.channel) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    invalidate_moderation("Device configuration is changing");
    force_validation.store(true);
    return xQueueSend(commands, &command, 0) == pdTRUE;
}
bool twitch_watch_channel(const char* channel, bool read_only) {
    if (!commands || !login_name(channel, false)) return false;
    Command command; command.kind = CommandKind::WatchChannel;
    if (!copy_exact(command.config.channel, channel)) return false;
    for (auto& c : command.config.channel) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    command.config.read_only = read_only ? 1 : 0;
    // Never copy saved SSID/password/token state on the UI caller. Permission
    // removal takes effect immediately, before NVS or socket work can block.
    if (read_only) {
        read_only_enforced.store(true);
        portENTER_CRITICAL(&state_mux); visible_state.read_only = true; portEXIT_CRITICAL(&state_mux);
    }
    invalidate_moderation("Channel profile is changing");
    force_validation.store(true);
    return xQueueSend(commands, &command, 0) == pdTRUE;
}
void twitch_request_login() {
    if (commands) {
        invalidate_moderation("New Twitch consent is pending"); force_validation.store(true);
        Command command; command.kind = CommandKind::Login; xQueueSend(commands, &command, 0);
    }
}
void twitch_logout() {
    if (commands) {
        invalidate_moderation("Signing out of Twitch"); force_validation.store(true);
        Command command; command.kind = CommandKind::Logout; xQueueSend(commands, &command, 0);
    }
}
bool twitch_next_event(TwitchEvent& event, std::uint32_t timeout_ms) {
    forearm::ModerationResult result;
    if (moderation_results && xQueueReceive(moderation_results, &result, 0) == pdTRUE) {
        portENTER_CRITICAL(&state_mux); moderation_ledger.finish(result.request_id, result.session_generation); portEXIT_CRITICAL(&state_mux);
        event = TwitchEvent{}; event.kind = TwitchEventKind::ModerationResult; event.moderation = result; return true;
    }
    return events && xQueueReceive(events, &event, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}
TwitchStatus twitch_status() {
    portENTER_CRITICAL(&state_mux); const auto snapshot = visible_state; portEXIT_CRITICAL(&state_mux);
    return snapshot;
}
bool twitch_network_busy() {
    return http_active.load() || (frames && uxQueueMessagesWaiting(frames) > 0);
}
forearm::ModerationCapabilities twitch_moderation_capabilities() {
    portENTER_CRITICAL(&state_mux); auto snapshot = moderation_caps; portEXIT_CRITICAL(&state_mux);
    snapshot.online = snapshot.online && wifi_connected.load();
    snapshot.read_only = snapshot.read_only || read_only_enforced.load();
    if (snapshot.read_only) {
        snapshot.can_delete = snapshot.can_ban = false;
        copy_text(snapshot.reason, "Read-only channel profile: moderation is disabled");
    }
    return snapshot;
}
forearm::ModerationResult twitch_moderate(const forearm::ModerationRequest& target) {
    const auto caps = twitch_moderation_capabilities();
    auto denial = forearm::moderation_validate(target, caps, now_ms());
    if (denial == forearm::ModerationDenial::None &&
        (!commands || http_active.load() || uxQueueMessagesWaiting(commands) ||
         (moderation_results && uxQueueMessagesWaiting(moderation_results)))) denial = forearm::ModerationDenial::Busy;
    if (denial == forearm::ModerationDenial::None) {
        portENTER_CRITICAL(&state_mux);
        // Recheck generation under the same lock as ledger admission.
        denial = moderation_ledger.accept(target, moderation_caps, now_ms());
        portEXIT_CRITICAL(&state_mux);
    }
    if (denial != forearm::ModerationDenial::None) return result_for(target, forearm::ModerationState::Denied, denial);
    Command command; command.kind = CommandKind::Moderate; command.moderation = target; command.submitted_ms = now_ms();
    if (xQueueSend(commands, &command, 0) != pdTRUE) {
        portENTER_CRITICAL(&state_mux); moderation_ledger.finish(target.request_id, target.session_generation); portEXIT_CRITICAL(&state_mux);
        return result_for(target, forearm::ModerationState::Denied, forearm::ModerationDenial::Busy);
    }
    portENTER_CRITICAL(&state_mux); ++visible_state.moderation_requests; portEXIT_CRITICAL(&state_mux);
    return result_for(target, forearm::ModerationState::Pending);
}
void twitch_request_disconnect_for_test(std::uint32_t duration_ms) {
    invalidate_moderation("Diagnostic disconnect is pending");
    disconnect_test_ms.store(std::clamp<std::uint32_t>(duration_ms, 100, 60000));
    force_validation.store(true);
}
void twitch_request_validation() {
    invalidate_moderation("Revalidating Twitch permission"); force_validation.store(true);
}
void twitch_request_wifi_disconnect_for_test(std::uint32_t duration_ms) {
    invalidate_moderation("Diagnostic Wi-Fi disconnect is pending");
    wifi_disconnect_test_ms.store(std::clamp<std::uint32_t>(duration_ms, 100, 60000));
    force_validation.store(true);
}
bool twitch_request_refresh_for_test() {
    const auto status = twitch_status();
    if (!commands || !status.authenticated || !wifi_connected.load() || status.login_pending ||
        uxQueueMessagesWaiting(commands) ||
        (moderation_results && uxQueueMessagesWaiting(moderation_results))) return false;
    // The worker serializes all TLS operations. A currently active read-only
    // request is safe to queue behind; the latch prevents duplicate queued or
    // executing refresh diagnostics, including the gap before HTTP starts.
    bool expected = false;
    if (!refresh_test_pending.compare_exchange_strong(expected, true)) return false;
    Command command; command.kind = CommandKind::RefreshTest;
    if (xQueueSend(commands, &command, 0) == pdTRUE) return true;
    refresh_test_pending.store(false);
    return false;
}
