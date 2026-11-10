#include "app_ui.h"
#include "emote_cache.h"
#include "lvgl.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

static std::int64_t fake_us = 0;
static lv_indev_state_t pointer_state = LV_INDEV_STATE_REL;
static lv_point_t pointer_point{};
static unsigned submitted = 0;
static forearm::ModerationRequest sent{};
static forearm::ModerationCapabilities caps{};

std::int64_t esp_timer_get_time() { return fake_us; }
bool emote_cache_init() { return false; }
bool emote_cache_request(const char *) { return false; }
const lv_img_dsc_t *emote_cache_get(const char *) { return nullptr; }
EmoteCacheStats emote_cache_stats() { return {}; }

static void pump(unsigned ms = 32) {
    for (unsigned elapsed = 0; elapsed < ms; elapsed += 8) {
        fake_us += 8000;
        lv_tick_inc(8);
        app_ui_tick();
        lv_timer_handler();
    }
}
static void touch(int x, int y, lv_indev_state_t state) {
    pointer_point = {static_cast<lv_coord_t>(x), static_cast<lv_coord_t>(y)};
    pointer_state = state;
    pump(24);
}
static void tap(int x, int y) {
    touch(x, y, LV_INDEV_STATE_PR);
    touch(x, y, LV_INDEV_STATE_REL);
    pump(24);
}
static lv_obj_t *find_label(lv_obj_t *obj, const char *caption) {
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return nullptr;
    for (int i = static_cast<int>(lv_obj_get_child_cnt(obj)) - 1; i >= 0; --i)
        if (auto *found = find_label(lv_obj_get_child(obj, i), caption)) return found;
    if (lv_obj_check_type(obj, &lv_label_class) && !std::strcmp(lv_label_get_text(obj), caption)) return obj;
    return nullptr;
}
static lv_obj_t *button_named(const char *caption) {
    auto *caption_obj = find_label(lv_scr_act(), caption);
    if (!caption_obj && !std::strcmp(caption, "Back to chat"))
        caption_obj = find_label(lv_scr_act(), LV_SYMBOL_LEFT "  Back to chat");
    if (!caption_obj) std::fprintf(stderr, "Missing button: %s\n", caption);
    assert(caption_obj);
    auto *obj = lv_obj_get_parent(caption_obj);
    assert(lv_obj_check_type(obj, &lv_btn_class));
    return obj;
}
static lv_point_t center(lv_obj_t *obj) {
    lv_obj_update_layout(obj);
    lv_area_t area{};
    lv_obj_get_coords(obj, &area);
    return {static_cast<lv_coord_t>((area.x1 + area.x2) / 2), static_cast<lv_coord_t>((area.y1 + area.y2) / 2)};
}
static void tap_button(const char *caption) {
    const auto p = center(button_named(caption));
    tap(p.x, p.y);
}
static void back_to_chat() {
    tap_button("Back to chat");
    pump(500);
    assert(!app_ui_stats().selection_visible);
}
static void fresh_message(const char *id = "message-1", const char *author = "Target", const char *user = "300",
                          const char *source = "100", bool metadata = true, bool protected_target = false) {
    app_ui_go_live();
    app_ui_set_demo(true);
    app_ui_set_demo(false);
    assert(app_ui_append("100", id, user, author, "A specific message to moderate.", fake_us / 1000));
    if (metadata) app_ui_set_message_moderation_metadata("100", id, source, id, protected_target);
    app_ui_go_live();
    pump(500);
}
static void select_message() {
    tap(30, 45);
    pump(500);
    assert(app_ui_stats().selection_visible);
}
static void result(forearm::ModerationState state, const char *detail) {
    forearm::ModerationResult value{};
    value.request_id = sent.request_id;
    value.session_generation = sent.session_generation;
    value.action = sent.action;
    value.state = state;
    std::snprintf(value.detail, sizeof(value.detail), "%s", detail);
    app_ui_moderation_result(value);
    pump(500);
}
static void confirm_action(const char *action) {
    tap_button(action);
    assert(app_ui_stats().moderation_confirmation_visible);
    pump(500);
}

int main() {
    lv_init();
    static lv_color_t pixels[320 * 32];
    static lv_disp_draw_buf_t draw_buffer;
    lv_disp_draw_buf_init(&draw_buffer, pixels, nullptr, 320 * 32);
    lv_disp_drv_t display;
    lv_disp_drv_init(&display);
    display.hor_res = 320;
    display.ver_res = 480;
    display.draw_buf = &draw_buffer;
    display.flush_cb = [](lv_disp_drv_t *driver, const lv_area_t *, lv_color_t *) { lv_disp_flush_ready(driver); };
    lv_disp_drv_register(&display);
    lv_indev_drv_t input;
    lv_indev_drv_init(&input);
    input.type = LV_INDEV_TYPE_POINTER;
    input.read_cb = [](lv_indev_drv_t *, lv_indev_data_t *data) {
        data->point = pointer_point;
        data->state = pointer_state;
    };
    lv_indev_drv_register(&input);
    app_ui_init();
    app_ui_set_authenticated("Owner", "room");
    caps.authenticated = caps.online = caps.role_verified = caps.can_delete = caps.can_ban = true;
    caps.generation = 7;
    std::snprintf(caps.broadcaster_id, sizeof(caps.broadcaster_id), "100");
    std::snprintf(caps.moderator_id, sizeof(caps.moderator_id), "200");
    app_ui_set_moderation_capabilities(caps);
    app_ui_set_moderation_callback([](const forearm::ModerationRequest &request) {
        ++submitted;
        sent = request;
        return true;
    });

    // Unknown, shared-chat and protected identities never enable controls.
    fresh_message("unknown", "Target", "300", "100", false);
    select_message();
    assert(lv_obj_has_state(button_named("Delete"), LV_STATE_DISABLED));
    back_to_chat();
    fresh_message("shared", "Target", "300", "999");
    select_message();
    assert(lv_obj_has_state(button_named("Ban"), LV_STATE_DISABLED));
    back_to_chat();
    fresh_message("protected", "Target", "300", "100", true, true);
    select_message();
    assert(lv_obj_has_state(button_named("Ban"), LV_STATE_DISABLED));
    back_to_chat();

    // First tap only navigates. Its coordinates cannot confirm the new screen.
    fresh_message();
    select_message();
    const auto initial = center(button_named("Delete"));
    tap_button("Delete");
    assert(app_ui_stats().moderation_confirmation_visible && submitted == 0);
    const auto final = center(button_named("Delete message"));
    assert(std::abs(initial.y - final.y) >= 56);
    tap(final.x, final.y); // Too soon: final control is still disarmed.
    tap(initial.x, initial.y); // A repeated first tap hits no final control.
    assert(submitted == 0);
    pump(500);
    touch(final.x, final.y, LV_INDEV_STATE_PR);
    touch(final.x + 18, final.y, LV_INDEV_STATE_PR);
    touch(final.x + 18, final.y, LV_INDEV_STATE_REL);
    assert(submitted == 0); // A drag inside the same button is not a tap.
    touch(final.x, final.y - 70, LV_INDEV_STATE_PR);
    touch(final.x, final.y, LV_INDEV_STATE_PR);
    touch(final.x, final.y, LV_INDEV_STATE_REL);
    assert(submitted == 0); // A contact starting outside cannot slide onto Confirm.
    tap(final.x, final.y);
    tap(final.x, final.y);
    assert(submitted == 1 && app_ui_stats().moderation_pending);
    assert(sent.confirmed && sent.action == forearm::ModerationAction::DeleteMessage);
    assert(!std::strcmp(sent.channel_id, "100") && !std::strcmp(sent.message_id, "message-1") && !std::strcmp(sent.user_id, "300"));
    for (unsigned i = 0; i < 600; ++i) {
        char id[64]; std::snprintf(id, sizeof(id), "eviction-%u", i);
        assert(app_ui_append("100", id, "301", "Later viewer", "History eviction must not change the target."));
    }
    pump(32);
    assert(app_ui_stats().retained == 512 && app_ui_stats().selected_sequence == 1);
    assert(!std::strcmp(sent.message_id, "message-1"));

    // Back and later selection cannot change the in-flight target or send twice.
    back_to_chat();
    fresh_message("new-message", "Other viewer", "301");
    select_message();
    assert(lv_obj_has_state(button_named("Delete"), LV_STATE_DISABLED));
    forearm::ModerationResult stale{};
    stale.request_id = sent.request_id + 1;
    stale.session_generation = sent.session_generation;
    stale.state = forearm::ModerationState::Succeeded;
    app_ui_moderation_result(stale);
    assert(app_ui_stats().moderation_pending);
    result(forearm::ModerationState::Succeeded, "Message removed by Twitch.");
    assert(!app_ui_stats().moderation_pending && submitted == 1);
    assert(find_label(lv_scr_act(), "TWITCH CONFIRMED"));
    assert(find_label(lv_scr_act(), "Target")); // Frozen result target, not Other viewer.
    assert(!find_label(lv_scr_act(), "Undo timeout") && !find_label(lv_scr_act(), "Unban user"));
    back_to_chat();

    // Deletion and account-generation changes invalidate an open confirmation.
    fresh_message("deleted"); select_message(); confirm_action("Delete");
    app_ui_delete_message("100", "deleted"); pump(500);
    assert(lv_obj_has_state(button_named("Delete message"), LV_STATE_DISABLED));
    tap_button("Delete message"); assert(submitted == 1);
    tap_button("Cancel"); pump(500);
    assert(lv_obj_has_state(button_named("Delete"), LV_STATE_DISABLED));
    assert(!lv_obj_has_state(button_named("Ban"), LV_STATE_DISABLED));
    back_to_chat();
    fresh_message("stale-session"); select_message(); confirm_action("Ban");
    ++caps.generation; app_ui_set_moderation_capabilities(caps); pump(500);
    assert(lv_obj_has_state(button_named("Ban user"), LV_STATE_DISABLED));
    tap_button("Ban user"); assert(submitted == 1);
    tap_button("Cancel"); pump(500); back_to_chat();

    // Each timeout preset is explicit; unknown results never retry themselves.
    for (const auto seconds : {30u, 60u, 600u}) {
        fresh_message("timeout"); select_message();
        tap_button("Timeout"); pump(500);
        tap_button(seconds == 30 ? "30 sec" : seconds == 60 ? "60 sec" : "10 min");
        assert(app_ui_stats().moderation_confirmation_visible);
        pump(500); tap_button("Timeout user");
        assert(app_ui_stats().moderation_pending && sent.timeout_seconds == seconds);
        assert(sent.action == forearm::ModerationAction::Timeout);
        const auto count = submitted;
        result(forearm::ModerationState::OutcomeUnknown, "Connection ended before acknowledgement.");
        assert(find_label(lv_scr_act(), "OUTCOME UNKNOWN"));
        assert(!find_label(lv_scr_act(), "Undo timeout") && !find_label(lv_scr_act(), "Unban user"));
        pump(1000); assert(submitted == count);
        back_to_chat();
    }

    // Only an acknowledged restriction offers restoration. Its target remains
    // the original immutable pending request through Back, eviction, a cleared
    // room and selection of a different shared-chat viewer.
    fresh_message("ban-original", "Frozen ban target", "300");
    select_message(); confirm_action("Ban"); tap_button("Ban user");
    assert(sent.action == forearm::ModerationAction::Ban && app_ui_stats().moderation_pending);
    const auto original_ban = sent;
    const auto ban_count = submitted;
    assert(!find_label(lv_scr_act(), "Unban user"));
    back_to_chat();
    for (unsigned i = 0; i < 600; ++i) {
        char id[64]; std::snprintf(id, sizeof(id), "ban-eviction-%u", i);
        assert(app_ui_append("100", id, "301", "Later viewer", "Original target has left history."));
    }
    app_ui_clear_channel("100");
    fresh_message("different-shared", "Different shared viewer", "301", "999");
    select_message();
    assert(lv_obj_has_state(button_named("Ban"), LV_STATE_DISABLED));
    result(forearm::ModerationState::Succeeded, "Twitch acknowledged the ban.");
    assert(find_label(lv_scr_act(), "Frozen ban target"));
    assert(!find_label(lv_obj_get_child(lv_scr_act(), lv_obj_get_child_cnt(lv_scr_act()) - 1),
                       "Different shared viewer")); // Ignore covered chat rows behind the modal.
    assert(!lv_obj_has_state(button_named("Unban user"), LV_STATE_DISABLED));
    const auto undo_point = center(button_named("Unban user"));
    tap_button("Unban user");
    assert(app_ui_stats().moderation_confirmation_visible && submitted == ban_count);
    assert(find_label(lv_scr_act(), "Frozen ban target"));
    const auto restore_point = center(button_named("Restore chat access"));
    assert(std::abs(undo_point.y - restore_point.y) >= 56);
    // A finger already down before the new screen arms cannot submit it after
    // 450ms passes. It must release, then make an entirely new contact.
    touch(restore_point.x, restore_point.y, LV_INDEV_STATE_PR);
    pump(500);
    touch(restore_point.x, restore_point.y, LV_INDEV_STATE_REL);
    assert(submitted == ban_count);
    tap_button("Cancel"); pump(500);
    assert(find_label(lv_scr_act(), "TWITCH CONFIRMED"));
    assert(find_label(lv_scr_act(), "Frozen ban target"));
    tap_button("Unban user"); pump(500);
    tap_button("Restore chat access"); tap(restore_point.x, restore_point.y);
    assert(submitted == ban_count + 1 && app_ui_stats().moderation_pending);
    assert(sent.action == forearm::ModerationAction::Unban && sent.confirmed && !sent.timeout_seconds);
    assert(sent.request_id != original_ban.request_id);
    assert(sent.session_generation == original_ban.session_generation);
    assert(!std::strcmp(sent.channel_id, original_ban.channel_id));
    assert(!std::strcmp(sent.user_id, original_ban.user_id));
    assert(!std::strcmp(sent.message_id, original_ban.message_id));
    assert(!std::strcmp(sent.source_channel_id, original_ban.source_channel_id));
    assert(!std::strcmp(sent.source_message_id, original_ban.source_message_id));
    back_to_chat();
    fresh_message("while-unban", "New target", "301"); select_message();
    assert(lv_obj_has_state(button_named("Ban"), LV_STATE_DISABLED));
    tap_button("Ban"); assert(submitted == ban_count + 1);
    result(forearm::ModerationState::Succeeded, "Twitch restored chat access.");
    assert(find_label(lv_scr_act(), "Frozen ban target"));
    assert(!find_label(lv_scr_act(), "Undo timeout") && !find_label(lv_scr_act(), "Unban user"));
    // A duplicate old acknowledgement cannot reopen the consumed Ban result.
    forearm::ModerationResult duplicate{};
    duplicate.request_id = original_ban.request_id;
    duplicate.session_generation = original_ban.session_generation;
    duplicate.action = original_ban.action;
    duplicate.state = forearm::ModerationState::Succeeded;
    app_ui_moderation_result(duplicate); pump(500);
    assert(!find_label(lv_scr_act(), "Unban user"));
    back_to_chat();

    // Restoration uses the same live role/scope/session gates as a new action.
    fresh_message("timeout-undo", "Frozen timeout target", "300"); select_message();
    tap_button("Timeout"); pump(500); tap_button("30 sec"); pump(500); tap_button("Timeout user");
    const auto original_timeout = sent;
    const auto timeout_count = submitted;
    app_ui_clear_user("100", "300"); app_ui_clear_channel("100");
    result(forearm::ModerationState::Succeeded, "Twitch acknowledged the timeout.");
    assert(find_label(lv_scr_act(), "Undo timeout"));
    for (unsigned gate = 0; gate < 4; ++gate) {
        const auto saved_caps = caps;
        if (gate == 0) caps.online = false;
        if (gate == 1) caps.role_verified = false;
        if (gate == 2) caps.can_ban = false;
        if (gate == 3) caps.authenticated = false;
        app_ui_set_moderation_capabilities(caps); pump(500);
        assert(lv_obj_has_state(button_named("Undo timeout"), LV_STATE_DISABLED));
        tap_button("Undo timeout"); assert(submitted == timeout_count);
        caps = saved_caps; app_ui_set_moderation_capabilities(caps); pump(500);
    }
    tap_button("Undo timeout"); pump(500);
    assert(find_label(lv_scr_act(), "Frozen timeout target"));
    ++caps.generation; app_ui_set_moderation_capabilities(caps); pump(500);
    assert(lv_obj_has_state(button_named("Restore chat access"), LV_STATE_DISABLED));
    tap_button("Restore chat access"); assert(submitted == timeout_count);
    tap_button("Cancel"); pump(500);
    assert(lv_obj_has_state(button_named("Undo timeout"), LV_STATE_DISABLED));
    back_to_chat();

    // A fresh acknowledged timeout may be explicitly removed under the current
    // session, without any selected chat line or surviving message history.
    fresh_message("timeout-current", "Current timeout target", "300"); select_message();
    tap_button("Timeout"); pump(500); tap_button("60 sec"); pump(500); tap_button("Timeout user");
    const auto current_timeout_id = sent.request_id;
    app_ui_clear_channel("100"); back_to_chat();
    result(forearm::ModerationState::Succeeded, "Twitch acknowledged the timeout.");
    assert(!app_ui_stats().selected_sequence);
    tap_button("Undo timeout"); pump(500); tap_button("Restore chat access");
    assert(sent.action == forearm::ModerationAction::Unban && !std::strcmp(sent.user_id, "300"));
    assert(sent.request_id != current_timeout_id && sent.request_id != original_timeout.request_id);
    result(forearm::ModerationState::Succeeded, "Twitch removed the timeout.");
    assert(!find_label(lv_scr_act(), "Undo timeout"));
    back_to_chat();

    for (const auto state : {forearm::ModerationState::Denied, forearm::ModerationState::RateLimited,
                             forearm::ModerationState::Failed, forearm::ModerationState::OutcomeUnknown}) {
        fresh_message("unconfirmed-ban"); select_message(); confirm_action("Ban"); tap_button("Ban user");
        const auto count = submitted;
        result(state, "Synthetic non-success result.");
        assert(!find_label(lv_scr_act(), "Unban user") && !find_label(lv_scr_act(), "Undo timeout"));
        pump(1000); assert(submitted == count);
        back_to_chat();
    }
    fresh_message("offline"); select_message();
    caps.online = false; app_ui_set_moderation_capabilities(caps); pump(500);
    assert(lv_obj_has_state(button_named("Delete"), LV_STATE_DISABLED));
    assert(lv_obj_has_state(button_named("Ban"), LV_STATE_DISABLED));
    caps.online = true; caps.role_verified = false;
    app_ui_set_moderation_capabilities(caps); pump(500);
    assert(lv_obj_has_state(button_named("Delete"), LV_STATE_DISABLED));
    caps.role_verified = true; caps.can_delete = caps.can_ban = false;
    app_ui_set_moderation_capabilities(caps); pump(500);
    assert(lv_obj_has_state(button_named("Delete"), LV_STATE_DISABLED));
    assert(lv_obj_has_state(button_named("Ban"), LV_STATE_DISABLED));
    back_to_chat();

    // Long-message Back preserves the reading position; idle Go live is sticky.
    caps.can_delete = caps.can_ban = true; app_ui_set_moderation_capabilities(caps);
    app_ui_set_demo(true); app_ui_set_demo(false);
    const std::string long_message(500, 'W');
    assert(app_ui_append("100", "long", "300", "Long viewer", long_message.c_str()));
    app_ui_set_message_moderation_metadata("100", "long", "100", "long", false);
    app_ui_go_live(); pump(500); app_ui_scroll(-300); pump(64);
    const auto old_scroll = app_ui_stats().scroll_y;
    assert(old_scroll > 0);
    tap(30, 200); pump(500); assert(app_ui_stats().selection_visible);
    back_to_chat(); assert(app_ui_stats().scroll_y == old_scroll && app_ui_stats().paused);
    for (unsigned i = 0; i < 3; ++i) {
        assert(app_ui_stats().unread == 0);
        tap_button(LV_SYMBOL_DOWN "  Go live"); pump(100);
        assert(!app_ui_stats().paused);
        tap_button(LV_SYMBOL_PAUSE); pump(100); assert(app_ui_stats().paused);
    }
    // Re-select a retained tombstone after timeout-style user clearing. Text
    // remains scrubbed, while Ban/Timeout must bind the original exact IDs.
    fresh_message("tombstone-reselect", "Tombstone target", "300");
    app_ui_clear_user("100", "300"); pump(64);
    select_message();
    assert(find_label(lv_scr_act(), "Message removed"));
    assert(find_label(lv_scr_act(), "Tombstone target"));
    assert(lv_obj_has_state(button_named("Delete"), LV_STATE_DISABLED));
    assert(!lv_obj_has_state(button_named("Timeout"), LV_STATE_DISABLED));
    assert(!lv_obj_has_state(button_named("Ban"), LV_STATE_DISABLED));
    confirm_action("Ban"); tap_button("Ban user");
    assert(sent.action == forearm::ModerationAction::Ban);
    assert(!std::strcmp(sent.channel_id, "100") && !std::strcmp(sent.user_id, "300"));
    assert(!std::strcmp(sent.message_id, "tombstone-reselect") && !std::strcmp(sent.source_message_id, "tombstone-reselect"));
    assert(!std::strcmp(sent.source_channel_id, "100") && !sent.protected_target);
    result(forearm::ModerationState::Succeeded, "Twitch acknowledged the original tombstone author.");
    assert(find_label(lv_scr_act(), "Tombstone target"));
    assert(find_label(lv_scr_act(), "Unban user"));
    back_to_chat();
    select_message();
    assert(lv_obj_has_state(button_named("Delete"), LV_STATE_DISABLED));
    tap_button("Timeout"); pump(500); tap_button("30 sec"); pump(500); tap_button("Timeout user");
    assert(sent.action == forearm::ModerationAction::Timeout && sent.timeout_seconds == 30);
    assert(!std::strcmp(sent.user_id, "300") && !std::strcmp(sent.message_id, "tombstone-reselect"));
    result(forearm::ModerationState::OutcomeUnknown, "No automatic retry for a tombstone target.");
    assert(!find_label(lv_scr_act(), "Undo timeout"));
    back_to_chat();

    app_ui_set_fixture_mode(true); pump(500);
    assert(app_ui_stats().fixture_mode && app_ui_stats().demo && app_ui_stats().retained == 0);
    assert(find_label(lv_scr_act(), "SYNTHETIC FIXTURE"));
    assert(app_ui_append("100", "fixture", "300", "Synthetic target", "Fixture content."));
    app_ui_set_message_moderation_metadata("100", "fixture", "100", "fixture", false);
    app_ui_go_live(); pump(5000);
    assert(app_ui_stats().retained == 1); // No timed demo feed mixed into fixtures.
    select_message();
    assert(lv_obj_has_state(button_named("Delete"), LV_STATE_DISABLED));
    app_ui_set_fixture_mode(false); pump(500);
    assert(!app_ui_stats().fixture_mode && !app_ui_stats().demo && app_ui_stats().retained == 0);
    std::puts("Native LVGL moderation UI passed: guarded contacts, distinct confirmation, drag/doubletap rejection, frozen pending target, permissions, source identity, deletion, stale session, all timeout presets, unknown/no retry, acknowledged-only Undo/Unban, immutable restoration after history eviction/clear/new selection, new request IDs, no held-contact confirmation.");
}
