#pragma once

#include <cstddef>
#include <cstdint>
#include "forearm/moderation.h"

struct _lv_font_t;

struct AppUiEmote {
    const char *id;
    const char *text; // Alt text remains in the original chat message.
    // Exact UTF-8 byte occurrence, validated against the retained message.
    // Missing/invalid spans and occurrences beyond eight keep literal text.
    std::uint16_t byte_offset = 0;
    std::uint16_t byte_length = 0;
    bool position_valid = false;
};

// All calls are made by the single LVGL/UI task. Networking must queue events.
struct AppUiStats {
    bool paused;
    bool demo;
    bool fixture_mode;
    bool authenticated;
    bool read_only;
    bool login_visible;
    bool selection_visible;
    std::uint32_t unread;
    std::uint32_t retained;
    std::uint64_t anchor_sequence;
    std::int32_t anchor_offset;
    std::int32_t scroll_y;
    std::int32_t history_height;
    std::uint64_t selected_sequence;
    std::uint32_t appended;
    std::uint32_t rejected;
    std::uint32_t taps;
    std::uint32_t scrolls;
    std::uint32_t emote_rows;
    std::uint32_t visible_emote_images;
    std::uint32_t tick_last_us;
    std::uint32_t tick_max_us;
    std::uint32_t layout_last_us;
    std::uint32_t layout_max_us;
    std::uint32_t render_last_us;
    std::uint32_t render_max_us;
    std::uint32_t measure_last_us;
    std::uint32_t measure_max_us;
    std::uint32_t label_updates;
    std::uint64_t text_draw_total_us;
    std::uint32_t text_draw_calls;
    std::uint32_t text_draw_max_us;
    bool sprite_cache_enabled;
    bool backdrop_culling_enabled;
    std::uint64_t backdrop_pixels_skipped;
    std::uint32_t backdrop_culled_draws;
    std::uint32_t backdrop_fallback_draws;
    std::uint32_t sprite_cache_bytes;
    std::uint32_t sprite_cache_hits;
    std::uint32_t sprite_cache_misses;
    std::uint32_t sprite_cache_builds;
    std::uint32_t sprite_cache_evictions;
    std::uint32_t sprite_cache_rejected;
    std::uint64_t sprite_cache_build_total_us;
    std::uint32_t sprite_cache_build_max_us;
    bool author_cache_enabled;
    std::uint32_t author_cache_bytes;
    std::uint32_t author_cache_hits;
    std::uint32_t author_cache_misses;
    std::uint32_t author_cache_builds;
    std::uint32_t author_cache_evictions;
    std::uint32_t author_cache_rejected;
    std::uint64_t author_cache_build_total_us;
    std::uint32_t author_cache_build_max_us;
    std::uint64_t backdrop_draw_total_us;
    std::uint32_t backdrop_draw_calls;
    std::uint32_t backdrop_draw_max_us;
    std::uint64_t author_draw_total_us;
    std::uint32_t author_draw_calls;
    std::uint32_t author_draw_max_us;
    std::uint64_t author_style_total_us;
    std::uint32_t author_style_calls;
    std::uint32_t author_style_max_us;
    std::uint64_t opaque_body_total_us;
    std::uint32_t opaque_body_calls;
    std::uint32_t opaque_body_max_us;
    std::uint64_t author_blit_total_us;
    bool moderation_pending;
    bool moderation_confirmation_visible;
    std::uint32_t moderation_request_id;
    std::uint32_t moderation_submitted;
    forearm::ModerationState moderation_state;
};

void app_ui_init();
void app_ui_tick();
bool app_ui_append(const char *channel_id, const char *message_id,
                   const char *user_id, const char *display_name,
                   const char *text, std::uint64_t received_at_ms = 0,
                   const char *user_login = nullptr);
bool app_ui_append_emotes(const char *channel_id, const char *message_id,
                         const char *user_id, const char *display_name,
                         const char *text, const AppUiEmote *emotes,
                         unsigned emote_count, std::uint64_t received_at_ms = 0,
                         const char *user_login = nullptr);
void app_ui_set_message_emotes(const char *channel_id, const char *message_id,
                              const AppUiEmote *emotes, unsigned emote_count);
void app_ui_delete_message(const char *channel_id, const char *message_id);
void app_ui_clear_user(const char *channel_id, const char *user_id);
void app_ui_clear_channel(const char *channel_id);
void app_ui_set_status(const char *status);
void app_ui_set_network(const char *status);
void app_ui_set_channel(const char *channel);
// Apply the profile before its capabilities. Starts read-only until explicitly
// configured; profile changes invalidate old confirmations and restoration.
void app_ui_set_read_only(bool enabled);
void app_ui_set_demo(bool enabled);
// Explicit synthetic-only hardware qualification. Clears history on entry/exit,
// suppresses automatic demo arrivals, and always disables moderation in fixture.
void app_ui_set_fixture_mode(bool enabled);
void app_ui_set_authenticated(const char *display_name, const char *channel);
void app_ui_set_login(const char *verification_url, const char *user_code);
void app_ui_hide_login();
void app_ui_show_setup(const char *title, const char *instructions);
void app_ui_pause();
void app_ui_go_live();
void app_ui_scroll(std::int32_t delta_pixels);
void app_ui_demo_burst(unsigned count);
AppUiStats app_ui_stats();

// Only requests account login; actual OAuth work belongs to the network task.
void app_ui_set_login_callback(void (*callback)());

// Exact transport identities are required before a message can be moderated.
// Source identity must match the receiving message; shared-chat targets fail
// closed. Calls are UI-owner only, immediately after append, and copy inputs.
void app_ui_set_message_moderation_metadata(const char *channel_id, const char *message_id,
                                          const char *source_channel_id, const char *source_message_id,
                                          bool protected_target);
void app_ui_set_moderation_capabilities(const forearm::ModerationCapabilities &capabilities);
// true means queued, never successful. No request is retried by the UI.
void app_ui_set_moderation_callback(bool (*callback)(const forearm::ModerationRequest &));
void app_ui_moderation_result(const forearm::ModerationResult &result);
void app_ui_set_moderation_login_callback(void (*callback)());

// The fallback must remain alive for the lifetime of the app (e.g. a compiled
// monochrome emoji font). Heights and labels use the same composed font.
void app_ui_set_font_fallback(const _lv_font_t *fallback);
void app_ui_reset_perf();
// Default ON after hardware A/B validation. Safe on the UI owner only.
void app_ui_set_sprite_cache(bool enabled);
// Bounded normal-state author sprites; pressed/masked/unsupported styles use
// LVGL's original label painter. UI owner only, no cache mutation during draw.
void app_ui_set_author_cache(bool enabled);
// Default ON after device A/B; runtime switchable for verification. Skips only
// the native opaque screen background
// under ready opaque cached body images; does not change layout or interaction.
void app_ui_set_backdrop_culling(bool enabled);
