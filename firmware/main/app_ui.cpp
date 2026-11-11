#include "app_ui.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <new>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "emote_cache.h"
#include "forearm/chat_store.h"
#include "forearm_fonts.h"
#include "lvgl.h"

namespace {
constexpr std::size_t kHistory = 512;
constexpr std::size_t kVisibleRows = 14;
constexpr int kSide = 12;
constexpr int kAuthorWidth = 224; // Keeps every author clear of the floating cog.
static_assert(sizeof(lv_color_t) == 2, "Native row sprites require RGB565 pixels");
constexpr std::size_t kBodyBytes = 2080;
constexpr std::uint32_t kEmoteMarker = 0xF0000;
constexpr bool kSpriteCacheDefault = true;
constexpr std::size_t kSpriteEntries = 32;
constexpr std::size_t kSpriteBudget = 2 * 1024 * 1024;
constexpr std::size_t kSpriteMaximumEntry = 1024 * 1024;
constexpr std::size_t kAuthorEntries = 16;
constexpr std::size_t kAuthorBudget = 256 * 1024;
constexpr std::size_t kAuthorMaximumEntry = 64 * 1024;
constexpr auto kTag = "chat-ui";
constexpr std::uint32_t kInk = 0x08090C;
constexpr std::uint32_t kSurface = 0x111017;
constexpr std::uint32_t kText = 0xEEEAF5;
constexpr std::uint32_t kMuted = 0xB9B0C8;
constexpr std::uint32_t kViolet = 0xBE98F1;
constexpr std::uint32_t kAmber = 0xEBC17E;
using Store = forearm::ChatStore<kHistory>;

struct EmoteRow {
    unsigned count = 0;
    char ids[8][65]{};
    std::uint16_t offsets[8]{}, lengths[8]{};
    std::uint8_t ready_mask = 0;
};
struct ModerationMetadata {
    char source_channel_id[33]{};
    char source_message_id[65]{};
    bool protected_target = false;
};

struct Layout {
    std::uint64_t sequence = 0;
    std::int32_t height = 0;
    EmoteRow *emotes = nullptr;
    ModerationMetadata *moderation = nullptr;
    std::uint16_t author_height = 27;
    std::uint16_t revision = 1;
};
struct Row {
    lv_obj_t *button = nullptr;
    lv_obj_t *author = nullptr;
    lv_obj_t *body = nullptr;
    char *body_text = nullptr;
    lv_draw_label_hint_t text_hint{-1, 0, 0};
    std::uint64_t sequence = 0;
    std::int32_t top = -1;
    std::int32_t height = -1;
    std::int32_t body_height = -1;
    std::uint16_t body_y = 43;
    std::uint16_t revision = 0;
    std::uint32_t font_generation = 0;
    bool deleted = false;
    bool sprite_attempted = false;
    bool author_attempted = false;
    std::array<lv_obj_t *, 8> images{};
    std::array<const lv_img_dsc_t *, 8> image_sources{};
};

Store *store = nullptr;
Layout *layout = nullptr; // One lifetime PSRAM allocation; visible rows stay internal.
std::array<Row, kVisibleRows> rows{};
std::size_t layout_count = 0;
std::int32_t total_height = 0;
std::int32_t scroll_y = 0;
int width = 320;
int height = 480;
int chat_height = 480;
lv_font_t message_font = lv_font_montserrat_24;
lv_font_t inline_font = lv_font_montserrat_24;
bool update_guard = false;
bool layout_dirty = false;
bool follow_dirty = false;
bool history_expired = false;
bool demo = true;
bool fixture_mode = false;
bool authenticated = false;
bool login_visible = false;
std::uint32_t appended = 0;
std::uint32_t rejected = 0;
std::uint32_t taps = 0;
std::uint32_t scrolls = 0;
std::uint32_t emote_rows = 0;
std::uint32_t last_ready_emotes = 0;
std::uint32_t font_generation = 1;
std::uint32_t label_updates = 0;
struct BodySprite {
    std::uint64_t sequence = 0;
    std::uint64_t last_used = 0;
    std::uint32_t font_generation = 0;
    bool deleted = false;
    std::uint16_t revision = 0;
    int width = 0;
    int height = 0;
    std::size_t bytes = 0;
    lv_color_t *pixels = nullptr;
    lv_img_dsc_t image{};
};
std::array<BodySprite, kSpriteEntries> body_sprites{};
bool sprite_cache_enabled = kSpriteCacheDefault;
bool backdrop_culling_enabled = true;
std::uint64_t backdrop_pixels_skipped = 0;
std::uint32_t backdrop_culled_draws = 0;
std::uint32_t backdrop_fallback_draws = 0;
std::size_t sprite_cache_bytes = 0;
std::uint64_t sprite_clock = 0;
std::uint32_t sprite_cache_hits = 0;
std::uint32_t sprite_cache_misses = 0;
std::uint32_t sprite_cache_builds = 0;
std::uint32_t sprite_cache_evictions = 0;
std::uint32_t sprite_cache_rejected = 0;
std::uint64_t sprite_cache_build_total_us = 0;
std::uint32_t sprite_cache_build_max_us = 0;
lv_obj_t *sprite_canvas = nullptr;
lv_color_t sprite_canvas_dummy{};
struct AuthorSprite {
    char name[101]{};
    const lv_font_t *font = nullptr;
    std::uint32_t font_generation = 0;
    std::uint64_t last_used = 0;
    std::uint16_t height = 0;
    std::uint8_t gutter = 0;
    lv_color_t text_color{};
    lv_color_t *pixels = nullptr;
    std::size_t bytes = 0;
    lv_img_dsc_t image{};
};
std::array<AuthorSprite, kAuthorEntries> author_sprites{};
bool author_cache_enabled = true;
std::uint32_t author_cache_bytes = 0, author_cache_hits = 0, author_cache_misses = 0;
std::uint32_t author_cache_builds = 0, author_cache_evictions = 0, author_cache_rejected = 0;
std::uint64_t author_cache_clock = 0, author_cache_build_total_us = 0;
std::uint32_t author_cache_build_max_us = 0;
std::uint64_t demo_serial = 0;
std::uint64_t next_demo_ms = 0;
std::uint64_t pressed_sequence = 0;
std::uint32_t pressed_tick = 0;
lv_point_t pressed_point{};
bool gesture_cancelled = false;
char channel[96] = "#forearm";
char network_status[128] = "Hardware demo";
char account_name[96] = "Not connected";
char transient_status[128] = "";
void (*login_callback)() = nullptr;

lv_obj_t *screen = nullptr;
lv_obj_t *chat = nullptr;
lv_obj_t *content = nullptr;
lv_obj_t *empty_hint = nullptr;
lv_obj_t *follow_button = nullptr;
lv_obj_t *follow_label = nullptr;
lv_obj_t *settings_hit = nullptr;
lv_obj_t *settings_visual = nullptr;
bool settings_requested = false;
bool settings_contact = false;
bool settings_dragged = false;
lv_point_t settings_pressed_point{};
std::uint32_t settings_pressed_tick = 0;
lv_obj_t *overlay = nullptr;
lv_obj_t *fixture_badge = nullptr;
bool selection_visible = false;
bool account_visible = false;
bool account_refresh = false;
bool read_only_profile = true;
std::uint32_t profile_generation = 1;
std::uint32_t target_profile_generation = 0;
std::uint32_t pending_profile_generation = 0;
bool close_requested = false;
bool connect_requested = false;
bool upgrade_requested = false;
enum class ModerationView { None, Selection, Timeout, Confirm, Pending, Result };
enum class ModerationNavigation { None, Selection, Timeout, Delete, Ban, Seconds30, Seconds60, Seconds600, Submit, Undo, Result };
ModerationView moderation_view = ModerationView::None;
ModerationNavigation moderation_navigation = ModerationNavigation::None;
forearm::ModerationCapabilities moderation_caps{};
forearm::ModerationRequest moderation_target{};
forearm::ModerationRequest pending_moderation{};
forearm::ModerationResult moderation_result{};
char selected_author[101]{};
char selected_channel[96]{};
char pending_author[101]{};
char pending_channel[96]{};
bool selected_metadata_verified = false;
bool pending_metadata_verified = false;
bool moderation_pending = false;
bool moderation_refresh = false;
bool moderation_result_ready = false;
std::uint32_t next_moderation_request = 1;
std::uint32_t moderation_submitted = 0;
std::uint32_t modal_generation = 0;
std::uint32_t modal_ready_tick = 0;
lv_obj_t *confirmation_button = nullptr;
lv_obj_t *modal_pressed_button = nullptr;
std::uint32_t modal_pressed_generation = 0;
std::uint32_t modal_pressed_tick = 0;
lv_point_t modal_pressed_point{};
bool modal_dragged = false;
bool (*moderation_callback)(const forearm::ModerationRequest &) = nullptr;
void (*moderation_login_callback)() = nullptr;

struct Duration {
    std::uint32_t last = 0;
    std::uint32_t maximum = 0;
    std::uint64_t total = 0;
    std::uint32_t calls = 0;
};
Duration tick_duration, layout_duration, render_duration, measure_duration;
Duration backdrop_draw_duration, author_draw_duration, author_style_duration;
Duration opaque_body_duration, author_blit_duration;
std::uint64_t text_draw_total_us = 0;
std::uint32_t text_draw_calls = 0;
std::uint32_t text_draw_max_us = 0;
void record(Duration &duration, std::uint32_t elapsed) {
    duration.last = elapsed;
    duration.maximum = std::max(duration.maximum, elapsed);
    duration.total += elapsed;
    ++duration.calls;
}
struct ScopedDuration {
    Duration &duration;
    std::int64_t start = esp_timer_get_time();
    ~ScopedDuration() { record(duration, static_cast<std::uint32_t>(esp_timer_get_time() - start)); }
};

std::uint64_t now_ms() { return static_cast<std::uint64_t>(esp_timer_get_time() / 1000); }
lv_color_t color(std::uint32_t value) { return lv_color_hex(value); }

lv_obj_t *plain(lv_obj_t *parent, int x, int y, int w, int h, std::uint32_t bg) {
    auto *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(obj, color(bg), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    return obj;
}

lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y, int w,
                const lv_font_t *font, std::uint32_t foreground) {
    auto *obj = lv_label_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_width(obj, w);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, color(foreground), 0);
    lv_obj_set_style_text_line_space(obj, 4, 0);
    lv_label_set_text(obj, text ? text : "");
    return obj;
}

lv_obj_t *button(lv_obj_t *parent, const char *text, int x, int y, int w, int h,
                 lv_event_cb_t callback, std::uint32_t bg = 0x241B32) {
    auto *obj = lv_btn_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(obj, color(bg), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, 10, 0);
    lv_obj_set_style_bg_color(obj, color(0x49315E), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(obj, LV_OPA_50, LV_STATE_DISABLED);
    auto *caption = label(obj, text, 0, 0, w - 12, &lv_font_montserrat_20, kText);
    lv_obj_clear_flag(caption, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(caption);
    if (callback) lv_obj_add_event_cb(obj, callback, LV_EVENT_CLICKED, nullptr);
    return obj;
}

std::int32_t max_scroll() { return std::max<std::int32_t>(0, total_height - chat_height); }

std::size_t first_visible(std::int32_t *top_out = nullptr) {
    std::int32_t top = 0;
    std::size_t index = 0;
    while (index + 1 < layout_count && top + layout[index].height <= scroll_y) {
        top += layout[index].height;
        ++index;
    }
    if (top_out) *top_out = top;
    return index;
}

void synchronize_anchor() {
    if (!store || !store->paused() || layout_count == 0) return;
    const auto index = first_visible();
    store->pauseAt(layout[index].sequence);
}

void update_status() {
    if (!store || !follow_label) return;
    char caption[64];
    static char previous_caption[64] = "";
    static bool previous_paused = false;
    const bool paused = store->paused();
    if (paused) {
        if (store->unread()) std::snprintf(caption, sizeof(caption), LV_SYMBOL_DOWN " Go live / %lu", static_cast<unsigned long>(store->unread()));
        else std::snprintf(caption, sizeof(caption), LV_SYMBOL_DOWN "  Go live");
    } else std::snprintf(caption, sizeof(caption), LV_SYMBOL_PAUSE);
    if (paused == previous_paused && !std::strcmp(caption, previous_caption)) return;
    std::snprintf(previous_caption, sizeof(previous_caption), "%s", caption);
    previous_paused = paused;
    if (paused) {
        const int pill_width = store->unread() > 9999 ? 252 : store->unread() ? 220 : 160;
        lv_obj_set_size(follow_button, pill_width, 52);
        lv_obj_set_pos(follow_button, width - pill_width - 10, height - 62);
        lv_obj_set_width(follow_label, pill_width - 16);
        lv_obj_set_style_radius(follow_button, 26, 0);
        lv_obj_set_style_bg_color(follow_button, color(0x8E60C8), 0);
        lv_obj_set_style_bg_opa(follow_button, LV_OPA_COVER, 0);
    } else {
        lv_obj_set_size(follow_button, 52, 52);
        lv_obj_set_pos(follow_button, width - 62, height - 62);
        lv_obj_set_width(follow_label, 40);
        lv_obj_set_style_radius(follow_button, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(follow_button, color(kSurface), 0);
        lv_obj_set_style_bg_opa(follow_button, LV_OPA_30, 0);
    }
    lv_label_set_text(follow_label, caption);
    lv_obj_center(follow_label);
}

bool emote_glyph(const lv_font_t *, lv_font_glyph_dsc_t *out, std::uint32_t letter, std::uint32_t) {
    if (letter < kEmoteMarker || letter >= kEmoteMarker + 8) return false;
    *out = {};
    out->adv_w = 28;
    out->bpp = 1;
    return true; // Zero ink; the matching bounded image object supplies pixels.
}
const std::uint8_t *emote_bitmap(const lv_font_t *, std::uint32_t) { return nullptr; }

const char *display_author(const forearm::Message &message) {
    return !message.user_login.view().empty() && !forearm_font_supports_text(message.display_name.c_str()) ?
           message.user_login.c_str() : message.display_name.c_str();
}
const lv_font_t *author_font(const char *name) {
    for (const auto *c = reinterpret_cast<const unsigned char *>(name); *c; ++c)
        if (*c >= 0x80) return &message_font;
    return &lv_font_montserrat_20;
}

void body_text(const forearm::Message &message, const EmoteRow *emotes, char *out) {
    if (message.deleted) { std::snprintf(out, kBodyBytes, "Message removed"); return; }
    const auto *raw = message.text.c_str();
    const auto length = message.text.view().size();
    std::size_t source = 0, written = 0;
    while (source < length && written + 5 < kBodyBytes) {
        unsigned replacement = 8;
        if (emotes) for (unsigned i = 0; i < emotes->count; ++i)
            if ((emotes->ready_mask & (1U << i)) && source == emotes->offsets[i]) { replacement = i; break; }
        if (replacement < 8) {
            const auto marker = kEmoteMarker + replacement;
            out[written++] = static_cast<char>(0xF0 | (marker >> 18));
            out[written++] = static_cast<char>(0x80 | ((marker >> 12) & 63));
            out[written++] = static_cast<char>(0x80 | ((marker >> 6) & 63));
            out[written++] = static_cast<char>(0x80 | (marker & 63));
            source += emotes->lengths[replacement];
        } else {
            auto next = static_cast<std::uint32_t>(source);
            const auto letter = _lv_txt_encoded_next(raw, &next);
            if (letter >= kEmoteMarker && letter < kEmoteMarker + 8) out[written++] = '?';
            else { const auto count = next - source; std::memcpy(out + written, raw + source, count); written += count; }
            source = next;
        }
    }
    if (message.text.truncated() && written + 4 < kBodyBytes) { std::memcpy(out + written, "...", 3); written += 3; }
    out[written] = 0;
}

int text_height(const char *text, const lv_font_t *font, int available_width) {
    // We only need height. lv_txt_get_size additionally walks every line again
    // to calculate its width, which is expensive for long fallback-font text.
    const auto length = std::strlen(text);
    std::size_t offset = 0;
    unsigned lines = 0;
    while (offset < length) {
        auto advance = _lv_txt_get_next_line(text + offset, font, 0,
                                             available_width, nullptr, LV_TEXT_FLAG_NONE);
        if (!advance) advance = std::max<unsigned>(1, _lv_txt_encoded_size(text + offset));
        offset += std::min<std::size_t>(advance, length - offset);
        ++lines;
    }
    if (offset && (text[offset - 1] == '\n' || text[offset - 1] == '\r')) ++lines;
    lines = std::max<unsigned>(lines, 1);
    return lines * (lv_font_get_line_height(font) + 4) - 4;
}
std::int32_t measure_message(const forearm::Message &message, const EmoteRow *emotes = nullptr,
                             std::uint16_t *author_height = nullptr) {
    ScopedDuration timer{measure_duration};
    char text[kBodyBytes];
    body_text(message, emotes, text);
    const auto *name = display_author(message);
    const auto author = text_height(name, author_font(name), kAuthorWidth);
    if (author_height) *author_height = static_cast<std::uint16_t>(author);
    return std::max<int>(76, 16 + author + text_height(text, &inline_font, width - 2 * kSide) + 12);
}

void body_descriptor(const Row &row, lv_draw_label_dsc_t &descriptor) {
    lv_draw_label_dsc_init(&descriptor);
    descriptor.font = &inline_font;
    descriptor.color = color(row.deleted ? kMuted : kText);
    descriptor.line_space = 4;
    descriptor.bidi_dir = LV_BASE_DIR_AUTO;
}

bool sprite_pinned(std::uint64_t sequence) {
    for (const auto &row : rows)
        if (row.sequence == sequence && row.button &&
            !lv_obj_has_flag(row.button, LV_OBJ_FLAG_HIDDEN)) return true;
    return false;
}

void free_sprite(BodySprite &sprite) {
    if (!sprite.pixels) return;
    // The descriptor is reused by the LRU. Close any LVGL decoder entry before
    // releasing its pixels; display DMA references the composed frame, not this.
    lv_img_cache_invalidate_src(&sprite.image);
    heap_caps_free(sprite.pixels);
    sprite_cache_bytes -= sprite.bytes;
    sprite = {};
}

void clear_sprites() {
    for (auto &sprite : body_sprites) free_sprite(sprite);
    for (auto &row : rows) row.sprite_attempted = false;
}

void invalidate_sprite(std::uint64_t sequence) {
    for (auto &sprite : body_sprites)
        if (sprite.sequence == sequence) free_sprite(sprite);
    for (auto &row : rows)
        if (row.sequence == sequence) row.sprite_attempted = false;
}

BodySprite *find_sprite(const Row &row) {
    for (auto &sprite : body_sprites) {
        if (sprite.pixels && sprite.sequence == row.sequence &&
            sprite.revision == row.revision &&
            sprite.font_generation == font_generation && sprite.deleted == row.deleted &&
            sprite.width == width - 2 * kSide && sprite.height == row.body_height)
            return &sprite;
    }
    return nullptr;
}

bool author_style_safe(const Row &row) {
    ScopedDuration timer{author_style_duration};
    auto *obj = row.author;
    if (!obj || !row.sequence || lv_obj_has_state(row.button, LV_STATE_PRESSED) ||
        lv_label_get_long_mode(obj) != LV_LABEL_LONG_WRAP || lv_label_get_recolor(obj) ||
        lv_obj_get_style_text_opa(obj, LV_PART_MAIN) != LV_OPA_COVER ||
        lv_obj_get_style_text_letter_space(obj, LV_PART_MAIN) ||
        lv_obj_get_style_text_line_space(obj, LV_PART_MAIN) != 4 ||
        lv_obj_get_style_base_dir(obj, LV_PART_MAIN) != LV_BASE_DIR_LTR ||
        lv_obj_get_style_text_decor(obj, LV_PART_MAIN) != LV_TEXT_DECOR_NONE ||
        lv_obj_calculate_style_text_align(obj, LV_PART_MAIN, lv_label_get_text(obj)) != LV_TEXT_ALIGN_LEFT ||
        lv_obj_get_style_bg_opa(obj, LV_PART_MAIN) != LV_OPA_TRANSP ||
        lv_obj_get_style_border_width(obj, LV_PART_MAIN) ||
        lv_obj_get_style_outline_width(obj, LV_PART_MAIN) ||
        lv_obj_get_style_shadow_width(obj, LV_PART_MAIN) ||
        lv_obj_get_style_pad_top(obj, LV_PART_MAIN) || lv_obj_get_style_pad_bottom(obj, LV_PART_MAIN) ||
        lv_obj_get_style_pad_left(obj, LV_PART_MAIN) || lv_obj_get_style_pad_right(obj, LV_PART_MAIN) ||
        lv_obj_get_scroll_x(obj) || lv_obj_get_scroll_y(obj) || lv_draw_mask_get_cnt()) return false;
    const auto *label = reinterpret_cast<const lv_label_t *>(obj);
    if (label->offset.x || label->offset.y || label->expand ||
        lv_label_get_text_selection_start(obj) != LV_DRAW_LABEL_NO_TXT_SEL ||
        lv_label_get_text_selection_end(obj) != LV_DRAW_LABEL_NO_TXT_SEL) return false;
    for (auto *ancestor = obj; ancestor; ancestor = lv_obj_get_parent(ancestor)) {
        if (lv_obj_has_flag(ancestor, LV_OBJ_FLAG_HIDDEN) ||
            _lv_obj_get_layer_type(ancestor) != LV_LAYER_TYPE_NONE ||
            lv_obj_get_style_opa(ancestor, LV_PART_MAIN) != LV_OPA_COVER ||
            lv_obj_get_style_clip_corner(ancestor, LV_PART_MAIN) ||
            lv_obj_get_style_radius(ancestor, LV_PART_MAIN) ||
            lv_obj_get_style_bg_img_src(ancestor, LV_PART_MAIN) ||
            lv_obj_get_style_outline_width(ancestor, LV_PART_MAIN) ||
            lv_obj_get_style_shadow_width(ancestor, LV_PART_MAIN) ||
            lv_obj_get_style_transform_width(ancestor, LV_PART_MAIN) ||
            lv_obj_get_style_transform_height(ancestor, LV_PART_MAIN)) return false;
        const auto border = lv_obj_get_style_border_width(ancestor, LV_PART_MAIN);
        if (border && (ancestor != row.button || border != 1 ||
                       lv_obj_get_style_border_side(ancestor, LV_PART_MAIN) != LV_BORDER_SIDE_BOTTOM)) return false;
        if (lv_obj_get_style_bg_opa(ancestor, LV_PART_MAIN) != LV_OPA_TRANSP &&
            (lv_obj_get_style_bg_opa(ancestor, LV_PART_MAIN) != LV_OPA_COVER ||
             lv_obj_get_style_bg_color_filtered(ancestor, LV_PART_MAIN).full != color(kInk).full ||
             lv_obj_get_style_bg_grad_dir(ancestor, LV_PART_MAIN) != LV_GRAD_DIR_NONE ||
             lv_obj_get_style_bg_img_src(ancestor, LV_PART_MAIN))) return false;
    }
    return true;
}

bool author_image_area(const Row &row, lv_area_t &area, int &gutter) {
    lv_obj_get_coords(row.author, &area);
    gutter = _lv_obj_get_ext_draw_size(row.author);
    if (gutter < 0 || gutter > 7 || lv_area_get_width(&area) != kAuthorWidth ||
        lv_area_get_height(&area) != row.body_y - 16) return false;
    area.x1 -= gutter; area.x2 += gutter;
    area.y1 -= gutter; area.y2 += gutter;
    lv_area_t parent, body;
    lv_obj_get_content_coords(row.button, &parent);
    lv_obj_get_coords(row.body, &body);
    // Native labels permit glyph ink outside their logical box. These gutters
    // match that native extension, but may cover only the row's otherwise blank
    // ink margins: never a sibling body, divider, or another row.
    return _lv_area_is_in(&area, &parent, 0) && area.y2 < body.y1 &&
           area.x2 < width - 72;
}

AuthorSprite *find_author_sprite(const Row &row) {
    const auto *name = lv_label_get_text(row.author);
    const auto *font = lv_obj_get_style_text_font(row.author, LV_PART_MAIN);
    const auto foreground = lv_obj_get_style_text_color_filtered(row.author, LV_PART_MAIN);
    for (auto &sprite : author_sprites)
        if (sprite.pixels && sprite.font_generation == font_generation && sprite.font == font &&
            sprite.height == row.body_y - 16 && sprite.text_color.full == foreground.full &&
            sprite.gutter == _lv_obj_get_ext_draw_size(row.author) &&
            !std::strcmp(sprite.name, name)) return &sprite;
    return nullptr;
}

void free_author_sprite(AuthorSprite &sprite) {
    if (!sprite.pixels) return;
    lv_img_cache_invalidate_src(&sprite.image);
    heap_caps_free(sprite.pixels);
    author_cache_bytes -= sprite.bytes;
    sprite = {};
}
void clear_author_sprites() {
    for (auto &sprite : author_sprites) free_author_sprite(sprite);
    for (auto &row : rows) row.author_attempted = false;
}
bool author_sprite_pinned(const AuthorSprite &sprite) {
    for (const auto &row : rows) {
        if (!row.sequence || !row.author || lv_obj_has_flag(row.button, LV_OBJ_FLAG_HIDDEN) ||
            row.top + 8 >= scroll_y + chat_height || row.top + row.body_y - 8 <= scroll_y) continue;
        if (find_author_sprite(row) == &sprite) return true;
    }
    return false;
}
bool evict_author_sprite() {
    AuthorSprite *victim = nullptr;
    for (auto &sprite : author_sprites) {
        if (!sprite.pixels || author_sprite_pinned(sprite)) continue;
        if (!victim || sprite.last_used < victim->last_used) victim = &sprite;
    }
    if (!victim) return false;
    free_author_sprite(*victim);
    ++author_cache_evictions;
    return true;
}

bool opaque_author_area(const Row &row, const lv_area_t &clip, lv_area_t &area) {
    if (!author_cache_enabled || !author_style_safe(row)) return false;
    int gutter;
    if (!author_image_area(row, area, gutter)) return false;
    const auto *sprite = find_author_sprite(row);
    if (!sprite || sprite->image.header.cf != LV_IMG_CF_TRUE_COLOR ||
        sprite->image.data != reinterpret_cast<const std::uint8_t *>(sprite->pixels) ||
        sprite->image.header.w != kAuthorWidth + 2 * gutter ||
        sprite->image.header.h != sprite->height + 2 * gutter ||
        sprite->image.data_size != sprite->bytes ||
        sprite->bytes != (kAuthorWidth + 2 * gutter) * (sprite->height + 2 * gutter) * sizeof(lv_color_t)) return false;
    if (!_lv_area_intersect(&area, &area, &clip)) return false;
    for (auto *obj = row.button; obj; obj = lv_obj_get_parent(obj)) {
        lv_area_t bounds;
        lv_obj_get_coords(obj, &bounds);
        if (!_lv_area_intersect(&area, &area, &bounds)) return false;
    }
    return true;
}

// All cache mutation happens on this UI owner before/after LVGL rendering.
// These checks borrow stable RGB565 storage without allocating, evicting or
// changing objects. Reject any layering/masking/opacity we cannot prove opaque.
bool opaque_body_area(const Row &row, const lv_area_t &clip, lv_area_t &area) {
    ScopedDuration timer{opaque_body_duration};
    if (!row.body_text || !row.sequence || !row.body || !row.button) return false;
    const auto *sprite = find_sprite(row);
    if (!sprite || sprite->image.header.cf != LV_IMG_CF_TRUE_COLOR ||
        sprite->image.header.w != sprite->width || sprite->image.header.h != sprite->height ||
        sprite->image.data != reinterpret_cast<const std::uint8_t *>(sprite->pixels) ||
        sprite->image.data_size != sprite->bytes ||
        sprite->bytes != static_cast<std::size_t>(sprite->width) * sprite->height * sizeof(lv_color_t)) return false;
    lv_obj_get_coords(row.body, &area);
    if (sprite->width != lv_area_get_width(&area) || sprite->height != lv_area_get_height(&area)) return false;
    if (!_lv_area_intersect(&area, &area, &clip)) return false;
    // Intersect every ancestor's actual child clip, conservatively including
    // overflow-visible parents too. Underestimating coverage only fills extra.
    for (auto *obj = row.body; obj; obj = lv_obj_get_parent(obj)) {
        if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN) || _lv_obj_get_layer_type(obj) != LV_LAYER_TYPE_NONE ||
            lv_obj_get_style_opa(obj, LV_PART_MAIN) != LV_OPA_COVER ||
            lv_obj_get_style_clip_corner(obj, LV_PART_MAIN) ||
            lv_obj_get_style_radius(obj, LV_PART_MAIN) ||
            lv_obj_get_style_transform_width(obj, LV_PART_MAIN) ||
            lv_obj_get_style_transform_height(obj, LV_PART_MAIN)) return false;
        lv_area_t bounds;
        lv_obj_get_coords(obj, &bounds);
        if (!_lv_area_intersect(&area, &area, &bounds)) return false;
    }
    return true;
}

void backdrop_draw_event(lv_event_t *event) {
    ScopedDuration timer{backdrop_draw_duration};
    if (!backdrop_culling_enabled || lv_event_get_target(event) != screen) return;
    auto *part = static_cast<lv_obj_draw_part_dsc_t *>(lv_event_get_param(event));
    if (!part || part->part != LV_PART_MAIN || part->class_p != &lv_obj_class ||
        part->type != LV_OBJ_DRAW_PART_RECTANGLE || !part->rect_dsc || !part->draw_area) return;
    auto *descriptor = part->rect_dsc;
    // Manipulate only this invocation's background descriptor, not object
    // styles or COVER_CHECK. The ordinary full rectangle is the fallback.
    if (!sprite_cache_enabled || overlay || descriptor->bg_opa != LV_OPA_COVER ||
        descriptor->radius || descriptor->bg_grad.dir != LV_GRAD_DIR_NONE || descriptor->bg_img_src ||
        descriptor->border_width || descriptor->outline_width || descriptor->shadow_width ||
        descriptor->blend_mode != LV_BLEND_MODE_NORMAL || lv_draw_mask_get_cnt()) {
        ++backdrop_fallback_draws;
        return;
    }
    lv_area_t clip;
    if (!_lv_area_intersect(&clip, part->draw_area, part->draw_ctx->clip_area)) return;
    std::array<lv_area_t, 2 * kVisibleRows> covered{};
    std::size_t count = 0;
    for (const auto &row : rows) {
        lv_area_t area;
        if (opaque_body_area(row, clip, area)) covered[count++] = area;
        if (opaque_author_area(row, clip, area)) covered[count++] = area;
    }
    if (!count) { ++backdrop_fallback_draws; return; }
    std::sort(covered.begin(), covered.begin() + count, [](const lv_area_t &a, const lv_area_t &b) {
        return a.y1 < b.y1;
    });
    // Native message bodies are disjoint vertical bands. Unrecognized overlap
    // falls back intact instead of assuming an unsafe coverage union.
    for (std::size_t i = 1; i < count; ++i)
        if (covered[i].y1 <= covered[i - 1].y2) { ++backdrop_fallback_draws; return; }

    auto fill = [&](lv_coord_t x1, lv_coord_t y1, lv_coord_t x2, lv_coord_t y2) {
        if (x1 > x2 || y1 > y2) return;
        const lv_area_t area{x1, y1, x2, y2};
        lv_draw_rect(part->draw_ctx, descriptor, &area);
    };
    lv_coord_t next_y = clip.y1;
    for (std::size_t i = 0; i < count; ++i) {
        const auto &area = covered[i];
        fill(clip.x1, next_y, clip.x2, area.y1 - 1);
        fill(clip.x1, area.y1, area.x1 - 1, area.y2);
        fill(area.x2 + 1, area.y1, clip.x2, area.y2);
        next_y = area.y2 + 1;
        backdrop_pixels_skipped += static_cast<std::uint64_t>(lv_area_get_width(&area)) * lv_area_get_height(&area);
    }
    fill(clip.x1, next_y, clip.x2, clip.y2);
    descriptor->bg_opa = LV_OPA_TRANSP;
    ++backdrop_culled_draws;
}

bool evict_sprite() {
    BodySprite *victim = nullptr;
    for (auto &sprite : body_sprites) {
        if (!sprite.pixels || sprite_pinned(sprite.sequence)) continue;
        if (!victim || sprite.last_used < victim->last_used) victim = &sprite;
    }
    if (!victim) return false;
    free_sprite(*victim);
    ++sprite_cache_evictions;
    return true;
}

void prepare_sprite(Row &row) {
    if (!sprite_cache_enabled || !row.body_text || !row.sequence || row.body_height <= 0) return;
    if (find_sprite(row) || row.sprite_attempted) return;
    row.sprite_attempted = true;
    const int body_width = width - 2 * kSide;
    const auto bytes = static_cast<std::size_t>(body_width) * row.body_height * sizeof(lv_color_t);
    if (body_width <= 0 || row.body_height > 2047 || bytes > kSpriteMaximumEntry) {
        ++sprite_cache_rejected;
        return; // Oversized records retain the existing clipped text renderer.
    }
    while (sprite_cache_bytes + bytes > kSpriteBudget)
        if (!evict_sprite()) { ++sprite_cache_rejected; return; }
    BodySprite *slot = nullptr;
    for (auto &sprite : body_sprites) if (!sprite.pixels) { slot = &sprite; break; }
    if (!slot && evict_sprite())
        for (auto &sprite : body_sprites) if (!sprite.pixels) { slot = &sprite; break; }
    if (!slot) { ++sprite_cache_rejected; return; }
    auto *pixels = static_cast<lv_color_t *>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!pixels) { ++sprite_cache_rejected; return; }
    if (!sprite_canvas) {
        sprite_canvas = lv_canvas_create(screen);
        if (!sprite_canvas) { heap_caps_free(pixels); ++sprite_cache_rejected; return; }
        lv_obj_add_flag(sprite_canvas, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(sprite_canvas, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }
    const auto started = esp_timer_get_time();
    lv_color_fill(pixels, color(kInk), bytes / sizeof(lv_color_t));
    lv_canvas_set_buffer(sprite_canvas, pixels, body_width, row.body_height, LV_IMG_CF_TRUE_COLOR);
    lv_draw_label_dsc_t descriptor;
    body_descriptor(row, descriptor);
    // Rasterize outside the main DRAW callback. This can cost one cold frame;
    // subsequent frames copy opaque RGB565 instead of mixing each glyph pixel.
    lv_canvas_draw_text(sprite_canvas, 0, 0, body_width, &descriptor, row.body_text);
    lv_canvas_set_buffer(sprite_canvas, &sprite_canvas_dummy, 1, 1, LV_IMG_CF_TRUE_COLOR);
    const auto elapsed = static_cast<std::uint32_t>(esp_timer_get_time() - started);
    sprite_cache_build_total_us += elapsed;
    sprite_cache_build_max_us = std::max(sprite_cache_build_max_us, elapsed);
    ++sprite_cache_builds;
    slot->sequence = row.sequence;
    slot->last_used = ++sprite_clock;
    slot->font_generation = font_generation;
    slot->deleted = row.deleted;
    slot->revision = row.revision;
    slot->width = body_width;
    slot->height = row.body_height;
    slot->bytes = bytes;
    slot->pixels = pixels;
    slot->image.header.always_zero = 0;
    slot->image.header.cf = LV_IMG_CF_TRUE_COLOR;
    slot->image.header.w = body_width;
    slot->image.header.h = row.body_height;
    slot->image.data_size = bytes;
    slot->image.data = reinterpret_cast<const std::uint8_t *>(pixels);
    sprite_cache_bytes += bytes;
}

void prepare_author_sprite(Row &row) {
    if (!author_cache_enabled || !author_style_safe(row)) return;
    lv_area_t area;
    int gutter;
    if (!author_image_area(row, area, gutter)) return;
    if (auto *sprite = find_author_sprite(row)) { sprite->last_used = ++author_cache_clock; return; }
    if (row.author_attempted) return;
    row.author_attempted = true;
    const int author_height = row.body_y - 16;
    const int image_width = kAuthorWidth + 2 * gutter;
    const int image_height = author_height + 2 * gutter;
    const auto bytes = static_cast<std::size_t>(image_width) * image_height * sizeof(lv_color_t);
    const auto *name = lv_label_get_text(row.author);
    if (author_height <= 0 || bytes > kAuthorMaximumEntry || std::strlen(name) > 100) {
        ++author_cache_rejected; return;
    }
    while (author_cache_bytes + bytes > kAuthorBudget)
        if (!evict_author_sprite()) { ++author_cache_rejected; return; }
    AuthorSprite *slot = nullptr;
    for (auto &sprite : author_sprites) if (!sprite.pixels) { slot = &sprite; break; }
    if (!slot && evict_author_sprite())
        for (auto &sprite : author_sprites) if (!sprite.pixels) { slot = &sprite; break; }
    if (!slot) { ++author_cache_rejected; return; }
    auto *pixels = static_cast<lv_color_t *>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!pixels) { ++author_cache_rejected; return; }
    if (!sprite_canvas) {
        sprite_canvas = lv_canvas_create(screen);
        if (!sprite_canvas) { heap_caps_free(pixels); ++author_cache_rejected; return; }
        lv_obj_add_flag(sprite_canvas, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(sprite_canvas, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }
    const auto started = esp_timer_get_time();
    lv_color_fill(pixels, color(kInk), bytes / sizeof(lv_color_t));
    lv_canvas_set_buffer(sprite_canvas, pixels, image_width, image_height, LV_IMG_CF_TRUE_COLOR);
    lv_draw_label_dsc_t descriptor;
    lv_draw_label_dsc_init(&descriptor);
    lv_obj_init_draw_label_dsc(row.author, LV_PART_MAIN, &descriptor);
    lv_bidi_calculate_align(&descriptor.align, &descriptor.bidi_dir, name);
    lv_canvas_draw_text(sprite_canvas, gutter, gutter, kAuthorWidth, &descriptor, name);
    lv_canvas_set_buffer(sprite_canvas, &sprite_canvas_dummy, 1, 1, LV_IMG_CF_TRUE_COLOR);
    const auto elapsed = static_cast<std::uint32_t>(esp_timer_get_time() - started);
    author_cache_build_total_us += elapsed;
    author_cache_build_max_us = std::max(author_cache_build_max_us, elapsed);
    ++author_cache_builds;
    std::snprintf(slot->name, sizeof(slot->name), "%s", name);
    slot->font = descriptor.font;
    slot->font_generation = font_generation;
    slot->last_used = ++author_cache_clock;
    slot->height = author_height;
    slot->gutter = gutter;
    slot->text_color = descriptor.color;
    slot->pixels = pixels;
    slot->bytes = bytes;
    slot->image.header.always_zero = 0;
    slot->image.header.cf = LV_IMG_CF_TRUE_COLOR;
    slot->image.header.w = image_width;
    slot->image.header.h = image_height;
    slot->image.data_size = bytes;
    slot->image.data = reinterpret_cast<const std::uint8_t *>(pixels);
    author_cache_bytes += bytes;
    row.author_attempted = false;
}

void author_draw_event(lv_event_t *event) {
    ScopedDuration timer{author_draw_duration};
    if (!author_cache_enabled) return;
    auto &row = *static_cast<Row *>(lv_event_get_user_data(event));
    auto *sprite = find_author_sprite(row);
    lv_area_t area;
    int gutter;
    if (!sprite || !author_style_safe(row) || !author_image_area(row, area, gutter)) {
        ++author_cache_misses;
        return; // Continue into LVGL's original label event, completely intact.
    }
    lv_draw_img_dsc_t descriptor;
    lv_draw_img_dsc_init(&descriptor);
    {
        ScopedDuration blit_timer{author_blit_duration};
        lv_draw_img(lv_event_get_draw_ctx(event), &descriptor, &area, &sprite->image);
    }
    ++author_cache_hits;
    lv_event_stop_processing(event);
}

void prepare_visible_sprites() {
    if (overlay) return;
    for (auto &row : rows) {
        if (!row.sequence || lv_obj_has_flag(row.button, LV_OBJ_FLAG_HIDDEN)) continue;
        const auto body_top = row.top + row.body_y;
        if (body_top < scroll_y + chat_height && body_top + row.body_height > scroll_y) prepare_sprite(row);
        if (row.top + 8 < scroll_y + chat_height && row.top + row.body_y - 8 > scroll_y)
            prepare_author_sprite(row);
    }
}

void body_draw_event(lv_event_t *event) {
    auto *row = static_cast<Row *>(lv_event_get_user_data(event));
    if (!row->body_text) return;
    lv_draw_label_dsc_t descriptor;
    body_descriptor(*row, descriptor);
    lv_area_t area;
    lv_obj_get_coords(lv_event_get_target(event), &area);
    const auto started = esp_timer_get_time();
    BodySprite *sprite = sprite_cache_enabled ? find_sprite(*row) : nullptr;
    if (sprite) {
        sprite->last_used = ++sprite_clock;
        ++sprite_cache_hits;
        lv_draw_img_dsc_t image_descriptor;
        lv_draw_img_dsc_init(&image_descriptor);
        lv_draw_img(lv_event_get_draw_ctx(event), &image_descriptor, &area, &sprite->image);
    } else {
        if (sprite_cache_enabled) ++sprite_cache_misses;
        lv_draw_label(lv_event_get_draw_ctx(event), &descriptor, &area,
                      row->body_text, &row->text_hint);
    }
    const auto elapsed = static_cast<std::uint32_t>(esp_timer_get_time() - started);
    text_draw_total_us += elapsed;
    ++text_draw_calls;
    text_draw_max_us = std::max(text_draw_max_us, elapsed);
}

void render_emotes(Row &row, const Layout *item) {
    const auto *meta = item ? item->emotes : nullptr;
    std::array<lv_point_t, 8> positions{};
    std::array<bool, 8> positioned{};
    if (meta && row.body_text) {
        const auto length = std::strlen(row.body_text);
        std::size_t line = 0;
        int y = row.body_y;
        while (line < length) {
            auto count = _lv_txt_get_next_line(row.body_text + line, &inline_font, 0,
                                               width - 2 * kSide, nullptr, LV_TEXT_FLAG_NONE);
            if (!count) count = std::max<unsigned>(1, _lv_txt_encoded_size(row.body_text + line));
            const auto end = std::min<std::size_t>(length, line + count);
            auto cursor = static_cast<std::uint32_t>(line);
            int x = kSide;
            while (cursor < end) {
                const auto letter = _lv_txt_encoded_next(row.body_text, &cursor);
                auto peek = cursor;
                const auto next = cursor < end ? _lv_txt_encoded_next(row.body_text, &peek) : 0;
                if (letter >= kEmoteMarker && letter < kEmoteMarker + 8) {
                    const auto index = letter - kEmoteMarker;
                    positions[index] = {static_cast<lv_coord_t>(x), static_cast<lv_coord_t>(y)};
                    positioned[index] = true;
                }
                x += lv_font_get_glyph_width(&inline_font, letter, next);
            }
            line = end;
            y += lv_font_get_line_height(&inline_font) + 4;
        }
    }
    for (unsigned i = 0; i < row.images.size(); ++i) {
        const auto *source = meta && i < meta->count && positioned[i] && (meta->ready_mask & (1U << i)) ?
                             emote_cache_get(meta->ids[i]) : nullptr;
        if (!source) {
            if (row.images[i]) lv_obj_add_flag(row.images[i], LV_OBJ_FLAG_HIDDEN);
            row.image_sources[i] = nullptr;
            continue;
        }
        if (!row.images[i]) {
            row.images[i] = lv_img_create(row.button);
            lv_obj_clear_flag(row.images[i], LV_OBJ_FLAG_CLICKABLE);
            lv_img_set_pivot(row.images[i], 0, 0);
        }
        if (row.image_sources[i] != source) {
            lv_img_set_src(row.images[i], source);
            row.image_sources[i] = source;
            const auto largest = std::max<unsigned>(source->header.w, source->header.h);
            const auto zoom = largest > 27 ? std::max<unsigned>(1, 27U * 256U / largest) : 256;
            lv_img_set_zoom(row.images[i], zoom);
        }
        const auto zoom = lv_img_get_zoom(row.images[i]);
        const int image_height = (source->header.h * zoom + 255) / 256;
        lv_obj_set_pos(row.images[i], positions[i].x,
                       positions[i].y + std::max<int>(0, (lv_font_get_line_height(&inline_font) - image_height) / 2));
        lv_obj_clear_flag(row.images[i], LV_OBJ_FLAG_HIDDEN);
    }
}

void refresh_visible_emotes() {
    std::int32_t old_top = 0;
    const auto anchor = first_visible(&old_top);
    const auto offset = scroll_y - old_top;
    bool changed = false;
    for (std::size_t i = 0; i < layout_count; ++i) {
        auto &item = layout[i];
        if (!item.emotes) continue;
        std::uint8_t mask = 0;
        for (unsigned j = 0; j < item.emotes->count; ++j)
            if (emote_cache_get(item.emotes->ids[j])) mask |= 1U << j;
        if (mask == item.emotes->ready_mask) continue;
        item.emotes->ready_mask = mask;
        const auto *message = store->at(i);
        if (!message) continue;
        const auto old_height = item.height;
        item.height = measure_message(*message, item.emotes, &item.author_height);
        total_height += item.height - old_height;
        ++item.revision;
        invalidate_sprite(item.sequence);
        changed = true;
    }
    if (changed) {
        if (store->paused() && layout_count) {
            std::int32_t top = 0;
            for (std::size_t i = 0; i < anchor; ++i) top += layout[i].height;
            scroll_y = top + std::min<std::int32_t>(offset, layout[anchor].height - 1);
        }
        layout_dirty = true;
    }
}

void release_deleted_emotes() {
    for (std::size_t i = 0; i < layout_count; ++i) {
        const auto *message = store->at(i);
        if (message && message->deleted) invalidate_sprite(message->sequence);
        if (layout[i].emotes && message && message->deleted) {
            heap_caps_free(layout[i].emotes);
            layout[i].emotes = nullptr;
            --emote_rows;
        }
    }
}

void render_rows() {
    if (!store || !content) return;
    ScopedDuration timer{render_duration};
    if (empty_hint) {
        if (layout_count) lv_obj_add_flag(empty_hint, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_clear_flag(empty_hint, LV_OBJ_FLAG_HIDDEN);
    }
    std::int32_t top = 0;
    std::size_t first = first_visible(&top);
    if (first > 0) { --first; top -= layout[first].height; }
    std::array<std::size_t, kVisibleRows> wanted{};
    std::array<std::int32_t, kVisibleRows> positions{};
    std::array<Row *, kVisibleRows> assigned{};
    std::array<bool, kVisibleRows> used{};
    std::size_t wanted_count = 0;
    for (std::size_t i = first; i < layout_count && wanted_count < rows.size(); ++i) {
        if (top > scroll_y + chat_height + 96) break;
        wanted[wanted_count] = i;
        positions[wanted_count++] = top;
        top += layout[i].height;
    }
    // Reserve objects already displaying these exact records. Crossing a row
    // boundary should recycle one object, not rebind every visible label.
    for (std::size_t i = 0; i < wanted_count; ++i) {
        for (std::size_t j = 0; j < rows.size(); ++j) {
            if (!used[j] && rows[j].sequence == layout[wanted[i]].sequence) {
                assigned[i] = &rows[j]; used[j] = true; break;
            }
        }
    }
    for (std::size_t i = 0; i < wanted_count; ++i) {
        if (assigned[i]) continue;
        for (std::size_t j = 0; j < rows.size(); ++j) {
            if (!used[j]) { assigned[i] = &rows[j]; used[j] = true; break; }
        }
    }
    for (std::size_t i = 0; i < wanted_count; ++i) {
        auto &row = *assigned[i];
        const auto &item = layout[wanted[i]];
        const auto *message = store->at(wanted[i]);
        if (!message) continue;
        if (lv_obj_has_flag(row.button, LV_OBJ_FLAG_HIDDEN))
            lv_obj_clear_flag(row.button, LV_OBJ_FLAG_HIDDEN);
        if (row.top != positions[i]) {
            lv_obj_set_pos(row.button, 0, positions[i]); row.top = positions[i];
        }
        if (row.height != item.height) {
            lv_obj_set_height(row.button, item.height); row.height = item.height;
            row.sprite_attempted = false;
            row.author_attempted = false;
        }
        const auto body_y = 16 + item.author_height;
        if (row.body_y != body_y) {
            lv_obj_set_y(row.body, body_y); row.body_y = body_y;
            row.sprite_attempted = false;
        }
        lv_obj_set_height(row.author, item.author_height);
        const auto body_height = std::max<int>(27, item.height - body_y - 12);
        if (row.body_height != body_height) {
            lv_obj_set_height(row.body, body_height); row.body_height = body_height;
            row.sprite_attempted = false;
        }
        if (row.sequence != message->sequence || row.deleted != message->deleted ||
            row.font_generation != font_generation || row.revision != item.revision) {
            row.sequence = message->sequence;
            row.sprite_attempted = false;
            row.font_generation = font_generation;
            row.revision = item.revision;
            row.author_attempted = false;
            const auto *name = display_author(*message);
            lv_obj_set_style_text_font(row.author, author_font(name), 0);
            lv_label_set_text(row.author, name);
            if (row.body_text) {
                body_text(*message, item.emotes, row.body_text);
            }
            row.text_hint.line_start = -1;
            lv_obj_invalidate(row.body);
            label_updates += 2;
            row.deleted = message->deleted;
        }
        render_emotes(row, message->deleted ? nullptr : &item);
    }
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (used[i]) continue;
        rows[i].sequence = 0;
        if (!lv_obj_has_flag(rows[i].button, LV_OBJ_FLAG_HIDDEN))
            lv_obj_add_flag(rows[i].button, LV_OBJ_FLAG_HIDDEN);
        render_emotes(rows[i], nullptr);
    }
    prepare_visible_sprites();
}

void close_overlay() {
    if (overlay) { lv_obj_del(overlay); overlay = nullptr; }
    login_visible = false;
    selection_visible = false;
    account_visible = false;
    close_requested = false;
    connect_requested = false;
    upgrade_requested = false;
    moderation_navigation = ModerationNavigation::None;
    moderation_view = ModerationView::None;
    confirmation_button = nullptr;
    modal_pressed_button = nullptr;
    ++modal_generation;
}
void close_event(lv_event_t *event) {
    // Never delete a button's ancestor from inside that button's own event.
    // Consume this one request; close safely on the next UI-owner iteration.
    close_requested = true;
    lv_obj_add_state(lv_event_get_target(event), LV_STATE_DISABLED);
}

lv_obj_t *new_overlay() {
    close_overlay();
    overlay = plain(screen, 0, 0, width, height, kInk);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_move_foreground(overlay);
    modal_ready_tick = lv_tick_get() + 450;
    return overlay;
}

void consume_contact() {
    lv_indev_reset(nullptr, nullptr);
    for (auto *indev = lv_indev_get_next(nullptr); indev; indev = lv_indev_get_next(indev))
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER) lv_indev_wait_release(indev);
    pressed_sequence = 0;
    gesture_cancelled = true;
    modal_pressed_button = nullptr;
}

bool read_only() { return read_only_profile || moderation_caps.read_only; }

void invalidate_profile_actions() {
    ++profile_generation;
    if (!profile_generation) ++profile_generation;
    ++modal_generation;
    modal_pressed_button = nullptr;
    moderation_navigation = ModerationNavigation::None;
    if (upgrade_requested) connect_requested = false;
    upgrade_requested = false;
    if (confirmation_button) lv_obj_add_state(confirmation_button, LV_STATE_DISABLED);
    moderation_refresh = true;
    account_refresh = true;
}

void reset_chat_history() {
    if (!store) return;
    consume_contact();
    close_overlay();
    clear_sprites();
    clear_author_sprites();
    store->~Store();
    new (store) Store();
    for (std::size_t i = 0; i < layout_count; ++i) {
        if (layout[i].emotes) heap_caps_free(layout[i].emotes);
        layout[i].emotes = nullptr;
        if (layout[i].moderation) heap_caps_free(layout[i].moderation);
        layout[i].moderation = nullptr;
    }
    emote_rows = 0;
    layout_count = 0;
    total_height = 0;
    scroll_y = 0;
    history_expired = false;
    for (auto &row : rows) row.sequence = 0;
    layout_dirty = true;
    follow_dirty = true;
}

bool selected_deleted() { return store && store->selected() && store->selected()->deleted; }

const char *moderation_block_reason_for(const forearm::ModerationRequest &target,
                                      bool metadata_verified, forearm::ModerationAction action,
                                      bool message_deleted, std::uint32_t target_profile) {
    if (read_only()) return "Read-only profile. Moderation is disabled.";
    if (target_profile != profile_generation) return "Account or channel changed. Select again.";
    if (moderation_pending) return "Another action is waiting for Twitch.";
    if (demo) return "Demo messages cannot be moderated.";
    if (!moderation_callback) return "Moderation is not connected.";
    if (!moderation_caps.authenticated) return "Connect Twitch to moderate.";
    if (!moderation_caps.online) return "Offline. Actions are never queued.";
    if (!moderation_caps.role_verified) return moderation_caps.reason[0] ? moderation_caps.reason : "Channel moderator role is not verified.";
    if (!metadata_verified) return "Message identity is not verified.";
    if (target.session_generation != moderation_caps.generation ||
        std::strcmp(target.channel_id, moderation_caps.broadcaster_id))
        return "Account or channel changed. Select again.";
    if (std::strcmp(target.channel_id, target.source_channel_id) ||
        std::strcmp(target.message_id, target.source_message_id))
        return "Shared-chat messages are read only.";
    if (target.protected_target ||
        !std::strcmp(target.user_id, moderation_caps.broadcaster_id) ||
        !std::strcmp(target.user_id, moderation_caps.moderator_id))
        return "This account is protected from moderation.";
    if (action == forearm::ModerationAction::DeleteMessage && message_deleted) return "This message was already removed.";
    if (action == forearm::ModerationAction::DeleteMessage ? !moderation_caps.can_delete : !moderation_caps.can_ban)
        return moderation_caps.reason[0] ? moderation_caps.reason : "Reconnect Twitch for moderation permission.";
    auto candidate = target;
    candidate.action = action;
    candidate.confirmed = true;
    candidate.request_id = 1;
    candidate.message_deleted = action == forearm::ModerationAction::DeleteMessage && message_deleted;
    candidate.timeout_seconds = action == forearm::ModerationAction::Timeout ?
                                (target.timeout_seconds ? target.timeout_seconds : 60) : 0;
    const auto denial = forearm::moderation_validate(candidate, moderation_caps, now_ms());
    if (denial != forearm::ModerationDenial::None) return forearm::moderation_denial_text(denial);
    return nullptr;
}

const char *moderation_block_reason(forearm::ModerationAction action) {
    return moderation_block_reason_for(moderation_target, selected_metadata_verified, action,
                                       selected_deleted(), target_profile_generation);
}

bool acknowledged_user_restriction() {
    return !moderation_pending && pending_moderation.request_id &&
           moderation_result.request_id == pending_moderation.request_id &&
           moderation_result.session_generation == pending_moderation.session_generation &&
           moderation_result.action == pending_moderation.action &&
           moderation_result.state == forearm::ModerationState::Succeeded &&
           (pending_moderation.action == forearm::ModerationAction::Ban ||
            pending_moderation.action == forearm::ModerationAction::Timeout);
}

void moderation_button_event(lv_event_t *event) {
    auto *target = lv_event_get_target(event);
    const auto code = lv_event_get_code(event);
    auto *indev = lv_indev_get_act();
    if (code == LV_EVENT_PRESSED) {
        modal_pressed_button = nullptr;
        if (static_cast<std::int32_t>(lv_tick_get() - modal_ready_tick) < 0 ||
            lv_obj_has_state(target, LV_STATE_DISABLED)) return;
        modal_pressed_button = target;
        modal_pressed_generation = modal_generation;
        modal_pressed_tick = lv_tick_get();
        modal_dragged = false;
        if (indev) lv_indev_get_point(indev, &modal_pressed_point);
    } else if (code == LV_EVENT_PRESSING && indev && modal_pressed_button == target) {
        lv_point_t point{};
        lv_indev_get_point(indev, &point);
        if (std::abs(point.x - modal_pressed_point.x) > 10 || std::abs(point.y - modal_pressed_point.y) > 10)
            modal_dragged = true;
    } else if (code == LV_EVENT_PRESS_LOST) {
        modal_pressed_button = nullptr;
    } else if (code == LV_EVENT_SHORT_CLICKED && modal_pressed_button == target &&
               modal_pressed_generation == modal_generation && !modal_dragged &&
               lv_tick_elaps(modal_pressed_tick) < 650 && moderation_navigation == ModerationNavigation::None &&
               !lv_obj_has_state(target, LV_STATE_DISABLED)) {
        moderation_navigation = static_cast<ModerationNavigation>(reinterpret_cast<std::uintptr_t>(lv_event_get_user_data(event)));
        modal_pressed_button = nullptr;
        lv_obj_add_state(target, LV_STATE_DISABLED);
    }
}

lv_obj_t *moderation_button(lv_obj_t *panel, const char *caption, int x, int y, int w,
                            ModerationNavigation navigation, bool enabled = true) {
    auto *obj = button(panel, caption, x, y, w, 56, nullptr);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(obj, moderation_button_event, LV_EVENT_ALL,
                        reinterpret_cast<void *>(static_cast<std::uintptr_t>(navigation)));
    if (!enabled) lv_obj_add_state(obj, LV_STATE_DISABLED);
    return obj;
}

void selection_heading(lv_obj_t *panel, const char *title, const char *author_name = selected_author,
                       const char *channel_name = selected_channel) {
    label(panel, title, 14, 14, width - 28, &lv_font_montserrat_20, kViolet);
    auto *author = label(panel, author_name, 14, 45, width - 28, &message_font, kText);
    lv_label_set_long_mode(author, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_height(author, 32);
    auto *name = label(panel, channel_name, 14, 83, width - 28, &lv_font_montserrat_20, kMuted);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_height(name, 28);
}

void show_selection_contents() {
    const auto *selected = store ? store->selected() : nullptr;
    if (!selected) return;
    auto *panel = new_overlay();
    selection_visible = true;
    moderation_view = ModerationView::Selection;
    selection_heading(panel, "SELECTED MESSAGE");
    auto *excerpt = label(panel, selected->deleted ? "Message removed" : selected->text.c_str(), 14, 120, width - 28, &message_font, kText);
    lv_label_set_long_mode(excerpt, LV_LABEL_LONG_DOT);
    lv_obj_set_height(excerpt, 98);
    const auto *del_reason = moderation_block_reason(forearm::ModerationAction::DeleteMessage);
    const auto *ban_reason = moderation_block_reason(forearm::ModerationAction::Ban);
    const auto *reason = ban_reason ? ban_reason : del_reason;
    auto *note = label(panel, reason ? reason : "Choose an action, then confirm.", 14, 226, width - 28, &lv_font_montserrat_20, reason ? kAmber : kMuted);
    lv_label_set_long_mode(note, LV_LABEL_LONG_DOT);
    lv_obj_set_height(note, 48);
    moderation_button(panel, "Delete", 14, 282, (width - 38) / 2, ModerationNavigation::Delete, !del_reason);
    moderation_button(panel, "Timeout", 24 + (width - 38) / 2, 282, (width - 38) / 2, ModerationNavigation::Timeout, !ban_reason);
    moderation_button(panel, "Ban", 14, 346, width - 28, ModerationNavigation::Ban, !ban_reason);
    button(panel, LV_SYMBOL_LEFT "  Back to chat", 14, height - 70, width - 28, 56, close_event);
    update_status();
}

void show_selection(std::uint64_t sequence) {
    // Removed text stays scrubbed, but the immutable retained author/source
    // identity remains useful for Ban/Timeout. Delete alone stays disabled.
    if (!store || !store->select(sequence, true)) return;
    synchronize_anchor();
    const auto *selected = store->selected();
    moderation_target = {};
    target_profile_generation = profile_generation;
    moderation_target.session_generation = moderation_caps.generation;
    std::snprintf(moderation_target.channel_id, sizeof(moderation_target.channel_id), "%s", selected->channel_id.c_str());
    std::snprintf(moderation_target.message_id, sizeof(moderation_target.message_id), "%s", selected->message_id.c_str());
    std::snprintf(moderation_target.user_id, sizeof(moderation_target.user_id), "%s", selected->user_id.c_str());
    moderation_target.received_at_ms = selected->received_at_ms;
    std::snprintf(selected_author, sizeof(selected_author), "%s", display_author(*selected));
    std::snprintf(selected_channel, sizeof(selected_channel), "%s", channel);
    selected_metadata_verified = false;
    for (std::size_t i = 0; i < kHistory; ++i) {
        const auto &item = layout[i];
        if (item.sequence != sequence || !item.moderation) continue;
        selected_metadata_verified = true;
        std::memcpy(moderation_target.source_channel_id, item.moderation->source_channel_id, sizeof(moderation_target.source_channel_id));
        std::memcpy(moderation_target.source_message_id, item.moderation->source_message_id, sizeof(moderation_target.source_message_id));
        moderation_target.protected_target = item.moderation->protected_target;
        break;
    }
    show_selection_contents();
}

void show_timeout_choices() {
    auto *panel = new_overlay();
    selection_visible = true;
    moderation_view = ModerationView::Timeout;
    selection_heading(panel, "TIMEOUT DURATION");
    label(panel, "How long should this user\nbe unable to chat?", 14, 130, width - 28, &message_font, kText);
    const auto *reason = moderation_block_reason(forearm::ModerationAction::Timeout);
    if (reason) label(panel, reason, 14, 212, width - 28, &lv_font_montserrat_20, kAmber);
    const int gap = 8, w = (width - 28 - 2 * gap) / 3;
    moderation_button(panel, "30 sec", 14, 282, w, ModerationNavigation::Seconds30, !reason);
    moderation_button(panel, "60 sec", 14 + w + gap, 282, w, ModerationNavigation::Seconds60, !reason);
    moderation_button(panel, "10 min", 14 + 2 * (w + gap), 282, w, ModerationNavigation::Seconds600, !reason);
    moderation_button(panel, LV_SYMBOL_LEFT "  Back", 14, height - 70, width - 28, ModerationNavigation::Selection);
}

const char *action_caption(forearm::ModerationAction action) {
    if (action == forearm::ModerationAction::DeleteMessage) return "Delete message";
    if (action == forearm::ModerationAction::Timeout) return "Timeout user";
    if (action == forearm::ModerationAction::Ban) return "Ban user";
    if (action == forearm::ModerationAction::Unban) return "Restore chat access";
    return "Unknown action";
}

void show_moderation_confirmation() {
    auto *panel = new_overlay();
    selection_visible = true;
    moderation_view = ModerationView::Confirm;
    selection_heading(panel, "CONFIRM ACTION");
    char effect[128];
    if (moderation_target.action == forearm::ModerationAction::DeleteMessage)
        std::snprintf(effect, sizeof(effect), "Remove this exact message from chat.");
    else if (moderation_target.action == forearm::ModerationAction::Timeout)
        std::snprintf(effect, sizeof(effect), "Prevent this user chatting\nfor %lu seconds.", static_cast<unsigned long>(moderation_target.timeout_seconds));
    else if (moderation_target.action == forearm::ModerationAction::Unban)
        std::snprintf(effect, sizeof(effect), "Remove this user's active\nban or timeout.");
    else std::snprintf(effect, sizeof(effect), "Ban this user from this\nchannel indefinitely.");
    label(panel, effect, 14, 130, width - 28, &message_font, kText);
    confirmation_button = moderation_button(panel, action_caption(moderation_target.action), 14, 220, width - 28, ModerationNavigation::Submit, false);
    const auto *reason = moderation_block_reason(moderation_target.action);
    char target_detail[192];
    std::snprintf(target_detail, sizeof(target_detail), "User ID %s\nApplies when Twitch accepts.", moderation_target.user_id);
    const auto *selected = store ? store->selected() : nullptr;
    const char *detail = reason ? reason : moderation_target.action == forearm::ModerationAction::DeleteMessage && selected ?
                         selected->text.c_str() : target_detail;
    auto *note = label(panel, detail, 14, 296, width - 28, &lv_font_montserrat_20, reason ? kAmber : kMuted);
    lv_label_set_long_mode(note, LV_LABEL_LONG_DOT);
    lv_obj_set_height(note, 76);
    label(panel, "No automatic retry.", 14, 382, width - 28, &lv_font_montserrat_16, kMuted);
    moderation_button(panel, "Cancel", 14, height - 70, width - 28,
                      moderation_target.action == forearm::ModerationAction::Unban ?
                      ModerationNavigation::Result : ModerationNavigation::Selection);
}

void show_pending_moderation() {
    auto *panel = new_overlay();
    moderation_view = ModerationView::Pending;
    selection_heading(panel, "WAITING FOR TWITCH", pending_author, pending_channel);
    label(panel, action_caption(pending_moderation.action), 14, 140, width - 28, &message_font, kText);
    label(panel, "Request sent.\nWaiting for acknowledgement.\n\nOther actions stay locked\nuntil the result arrives.", 14, 194, width - 28, &lv_font_montserrat_20, kMuted);
    button(panel, "Back to chat", 14, height - 70, width - 28, 56, close_event);
}

void show_moderation_result() {
    auto *panel = new_overlay();
    moderation_view = ModerationView::Result;
    const auto state = moderation_result.state;
    const char *title = state == forearm::ModerationState::Succeeded ? "TWITCH CONFIRMED" :
                        state == forearm::ModerationState::OutcomeUnknown ? "OUTCOME UNKNOWN" :
                        state == forearm::ModerationState::RateLimited ? "RATE LIMITED" :
                        state == forearm::ModerationState::Denied ? "NOT ALLOWED" : "ACTION NOT COMPLETED";
    selection_heading(panel, title, pending_author, pending_channel);
    label(panel, action_caption(pending_moderation.action), 14, 132, width - 28, &message_font, kText);
    auto *detail = label(panel, moderation_result.detail, 14, 185, width - 28, &message_font,
                         state == forearm::ModerationState::Succeeded ? kText : kAmber);
    lv_label_set_long_mode(detail, LV_LABEL_LONG_DOT);
    lv_obj_set_height(detail, 112);
    const bool undo = acknowledged_user_restriction();
    const auto *undo_reason = undo ? moderation_block_reason_for(
        pending_moderation, pending_metadata_verified, forearm::ModerationAction::Unban,
        false, pending_profile_generation) : nullptr;
    const char *note = undo_reason ? undo_reason : state == forearm::ModerationState::OutcomeUnknown ? "Check Twitch before trying again. This app will not retry." :
                       state == forearm::ModerationState::Succeeded ? "Acknowledged by Twitch." : "Nothing will retry automatically.";
    auto *note_label = label(panel, note, 14, undo ? 302 : 318, width - 28, &lv_font_montserrat_20, undo_reason ? kAmber : kMuted);
    if (undo) {
        lv_label_set_long_mode(note_label, LV_LABEL_LONG_DOT);
        lv_obj_set_height(note_label, 40);
        moderation_button(panel, pending_moderation.action == forearm::ModerationAction::Timeout ?
                          "Undo timeout" : "Unban user", 14, 346, width - 28,
                          ModerationNavigation::Undo, !undo_reason);
    }
    button(panel, "Back to chat", 14, height - 70, width - 28, 56, close_event);
}

void process_moderation_navigation() {
    const auto navigation = moderation_navigation;
    moderation_navigation = ModerationNavigation::None;
    consume_contact();
    if (navigation == ModerationNavigation::Result) { show_moderation_result(); return; }
    if (navigation == ModerationNavigation::Undo) {
        if (!acknowledged_user_restriction()) return;
        if (moderation_block_reason_for(pending_moderation, pending_metadata_verified,
                                        forearm::ModerationAction::Unban, false, pending_profile_generation)) {
            show_moderation_result();
            return;
        }
        // Use only the acknowledged immutable request. The original chat line
        // may have been evicted/cleared, and another viewer may now be selected.
        moderation_target = pending_moderation;
        target_profile_generation = pending_profile_generation;
        moderation_target.action = forearm::ModerationAction::Unban;
        moderation_target.timeout_seconds = 0;
        moderation_target.request_id = 0;
        moderation_target.confirmed = false;
        moderation_target.message_deleted = false;
        selected_metadata_verified = pending_metadata_verified;
        std::snprintf(selected_author, sizeof(selected_author), "%s", pending_author);
        std::snprintf(selected_channel, sizeof(selected_channel), "%s", pending_channel);
        show_moderation_confirmation();
        return;
    }
    if (navigation == ModerationNavigation::Selection) { show_selection_contents(); return; }
    if (navigation == ModerationNavigation::Timeout) { show_timeout_choices(); return; }
    if (navigation == ModerationNavigation::Submit) {
        const auto *reason = moderation_block_reason(moderation_target.action);
        if (reason) { moderation_refresh = true; return; }
        if (!next_moderation_request) { app_ui_set_status("Restart before sending more actions."); return; }
        moderation_target.request_id = next_moderation_request++;
        moderation_target.confirmed = true;
        moderation_target.message_deleted = moderation_target.action == forearm::ModerationAction::DeleteMessage && selected_deleted();
        pending_moderation = moderation_target;
        pending_profile_generation = target_profile_generation;
        pending_metadata_verified = selected_metadata_verified;
        std::snprintf(pending_author, sizeof(pending_author), "%s", selected_author);
        std::snprintf(pending_channel, sizeof(pending_channel), "%s", selected_channel);
        moderation_pending = true;
        moderation_result = {};
        moderation_result.request_id = pending_moderation.request_id;
        moderation_result.session_generation = pending_moderation.session_generation;
        moderation_result.action = pending_moderation.action;
        moderation_result.state = forearm::ModerationState::Pending;
        ++moderation_submitted;
        show_pending_moderation();
        if (!moderation_callback(pending_moderation) && moderation_pending) {
            forearm::ModerationResult result{};
            result.request_id = pending_moderation.request_id;
            result.session_generation = pending_moderation.session_generation;
            result.action = pending_moderation.action;
            result.state = forearm::ModerationState::Denied;
            std::snprintf(result.detail, sizeof(result.detail), "Request was not queued. No action was sent.");
            app_ui_moderation_result(result);
        }
        return;
    }
    moderation_target.confirmed = false;
    moderation_target.timeout_seconds = 0;
    if (navigation == ModerationNavigation::Delete) moderation_target.action = forearm::ModerationAction::DeleteMessage;
    else if (navigation == ModerationNavigation::Ban) moderation_target.action = forearm::ModerationAction::Ban;
    else if (navigation == ModerationNavigation::Seconds30 || navigation == ModerationNavigation::Seconds60 ||
             navigation == ModerationNavigation::Seconds600) {
        moderation_target.action = forearm::ModerationAction::Timeout;
        moderation_target.timeout_seconds = navigation == ModerationNavigation::Seconds30 ? 30 :
                                            navigation == ModerationNavigation::Seconds60 ? 60 : 600;
    } else return;
    show_moderation_confirmation();
}

void row_event(lv_event_t *event) {
    auto *row = static_cast<Row *>(lv_event_get_user_data(event));
    const auto code = lv_event_get_code(event);
    auto *indev = lv_indev_get_act();
    if (code == LV_EVENT_PRESSED) {
        app_ui_pause();
        pressed_sequence = row->sequence;
        pressed_tick = lv_tick_get();
        gesture_cancelled = false;
        if (indev) lv_indev_get_point(indev, &pressed_point);
    } else if (code == LV_EVENT_PRESSING && indev) {
        lv_point_t point{};
        lv_indev_get_point(indev, &point);
        if (std::abs(point.x - pressed_point.x) > 12 || std::abs(point.y - pressed_point.y) > 12) {
            gesture_cancelled = true;
            // A drag is no longer a candidate tap. Keep LVGL's input capture,
            // but stop repainting an opaque pressed background under the body.
            lv_obj_clear_state(row->button, LV_STATE_PRESSED);
        }
    } else if (code == LV_EVENT_SHORT_CLICKED) {
        if (!gesture_cancelled && row->sequence == pressed_sequence &&
            lv_tick_elaps(pressed_tick) < 650 && !lv_obj_is_scrolling(chat)) {
            ++taps;
            show_selection(pressed_sequence);
        }
    }
}

void scroll_event(lv_event_t *event) {
    if (update_guard || !store) return;
    const auto code = lv_event_get_code(event);
    const auto *indev = lv_indev_get_act();
    // LVGL also emits SCROLL_BEGIN for layout correction and for our explicit
    // scroll_to_y. Only an actual input device dragging this list may pause it.
    const bool user_scroll = indev && lv_indev_get_scroll_obj(indev) == chat;
    if (code == LV_EVENT_SCROLL_BEGIN && user_scroll) {
        gesture_cancelled = true;
        for (auto &row : rows)
            if (lv_obj_has_state(row.button, LV_STATE_PRESSED))
                lv_obj_clear_state(row.button, LV_STATE_PRESSED);
        app_ui_pause();
        ++scrolls;
    }
    if (code == LV_EVENT_SCROLL || code == LV_EVENT_SCROLL_END) {
        if (!store->paused()) {
            // Keep FOLLOW sticky if LVGL readjusts content after a layout pass.
            // A fresh row touch/real drag is the only route back to PAUSED.
            if (!user_scroll) follow_dirty = true;
            return;
        }
        scroll_y = std::clamp<std::int32_t>(lv_obj_get_scroll_y(chat), 0, max_scroll());
        synchronize_anchor();
        render_rows();
    }
}

void follow_event(lv_event_t *) {
    if (!store) return;
    if (store->paused()) app_ui_go_live(); else app_ui_pause();
}

void connect_event(lv_event_t *event) {
    connect_requested = true;
    close_requested = true;
    lv_obj_add_state(lv_event_get_target(event), LV_STATE_DISABLED);
}

void upgrade_event(lv_event_t *event) {
    if (read_only()) return;
    connect_event(event);
    upgrade_requested = true;
}

void account_event(lv_event_t *) {
    auto *panel = new_overlay();
    account_visible = true;
    label(panel, "FOREARM CHAT", 14, 14, width - 28, &lv_font_montserrat_20, kViolet);
    auto *channel_name = label(panel, channel, 14, 51, width - 28, &lv_font_montserrat_28, kText);
    lv_label_set_long_mode(channel_name, LV_LABEL_LONG_DOT);
    lv_obj_set_height(channel_name, 36);
    auto *account = label(panel, authenticated ? account_name : "Not connected to Twitch", 14, 98,
                          width - 28, &lv_font_montserrat_20, kText);
    lv_label_set_long_mode(account, LV_LABEL_LONG_DOT);
    lv_obj_set_height(account, 28);
    label(panel, read_only() ? "READ ONLY" : demo ? "Demo chat" : store && store->paused() ? "Twitch chat / paused" : "Twitch chat / live",
          14, 139, width - 28, &lv_font_montserrat_20, read_only() ? kAmber : kViolet);
    auto *network = label(panel, network_status, 14, 181, width - 28, &lv_font_montserrat_20, kMuted);
    lv_label_set_long_mode(network, LV_LABEL_LONG_DOT);
    lv_obj_set_height(network, 60);
    auto *detail = label(panel, read_only() ? "Chat viewing only.\nModeration stays disabled." : history_expired ? "Older history expired." : transient_status[0] ? transient_status :
                         authenticated ? "Standalone over Wi-Fi." : "Authorize on your phone.",
                         14, 251, width - 28, &lv_font_montserrat_20, history_expired ? kAmber : kMuted);
    lv_label_set_long_mode(detail, LV_LABEL_LONG_DOT);
    lv_obj_set_height(detail, 60);
    const bool upgrade = !read_only() && authenticated && (!moderation_caps.can_delete || !moderation_caps.can_ban);
    button(panel, upgrade ? "Enable moderation" : authenticated ? "Reconnect Twitch" : "Connect Twitch", 14, 326, width - 28, 56,
           upgrade ? upgrade_event : connect_event, 0x76509E);
    button(panel, LV_SYMBOL_LEFT "  Back", 14, height - 70, width - 28, 56, close_event);
}

void settings_event(lv_event_t *event) {
    const auto code = lv_event_get_code(event);
    auto *indev = lv_indev_get_act();
    if (code == LV_EVENT_PRESSED) {
        settings_contact = indev != nullptr;
        settings_dragged = false;
        settings_pressed_tick = lv_tick_get();
        if (indev) lv_indev_get_point(indev, &settings_pressed_point);
        lv_obj_set_style_bg_color(settings_visual, color(0x49315E), 0);
    } else if (code == LV_EVENT_PRESSING && indev) {
        lv_point_t point{};
        lv_indev_get_point(indev, &point);
        if (std::abs(point.x - settings_pressed_point.x) > 12 ||
            std::abs(point.y - settings_pressed_point.y) > 12) settings_dragged = true;
    } else if (code == LV_EVENT_PRESS_LOST || code == LV_EVENT_LONG_PRESSED) {
        settings_contact = false;
        settings_dragged = true;
        lv_obj_set_style_bg_color(settings_visual, color(kSurface), 0);
    } else if (code == LV_EVENT_RELEASED) {
        lv_obj_set_style_bg_color(settings_visual, color(kSurface), 0);
    } else if (code == LV_EVENT_SHORT_CLICKED) {
        if (settings_contact && !settings_dragged && lv_tick_elaps(settings_pressed_tick) < 650)
            settings_requested = true;
        settings_contact = false;
    }
}
} // namespace

void app_ui_init() {
    inline_font.get_glyph_dsc = emote_glyph;
    inline_font.get_glyph_bitmap = emote_bitmap;
    inline_font.fallback = &message_font;
    void *memory = heap_caps_malloc(sizeof(Store), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!memory) {
        ESP_LOGE(kTag, "Unable to allocate bounded chat history in PSRAM");
        return;
    }
    auto *layout_memory = static_cast<Layout *>(heap_caps_malloc(
        sizeof(Layout) * kHistory, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!layout_memory) {
        heap_caps_free(memory);
        ESP_LOGE(kTag, "Unable to allocate bounded chat layout in PSRAM");
        return;
    }
    for (std::size_t i = 0; i < kHistory; ++i) new (layout_memory + i) Layout();
    layout = layout_memory;
    store = new (memory) Store();
    width = lv_disp_get_hor_res(nullptr);
    height = lv_disp_get_ver_res(nullptr);
    chat_height = height;
    screen = lv_scr_act();
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen, color(kInk), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(screen, backdrop_draw_event, LV_EVENT_DRAW_PART_BEGIN, nullptr);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    chat = plain(screen, 0, 0, width, chat_height, kInk);
    lv_obj_set_style_bg_opa(chat, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(chat, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(chat, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_set_scroll_dir(chat, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(chat, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_style_width(chat, 3, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(chat, color(kViolet), LV_PART_SCROLLBAR);
    lv_obj_add_event_cb(chat, scroll_event, LV_EVENT_ALL, nullptr);
    content = plain(chat, 0, 0, width, chat_height, kInk);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    empty_hint = label(content, "Your chat will appear here.", 22, 110, width - 44,
                       &message_font, kMuted);
    lv_obj_set_style_text_align(empty_hint, LV_TEXT_ALIGN_CENTER, 0);
    for (auto &row : rows) {
        row.button = plain(content, 0, 0, width, 76, kInk);
        lv_obj_set_style_bg_opa(row.button, LV_OPA_TRANSP, 0);
        lv_obj_add_flag(row.button, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(row.button, color(0x1D1629), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(row.button, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_border_color(row.button, color(0x211C2A), 0);
        lv_obj_set_style_border_width(row.button, 1, 0);
        lv_obj_set_style_border_side(row.button, LV_BORDER_SIDE_BOTTOM, 0);
        row.author = label(row.button, "", kSide, 8, kAuthorWidth, &message_font, kViolet);
        lv_obj_add_event_cb(row.author, author_draw_event,
                            static_cast<lv_event_code_t>(LV_EVENT_DRAW_MAIN | LV_EVENT_PREPROCESS), &row);
        lv_label_set_long_mode(row.author, LV_LABEL_LONG_WRAP);
        lv_label_set_recolor(row.author, false);
        lv_obj_set_height(row.author, 27);
        row.body_text = static_cast<char *>(heap_caps_calloc(kBodyBytes, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!row.body_text) ESP_LOGE(kTag, "Visible text buffer allocation failed");
        row.body = plain(row.button, kSide, 43, width - 2 * kSide, 30, kInk);
        lv_obj_set_style_bg_opa(row.body, LV_OPA_TRANSP, 0);
        lv_obj_clear_flag(row.body, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row.body, body_draw_event, LV_EVENT_DRAW_MAIN, &row);
        lv_obj_add_event_cb(row.button, row_event, LV_EVENT_ALL, &row);
        lv_obj_add_flag(row.button, LV_OBJ_FLAG_HIDDEN);
    }
    // The 52px circle keeps its visual position. A transparent 72px corner
    // target above chat owns the full contact, including the screen edges.
    settings_hit = lv_btn_create(screen);
    lv_obj_remove_style_all(settings_hit);
    lv_obj_set_pos(settings_hit, width - 72, 0);
    lv_obj_set_size(settings_hit, 72, 72);
    lv_obj_set_style_bg_opa(settings_hit, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(settings_hit, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ON_FOCUS |
                                  LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_add_flag(settings_hit, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    settings_visual = plain(settings_hit, 10, 10, 52, 52, kSurface);
    lv_obj_set_style_radius(settings_visual, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(settings_visual, LV_OPA_80, 0);
    lv_obj_clear_flag(settings_visual, LV_OBJ_FLAG_CLICKABLE);
    auto *settings_caption = label(settings_visual, LV_SYMBOL_SETTINGS, 0, 0, 40, &lv_font_montserrat_20, kText);
    lv_obj_clear_flag(settings_caption, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_align(settings_caption, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(settings_caption);
    lv_obj_add_event_cb(settings_hit, settings_event, LV_EVENT_ALL, nullptr);
    follow_button = button(screen, LV_SYMBOL_PAUSE, width - 62, height - 62, 52, 52, follow_event, kSurface);
    follow_label = lv_obj_get_child(follow_button, 0);
    fixture_badge = label(screen, "SYNTHETIC FIXTURE", 12, 10, 190, &lv_font_montserrat_16, kAmber);
    lv_obj_set_style_bg_color(fixture_badge, color(kSurface), 0);
    lv_obj_set_style_bg_opa(fixture_badge, LV_OPA_COVER, 0);
    lv_obj_add_flag(fixture_badge, LV_OBJ_FLAG_HIDDEN);
    next_demo_ms = now_ms() + 2800;
    app_ui_demo_burst(12);
    app_ui_tick();
    update_status();
    ESP_LOGI(kTag, "Native UI ready: %dx%d; history=%u; pool=%u; PSRAM bytes=%u",
             width, height, static_cast<unsigned>(kHistory),
             static_cast<unsigned>(kVisibleRows), static_cast<unsigned>(sizeof(Store)));
}

void app_ui_tick() {
    if (!store) return;
    ScopedDuration timer{tick_duration};
    if (settings_requested) {
        settings_requested = false;
        consume_contact();
        account_event(nullptr);
    }
    if (close_requested) {
        const bool start_login = connect_requested;
        const bool upgrade_login = upgrade_requested;
        close_requested = false;
        connect_requested = false;
        lv_indev_reset(nullptr, nullptr);
        for (auto *indev = lv_indev_get_next(nullptr); indev; indev = lv_indev_get_next(indev))
            if (lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER) lv_indev_wait_release(indev);
        pressed_sequence = 0;
        gesture_cancelled = true;
        close_overlay();
        store->clearSelection();
        // Back restores the same reading position and never enters FOLLOW.
        layout_dirty = true;
        follow_dirty = true;
        lv_obj_invalidate(chat);
        lv_obj_invalidate(screen);
        if (start_login) {
            if (upgrade_login && read_only()) {
                app_ui_set_status("Read-only profile. Moderation stays disabled.");
            } else if (upgrade_login && moderation_login_callback) {
                app_ui_set_status("Starting moderation authorization");
                moderation_login_callback();
            } else if (login_callback) {
                app_ui_set_status("Starting Twitch login");
                login_callback();
            } else app_ui_show_setup("Connect Twitch", "Configure Wi-Fi and a public Twitch Client ID over USB first.");
        }
    }
    if (account_refresh) {
        account_refresh = false;
        if (account_visible) {
            consume_contact();
            account_event(nullptr);
        }
    }
    if (moderation_result_ready) {
        moderation_result_ready = false;
        consume_contact();
        show_moderation_result();
    }
    if (moderation_navigation != ModerationNavigation::None) process_moderation_navigation();
    if (moderation_refresh) {
        moderation_refresh = false;
        const auto view = moderation_view;
        if (view == ModerationView::Selection || view == ModerationView::Timeout ||
            view == ModerationView::Confirm || view == ModerationView::Result) {
            consume_contact();
            if (view == ModerationView::Selection) show_selection_contents();
            else if (view == ModerationView::Timeout) show_timeout_choices();
            else if (view == ModerationView::Result) show_moderation_result();
            else show_moderation_confirmation();
        }
    }
    if (moderation_view == ModerationView::Confirm && confirmation_button &&
        static_cast<std::int32_t>(lv_tick_get() - modal_ready_tick) >= 0 &&
        !moderation_block_reason(moderation_target.action))
        lv_obj_clear_state(confirmation_button, LV_STATE_DISABLED);
    if (demo && !fixture_mode && now_ms() >= next_demo_ms) {
        next_demo_ms = now_ms() + 2800;
        app_ui_demo_burst(1);
    }
    const auto cache = emote_cache_stats();
    if (cache.ready != last_ready_emotes) {
        last_ready_emotes = cache.ready;
        // Compact only verified occurrences once when their assets arrive.
        // Preserve the paused message anchor while recalculating row heights.
        refresh_visible_emotes();
    }
    if (!layout_dirty && !follow_dirty) return;
    const auto layout_start = esp_timer_get_time();
    update_guard = true;
    lv_obj_set_height(content, std::max<int>(chat_height, total_height));
    if (!store->paused()) scroll_y = max_scroll();
    scroll_y = std::clamp<std::int32_t>(scroll_y, 0, max_scroll());
    lv_obj_update_layout(chat);
    lv_obj_scroll_to_y(chat, scroll_y, LV_ANIM_OFF);
    const auto first_layout_us = esp_timer_get_time() - layout_start;
    render_rows();
    const auto second_layout_start = esp_timer_get_time();
    // Virtual-row placement marks layout dirty. Settle that layout while the
    // programmatic guard remains held, then commit the requested position.
    lv_obj_update_layout(chat);
    lv_obj_scroll_to_y(chat, scroll_y, LV_ANIM_OFF);
    record(layout_duration, static_cast<std::uint32_t>(first_layout_us + esp_timer_get_time() - second_layout_start));
    update_guard = false;
    synchronize_anchor();
    layout_dirty = false;
    follow_dirty = false;
    update_status();
}

bool app_ui_append(const char *channel_id, const char *message_id,
                   const char *user_id, const char *display_name,
                   const char *text, std::uint64_t received_at_ms, const char *user_login) {
    if (!store || !channel_id || !message_id || !user_id || !display_name || !text) return false;
    const auto result = store->append({channel_id, message_id, user_id, display_name, text, received_at_ms,
                                      user_login ? user_login : ""});
    if (result == forearm::InsertResult::duplicate) return false;
    if (result != forearm::InsertResult::inserted) { ++rejected; return false; }
    ++appended;
    if (layout_count == kHistory) {
        const auto removed = layout[0];
        total_height -= removed.height;
        if (store->paused()) {
            if (store->anchorSequence() == removed.sequence) history_expired = true;
            scroll_y = std::max<std::int32_t>(0, scroll_y - removed.height);
        }
        if (removed.emotes) { heap_caps_free(removed.emotes); --emote_rows; }
        if (removed.moderation) heap_caps_free(removed.moderation);
        invalidate_sprite(removed.sequence);
        std::memmove(layout, layout + 1, (kHistory - 1) * sizeof(Layout));
        --layout_count;
    }
    const auto *message = store->at(store->size() - 1);
    auto &item = layout[layout_count++];
    item = {};
    item.sequence = message->sequence;
    item.height = measure_message(*message, nullptr, &item.author_height);
    total_height += layout[layout_count - 1].height;
    layout_dirty = true;
    return true;
}

bool app_ui_append_emotes(const char *channel_id, const char *message_id,
                         const char *user_id, const char *display_name,
                         const char *text, const AppUiEmote *emotes,
                         unsigned emote_count, std::uint64_t received_at_ms, const char *user_login) {
    if (!app_ui_append(channel_id, message_id, user_id, display_name, text, received_at_ms, user_login)) return false;
    app_ui_set_message_emotes(channel_id, message_id, emotes, emote_count);
    return true;
}

void app_ui_set_message_emotes(const char *channel_id, const char *message_id,
                              const AppUiEmote *emotes, unsigned emote_count) {
    if (!store || !channel_id || !message_id || !emotes || !emote_count) return;
    for (std::size_t i = 0; i < layout_count; ++i) {
        const auto *message = store->at(i);
        if (message->channel_id.view() != channel_id || message->message_id.view() != message_id || message->deleted)
            continue;
        // Copy only exact identifiers. Image decode is asynchronous and no
        // network-owned event pointers survive this call.
        void *memory = heap_caps_malloc(sizeof(EmoteRow), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!memory) return; // Original emote alt text remains fully readable.
        auto *meta = new (memory) EmoteRow();
        for (unsigned j = 0; j < std::min<unsigned>(emote_count, 8); ++j) {
            if (!emotes[j].id || !emotes[j].text || !emotes[j].position_valid || !emotes[j].byte_length) continue;
            const auto len = strnlen(emotes[j].id, 65);
            if (!len || len > 64) continue;
            const auto offset = emotes[j].byte_offset, bytes = emotes[j].byte_length;
            const auto end = static_cast<std::size_t>(offset) + bytes;
            const auto *raw = message->text.c_str();
            const auto raw_length = message->text.view().size();
            if (end > raw_length || strnlen(emotes[j].text, 2049) != bytes ||
                std::memcmp(raw + offset, emotes[j].text, bytes) ||
                (offset && (static_cast<unsigned char>(raw[offset]) & 0xC0) == 0x80) ||
                (end < raw_length && (static_cast<unsigned char>(raw[end]) & 0xC0) == 0x80)) continue;
            bool overlap = false;
            for (unsigned k = 0; k < meta->count; ++k)
                if (offset < meta->offsets[k] + meta->lengths[k] && meta->offsets[k] < end) overlap = true;
            if (overlap) continue;
            std::memcpy(meta->ids[meta->count], emotes[j].id, len + 1);
            meta->offsets[meta->count] = offset;
            meta->lengths[meta->count] = bytes;
            if (emote_cache_get(emotes[j].id)) meta->ready_mask |= 1U << meta->count;
            ++meta->count;
        }
        if (!meta->count) { heap_caps_free(meta); return; }
        std::int32_t old_top = 0;
        const auto old_first = first_visible(&old_top);
        const auto old_offset = scroll_y - old_top;
        if (layout[i].emotes) heap_caps_free(layout[i].emotes);
        else ++emote_rows;
        layout[i].emotes = meta;
        const auto old_height = layout[i].height;
        layout[i].height = measure_message(*message, meta, &layout[i].author_height);
        const auto delta = layout[i].height - old_height;
        total_height += delta;
        if (store->paused()) {
            if (i < old_first) scroll_y += delta;
            else if (i == old_first) scroll_y = old_top + std::min<std::int32_t>(old_offset, layout[i].height - 1);
        }
        ++layout[i].revision;
        invalidate_sprite(layout[i].sequence);
        if (emote_cache_init())
            for (unsigned j = 0; j < meta->count; ++j) emote_cache_request(meta->ids[j]);
        layout_dirty = true;
        return;
    }
}

void app_ui_delete_message(const char *channel_id, const char *message_id) {
    if (!store || !channel_id || !message_id) return;
    store->eraseMessage(channel_id, message_id);
    release_deleted_emotes();
    layout_dirty = true;
    if (selection_visible && selected_deleted()) moderation_refresh = true;
}
void app_ui_clear_user(const char *channel_id, const char *user_id) {
    if (!store || !channel_id || !user_id) return;
    store->clearUser(channel_id, user_id);
    release_deleted_emotes();
    layout_dirty = true;
    if (selection_visible && selected_deleted()) moderation_refresh = true;
}
void app_ui_clear_channel(const char *channel_id) {
    if (!store || !channel_id) return;
    store->clearChannel(channel_id);
    release_deleted_emotes();
    layout_dirty = true;
    if (selection_visible && selected_deleted()) moderation_refresh = true;
}

void app_ui_set_status(const char *status) {
    std::snprintf(transient_status, sizeof(transient_status), "%s", status ? status : "");
    update_status();
}
void app_ui_set_network(const char *status) {
    std::snprintf(network_status, sizeof(network_status), "%s", status ? status : "Offline");
    transient_status[0] = 0;
    update_status();
}
void app_ui_set_channel(const char *name) {
    if (!name) return;
    const auto *bare = name[0] == '#' ? name + 1 : name;
    if (!*bare) return;
    char next_channel[sizeof(channel)]{};
    std::snprintf(next_channel, sizeof(next_channel), "#%s", bare);
    for (auto &c : next_channel) {
        if (!c) break;
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    }
    if (!std::strcmp(channel, next_channel)) return;
    std::memcpy(channel, next_channel, sizeof(channel));
    invalidate_profile_actions();
    // A new room cannot inherit another room's scrollback or selected target.
    // Same-room reconnects return above and preserve the reading position.
    reset_chat_history();
}
void app_ui_set_read_only(bool enabled) {
    if (read_only_profile == enabled) return;
    read_only_profile = enabled;
    invalidate_profile_actions();
}
void app_ui_set_demo(bool enabled) {
    if (demo == enabled) return;
    demo = enabled;
    if (!enabled && store) {
        // Synthetic rows cannot be mistaken for messages from the real channel.
        reset_chat_history();
    }
    update_status();
}
void app_ui_set_authenticated(const char *name, const char *selected_channel) {
    authenticated = true;
    std::snprintf(account_name, sizeof(account_name), "%s", name ? name : "Connected");
    app_ui_set_demo(false);
    app_ui_set_channel(selected_channel ? selected_channel : name);
    app_ui_hide_login();
    app_ui_set_network("Connecting chat");
}

void app_ui_set_fixture_mode(bool enabled) {
    if (fixture_mode == enabled) return;
    // Force a transition through real mode, whose existing teardown frees all
    // message/emote/identity allocations and clears every selection snapshot.
    if (enabled) app_ui_set_demo(true);
    app_ui_set_demo(false);
    fixture_mode = enabled;
    if (enabled) {
        app_ui_set_demo(true);
        app_ui_set_status("Synthetic fixture / moderation disabled");
    }
    if (fixture_badge) {
        if (enabled) lv_obj_clear_flag(fixture_badge, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(fixture_badge, LV_OBJ_FLAG_HIDDEN);
    }
    layout_dirty = true;
}

void app_ui_set_login(const char *verification_url, const char *user_code) {
    if (!screen || !verification_url || !user_code) return;
    auto *panel = new_overlay();
    login_visible = true;
    label(panel, "Connect Twitch", 14, 13, width - 28, &lv_font_montserrat_28, kText);
    label(panel, "Authorize on your phone", 14, 54, width - 28, &lv_font_montserrat_20, kMuted);
#if LV_USE_QRCODE
    auto *qr = lv_qrcode_create(panel, 160, lv_color_black(), lv_color_white());
    lv_obj_set_pos(qr, (width - 160) / 2, 92);
    if (lv_qrcode_update(qr, verification_url, std::strlen(verification_url)) != LV_RES_OK)
        lv_obj_add_flag(qr, LV_OBJ_FLAG_HIDDEN);
#endif
    auto *code = label(panel, user_code, 14, 270, width - 28, &lv_font_montserrat_28, kViolet);
    lv_obj_set_style_text_align(code, LV_TEXT_ALIGN_CENTER, 0);
    auto *url = label(panel, verification_url, 14, 314, width - 28, &lv_font_montserrat_20, kText);
    lv_label_set_long_mode(url, LV_LABEL_LONG_DOT);
    lv_obj_set_height(url, 72);
    button(panel, "Back to chat", 14, height - 70, width - 28, 56, close_event);
}
void app_ui_hide_login() { if (login_visible) close_overlay(); }
void app_ui_show_setup(const char *title, const char *instructions) {
    if (!screen) return;
    auto *panel = new_overlay();
    auto *heading = label(panel, title ? title : "Setup", 14, 18, width - 28, &lv_font_montserrat_28, kText);
    lv_label_set_long_mode(heading, LV_LABEL_LONG_DOT);
    lv_obj_set_height(heading, 80);
    auto *body = label(panel, instructions ? instructions : "", 14, 110, width - 28, &lv_font_montserrat_24, kText);
    lv_label_set_long_mode(body, LV_LABEL_LONG_DOT);
    lv_obj_set_height(body, height - 210);
    button(panel, LV_SYMBOL_LEFT "  Back to chat", 14, height - 70, width - 28, 56, close_event);
}

void app_ui_pause() {
    if (!store) return;
    store->pause();
    synchronize_anchor();
    update_status();
}
void app_ui_go_live() {
    if (!store) return;
    close_overlay();
    // Stop a preceding fling and consume the old contact so its delayed events
    // cannot immediately pause FOLLOW or select the row under the smaller pill.
    lv_anim_del(chat, nullptr);
    lv_indev_reset(nullptr, nullptr);
    for (auto *indev = lv_indev_get_next(nullptr); indev; indev = lv_indev_get_next(indev))
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER) lv_indev_wait_release(indev);
    pressed_sequence = 0;
    gesture_cancelled = true;
    store->jumpLive();
    history_expired = false;
    transient_status[0] = 0;
    follow_dirty = true;
    update_status();
    // Returning live is immediate even when chat is completely idle.
    app_ui_tick();
}
void app_ui_scroll(std::int32_t delta_pixels) {
    if (!store) return;
    app_ui_pause();
    scroll_y = std::clamp<std::int32_t>(scroll_y + delta_pixels, 0, max_scroll());
    follow_dirty = true;
    ++scrolls;
    app_ui_tick();
}
void app_ui_demo_burst(unsigned count) {
    if (!store || !demo) return;
    static constexpr const char *names[] = {"mosslight", "pixelparty", "river_mod", "juniper", "orbit_fox", "sageandsea", "bytegarden", "cozy_static"};
    static constexpr const char *messages[] = {
        "The view from here is unreal.",
        "Tiny screen. Big adventure!",
        "Welcome in! Keep it kind, everyone.",
        "Can we take the long way back?",
        "Chat on your wrist is such a good idea.",
        "That golden light!",
        "Hello from the other side of the world.",
        "Enjoy the moment. We're right here."
    };
    count = std::min<unsigned>(count, 2000);
    for (unsigned i = 0; i < count; ++i) {
        char id[64], user[32];
        const auto serial = demo_serial++;
        const auto sample = static_cast<unsigned>(serial % 8);
        std::snprintf(id, sizeof(id), "demo-%llu", static_cast<unsigned long long>(serial));
        std::snprintf(user, sizeof(user), "viewer-%u", sample);
        app_ui_append("demo", id, user, names[sample], messages[sample], now_ms());
    }
}
AppUiStats app_ui_stats() {
    AppUiStats result{};
    if (!store) return result;
    result.paused = store->paused();
    result.demo = demo;
    result.fixture_mode = fixture_mode;
    result.authenticated = authenticated;
    result.read_only = read_only();
    result.login_visible = login_visible;
    result.selection_visible = selection_visible;
    result.unread = store->unread();
    result.retained = static_cast<std::uint32_t>(store->size());
    result.anchor_sequence = store->anchorSequence();
    std::int32_t top = 0;
    if (layout_count) first_visible(&top);
    result.anchor_offset = top - scroll_y;
    result.scroll_y = scroll_y;
    result.history_height = total_height;
    result.selected_sequence = store->selected() ? store->selected()->sequence : 0;
    result.appended = appended;
    result.rejected = rejected;
    result.taps = taps;
    result.scrolls = scrolls;
    result.emote_rows = emote_rows;
    result.tick_last_us = tick_duration.last;
    result.tick_max_us = tick_duration.maximum;
    result.layout_last_us = layout_duration.last;
    result.layout_max_us = layout_duration.maximum;
    result.render_last_us = render_duration.last;
    result.render_max_us = render_duration.maximum;
    result.measure_last_us = measure_duration.last;
    result.measure_max_us = measure_duration.maximum;
    result.label_updates = label_updates;
    result.text_draw_total_us = text_draw_total_us;
    result.text_draw_calls = text_draw_calls;
    result.text_draw_max_us = text_draw_max_us;
    result.sprite_cache_enabled = sprite_cache_enabled;
    result.backdrop_culling_enabled = backdrop_culling_enabled;
    result.backdrop_pixels_skipped = backdrop_pixels_skipped;
    result.backdrop_culled_draws = backdrop_culled_draws;
    result.backdrop_fallback_draws = backdrop_fallback_draws;
    result.sprite_cache_bytes = sprite_cache_bytes;
    result.sprite_cache_hits = sprite_cache_hits;
    result.sprite_cache_misses = sprite_cache_misses;
    result.sprite_cache_builds = sprite_cache_builds;
    result.sprite_cache_evictions = sprite_cache_evictions;
    result.sprite_cache_rejected = sprite_cache_rejected;
    result.sprite_cache_build_total_us = sprite_cache_build_total_us;
    result.sprite_cache_build_max_us = sprite_cache_build_max_us;
    result.author_cache_enabled = author_cache_enabled;
    result.author_cache_bytes = author_cache_bytes;
    result.author_cache_hits = author_cache_hits;
    result.author_cache_misses = author_cache_misses;
    result.author_cache_builds = author_cache_builds;
    result.author_cache_evictions = author_cache_evictions;
    result.author_cache_rejected = author_cache_rejected;
    result.author_cache_build_total_us = author_cache_build_total_us;
    result.author_cache_build_max_us = author_cache_build_max_us;
    result.backdrop_draw_total_us = backdrop_draw_duration.total;
    result.backdrop_draw_calls = backdrop_draw_duration.calls;
    result.backdrop_draw_max_us = backdrop_draw_duration.maximum;
    result.author_draw_total_us = author_draw_duration.total;
    result.author_draw_calls = author_draw_duration.calls;
    result.author_draw_max_us = author_draw_duration.maximum;
    result.author_style_total_us = author_style_duration.total;
    result.author_style_calls = author_style_duration.calls;
    result.author_style_max_us = author_style_duration.maximum;
    result.opaque_body_total_us = opaque_body_duration.total;
    result.opaque_body_calls = opaque_body_duration.calls;
    result.opaque_body_max_us = opaque_body_duration.maximum;
    result.author_blit_total_us = author_blit_duration.total;
    result.moderation_pending = moderation_pending;
    result.moderation_confirmation_visible = moderation_view == ModerationView::Confirm;
    result.moderation_request_id = pending_moderation.request_id;
    result.moderation_submitted = moderation_submitted;
    result.moderation_state = moderation_result.state;
    for (const auto &row : rows) {
        if (!row.sequence || lv_obj_has_flag(row.button, LV_OBJ_FLAG_HIDDEN)) continue;
        for (const auto *image : row.images)
            if (image && !lv_obj_has_flag(image, LV_OBJ_FLAG_HIDDEN)) ++result.visible_emote_images;
    }
    return result;
}
void app_ui_set_login_callback(void (*callback)()) { login_callback = callback; }

void app_ui_set_message_moderation_metadata(const char *channel_id, const char *message_id,
                                          const char *source_channel_id, const char *source_message_id,
                                          bool protected_target) {
    if (!store || !channel_id || !message_id) return;
    for (std::size_t i = 0; i < layout_count; ++i) {
        const auto *message = store->at(i);
        if (message->channel_id.view() != channel_id || message->message_id.view() != message_id) continue;
        if (layout[i].moderation) { heap_caps_free(layout[i].moderation); layout[i].moderation = nullptr; }
        forearm::FixedText<33> source_channel;
        forearm::FixedText<65> source_message;
        if (!source_channel_id || !source_message_id ||
            !source_channel.assignId(source_channel_id) || !source_message.assignId(source_message_id)) return;
        auto *meta = static_cast<ModerationMetadata *>(heap_caps_calloc(1, sizeof(ModerationMetadata), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!meta) return; // Missing metadata disables actions, never guesses.
        std::memcpy(meta->source_channel_id, source_channel.c_str(), source_channel.view().size() + 1);
        std::memcpy(meta->source_message_id, source_message.c_str(), source_message.view().size() + 1);
        meta->protected_target = protected_target;
        layout[i].moderation = meta;
        return;
    }
}

void app_ui_set_moderation_capabilities(const forearm::ModerationCapabilities &caps) {
    const bool profile_changed = moderation_caps.read_only != caps.read_only;
    const bool changed = moderation_caps.authenticated != caps.authenticated || moderation_caps.online != caps.online ||
        moderation_caps.role_verified != caps.role_verified || moderation_caps.can_delete != caps.can_delete ||
        moderation_caps.can_ban != caps.can_ban || profile_changed || moderation_caps.generation != caps.generation ||
        std::strcmp(moderation_caps.broadcaster_id, caps.broadcaster_id) ||
        std::strcmp(moderation_caps.moderator_id, caps.moderator_id) || std::strcmp(moderation_caps.reason, caps.reason);
    moderation_caps = caps;
    if (profile_changed) invalidate_profile_actions();
    if (changed) { moderation_refresh = true; account_refresh = true; }
}

void app_ui_set_moderation_callback(bool (*callback)(const forearm::ModerationRequest &)) {
    moderation_callback = callback;
    moderation_refresh = true;
}
void app_ui_set_moderation_login_callback(void (*callback)()) { moderation_login_callback = callback; }

void app_ui_moderation_result(const forearm::ModerationResult &result) {
    if (!moderation_pending || !pending_moderation.request_id || result.request_id != pending_moderation.request_id ||
        result.session_generation != pending_moderation.session_generation ||
        result.action != pending_moderation.action || result.state == forearm::ModerationState::Pending) return;
    moderation_result = result;
    moderation_pending = false;
    moderation_result_ready = true;
    if (result.state == forearm::ModerationState::Succeeded && result.action == forearm::ModerationAction::DeleteMessage && store) {
        store->eraseMessage(pending_moderation.channel_id, pending_moderation.message_id);
        release_deleted_emotes();
        layout_dirty = true;
    }
}

void app_ui_set_font_fallback(const _lv_font_t *fallback) {
    std::int32_t old_top = 0;
    const auto old_first = layout_count ? first_visible(&old_top) : 0;
    const auto old_offset = scroll_y - old_top;
    message_font.fallback = fallback;
    clear_sprites();
    clear_author_sprites();
    ++font_generation;
    if (!store) return;
    total_height = 0;
    std::int32_t new_top = 0;
    for (std::size_t i = 0; i < layout_count; ++i) {
        if (i == old_first) new_top = total_height;
        const auto *message = store->find(layout[i].sequence);
        if (message && !message->deleted)
            layout[i].height = measure_message(*message, layout[i].emotes, &layout[i].author_height);
        total_height += layout[i].height;
    }
    if (store->paused()) scroll_y = new_top + old_offset;
    layout_dirty = true;
}

void app_ui_reset_perf() {
    tick_duration = {};
    layout_duration = {};
    render_duration = {};
    measure_duration = {};
    backdrop_draw_duration = {};
    author_draw_duration = {};
    author_style_duration = {};
    opaque_body_duration = {};
    author_blit_duration = {};
    label_updates = 0;
    text_draw_total_us = 0;
    text_draw_calls = 0;
    text_draw_max_us = 0;
    sprite_cache_hits = 0;
    sprite_cache_misses = 0;
    sprite_cache_builds = 0;
    sprite_cache_evictions = 0;
    sprite_cache_rejected = 0;
    sprite_cache_build_total_us = 0;
    sprite_cache_build_max_us = 0;
    author_cache_hits = author_cache_misses = author_cache_builds = 0;
    author_cache_evictions = author_cache_rejected = 0;
    author_cache_build_total_us = author_cache_build_max_us = 0;
    backdrop_pixels_skipped = 0;
    backdrop_culled_draws = 0;
    backdrop_fallback_draws = 0;
}

void app_ui_set_sprite_cache(bool enabled) {
    if (sprite_cache_enabled == enabled) return;
    sprite_cache_enabled = enabled;
    clear_sprites();
    for (auto &row : rows) if (row.body) lv_obj_invalidate(row.body);
    layout_dirty = true;
}
void app_ui_set_author_cache(bool enabled) {
    if (author_cache_enabled == enabled) return;
    author_cache_enabled = enabled;
    clear_author_sprites();
    for (auto &row : rows) if (row.author) lv_obj_invalidate(row.author);
    layout_dirty = true;
}

void app_ui_set_backdrop_culling(bool enabled) {
    if (backdrop_culling_enabled == enabled) return;
    backdrop_culling_enabled = enabled;
    if (screen) lv_obj_invalidate(screen);
}
