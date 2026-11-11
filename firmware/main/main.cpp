#include <algorithm>
#include <array>
#include <cinttypes>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "app_ui.h"
#include "board_display.h"
#include "forearm_fonts.h"
#include "twitch_client.h"
#include "forearm/load_schedule.h"
#include "emote_cache.h"
#include "cJSON.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_heap_caps.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"

namespace {
uint16_t* snapshot;
uint32_t capture_generation;
uint32_t fixture_sequence;
SemaphoreHandle_t usb_output_mutex;
uint32_t usb_reply_errors;

int usb_log_vprintf(const char* format, va_list arguments) {
    // SDK workers share the USB endpoint with diagnostic JSON. Hold one lock
    // across the entire log call so a reconnect log cannot split a reply.
    xSemaphoreTake(usb_output_mutex,portMAX_DELAY);
    const int result=vprintf(format,arguments);
    xSemaphoreGive(usb_output_mutex);
    return result;
}
bool usb_reply_write(const char* bytes,size_t length,int64_t deadline_us) {
    size_t sent=0;
    while(sent<length && esp_timer_get_time()<deadline_us) {
        const auto chunk=std::min<size_t>(512,length-sent);
        const int written=usb_serial_jtag_write_bytes(bytes+sent,chunk,pdMS_TO_TICKS(20));
        if(written>0)sent+=static_cast<size_t>(written);
    }
    return sent==length;
}
lv_obj_t* touch_check_overlay;
lv_obj_t* touch_check_label;
lv_obj_t* touch_check_targets[5]{};
bool touch_check_was_paused;
constexpr uint16_t touch_target_x[]{26,294,294,26,160};
constexpr uint16_t touch_target_y[]{26,26,454,454,240};
struct TouchCheckPoint { uint16_t x,y; uint32_t squared_error; };
TouchCheckPoint touch_check_points[20]{};
uint32_t touch_check_count, touch_check_seen_sequence, touch_check_missed;
uint32_t touch_check_painted_count=UINT32_MAX,touch_check_painted_missed=UINT32_MAX;
void update_touch_check();
void touch_check_pressed(lv_event_t*) {
    auto* input=lv_indev_get_act();
    if(!input || !touch_check_overlay)return;
    const auto delivered=board_stats().touch_last_delivered_sequence;
    if(!delivered || delivered==touch_check_seen_sequence)return;
    touch_check_seen_sequence=delivered;
    if(touch_check_count<20) {
        lv_point_t point;lv_indev_get_point(input,&point);
        const auto target=touch_check_count%5;
        const int dx=point.x-touch_target_x[target],dy=point.y-touch_target_y[target];
        const uint32_t error=dx*dx+dy*dy;
        if(error<=32*32)touch_check_points[touch_check_count++]={static_cast<uint16_t>(point.x),static_cast<uint16_t>(point.y),error};
        else ++touch_check_missed;
        // Update existing visual properties before this handler's next refresh.
        // No object deletion/navigation occurs inside the input callback.
        update_touch_check();
    }
}

void stop_touch_check() {
    if(touch_check_overlay) {
        lv_obj_del(touch_check_overlay);
        lv_indev_reset(board_touch_indev(),nullptr);lv_indev_wait_release(board_touch_indev());
        if(!touch_check_was_paused)app_ui_go_live();
    }
    touch_check_overlay=nullptr;touch_check_label=nullptr;
}
void update_touch_check() {
    if(!touch_check_overlay)return;
    if(touch_check_painted_count==touch_check_count && touch_check_painted_missed==touch_check_missed)return;
    touch_check_painted_count=touch_check_count;touch_check_painted_missed=touch_check_missed;
    for(unsigned i=0;i<5;++i) {
        const bool active=touch_check_count<20 && i==touch_check_count%5;
        lv_obj_set_style_bg_color(touch_check_targets[i],lv_color_hex(active?0x9B71F5:0x24212E),0);
    }
    char text[120];
    snprintf(text,sizeof(text),"TOUCH CHECK\nTap the violet circle\n%lu / 20 - misses %lu\n%s",
             static_cast<unsigned long>(touch_check_count),static_cast<unsigned long>(touch_check_missed),touch_check_count==20?"Complete - USB report ready":touch_check_missed?"Aim for the circle centre":"Corners and centre");
    if(strcmp(lv_label_get_text(touch_check_label),text))lv_label_set_text(touch_check_label,text);
}
void start_touch_check() {
    stop_touch_check();board_inject_touch(0,0,false,false);board_reset_touch_latency();
    touch_check_was_paused=app_ui_stats().paused;
    app_ui_pause();
    lv_indev_reset(board_touch_indev(),nullptr);lv_indev_wait_release(board_touch_indev());
    touch_check_count=touch_check_missed=0;touch_check_seen_sequence=board_stats().touch_last_delivered_sequence;
    touch_check_painted_count=touch_check_painted_missed=UINT32_MAX;
    memset(touch_check_points,0,sizeof(touch_check_points));
    touch_check_overlay=lv_obj_create(lv_layer_top());lv_obj_set_pos(touch_check_overlay,0,0);
    lv_obj_set_size(touch_check_overlay,BOARD_WIDTH,BOARD_HEIGHT);
    lv_obj_clear_flag(touch_check_overlay,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(touch_check_overlay,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(touch_check_overlay,touch_check_pressed,LV_EVENT_PRESSED,nullptr);
    lv_obj_set_style_pad_all(touch_check_overlay,0,0);lv_obj_set_style_border_width(touch_check_overlay,0,0);
    lv_obj_set_style_radius(touch_check_overlay,0,0);lv_obj_set_style_bg_opa(touch_check_overlay,LV_OPA_COVER,0);
    lv_obj_set_style_bg_color(touch_check_overlay,lv_color_hex(0x09090B),0);
    for(unsigned i=0;i<5;++i) {
        auto* target=lv_obj_create(touch_check_overlay);touch_check_targets[i]=target;
        lv_obj_set_pos(target,touch_target_x[i]-26,touch_target_y[i]-26);lv_obj_set_size(target,52,52);
        lv_obj_set_style_radius(target,LV_RADIUS_CIRCLE,0);lv_obj_set_style_border_width(target,2,0);
        lv_obj_set_style_border_color(target,lv_color_hex(0xD6C5FF),0);
        lv_obj_clear_flag(target,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
    }
    touch_check_label=lv_label_create(touch_check_overlay);lv_obj_set_pos(touch_check_label,18,90);
    lv_obj_clear_flag(touch_check_label,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(touch_check_label,284);lv_obj_set_style_text_font(touch_check_label,&lv_font_montserrat_20,0);
    lv_obj_set_style_text_color(touch_check_label,lv_color_hex(0xF1EFFF),0);
    lv_obj_set_style_text_align(touch_check_label,LV_TEXT_ALIGN_CENTER,0);update_touch_check();
}
bool fixture_mode;
forearm::LoadSchedule fixture_load;
uint32_t fixture_append_rejected, fixture_live_suppressed;
unsigned fixture_length = 160;
char fixture_pattern[16] = "cycle";
char displayed_login_url[512]{},displayed_login_code[32]{};
bool network_started;
std::array<uint32_t,128> ui_frame_times{};
size_t ui_frame_count, ui_frame_position;
uint32_t ui_frame_max;
uint32_t fnv(const void* bytes, size_t size) {
    auto p = static_cast<const uint8_t*>(bytes);
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < size; ++i) h = (h ^ p[i]) * 16777619u;
    return h;
}
const char* str(cJSON* object, const char* key) {
    const auto* x = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(x) && x->valuestring ? x->valuestring : "";
}
int num(cJSON* object, const char* key, int fallback = 0) {
    const auto* x = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsNumber(x) ? x->valueint : fallback;
}
void flag(cJSON* o, const char* k, bool v) { cJSON_AddBoolToObject(o, k, v); }
template <typename T> void metric(cJSON* o, const char* k, T v) { cJSON_AddNumberToObject(o, k, static_cast<double>(v)); }
void reply(cJSON* response) {
    char* line = cJSON_PrintUnformatted(response);
    if (line) {
        // The SDK's character-at-a-time console writer can drop characters
        // after a short host stall. Queue bounded chunks with an overall limit.
        const auto deadline=esp_timer_get_time()+1000000;
        if(xSemaphoreTake(usb_output_mutex,pdMS_TO_TICKS(250))==pdTRUE) {
            const bool sent=usb_reply_write("\n@",2,deadline) &&
                usb_reply_write(line,strlen(line),deadline) && usb_reply_write("\n",1,deadline);
            if(!sent)++usb_reply_errors;
            xSemaphoreGive(usb_output_mutex);
        } else ++usb_reply_errors;
        cJSON_free(line);
    } else ++usb_reply_errors;
    cJSON_Delete(response);
}
void set_fixture_mode(bool enabled) {
    fixture_load.stop(esp_timer_get_time()/1000);
    fixture_mode = enabled;
    app_ui_set_fixture_mode(enabled);
    if (enabled) {
        fixture_live_suppressed = 0;
        app_ui_set_channel("SYNTHETIC FIXTURES");
        app_ui_set_status("Synthetic fixtures - no Twitch actions");
    } else {
        const auto network = twitch_status();
        if (network.authenticated) app_ui_set_authenticated(network.login, network.channel);
        app_ui_set_network(network.state);
        char id[48]; snprintf(id,sizeof(id),"fixture-gap-%lu",static_cast<unsigned long>(++fixture_sequence));
        app_ui_append("system",id,"system","Fixture session ended",
            "Synthetic history cleared. Live chat resumes here; messages received during fixtures were not shown.",esp_timer_get_time()/1000);
    }
}
void fixture_tick() {
    if (!fixture_mode) return;
    const auto now = static_cast<uint64_t>(esp_timer_get_time()/1000);
    const auto batch = fixture_load.tick(now, 8);
    for (unsigned i=0; i<batch; ++i) {
        const auto sequence = ++fixture_sequence;
        const char* token = "Readable chat scrolling and pause qualification. ";
        if (!strcmp(fixture_pattern,"emoji") || (!strcmp(fixture_pattern,"cycle") && sequence%3==1)) token="😀🚀🌙💜 ";
        else if (!strcmp(fixture_pattern,"mixed") || (!strcmp(fixture_pattern,"cycle") && sequence%3==2)) token="café Ελληνικά Кириллица 😀 Kappa ";
        char text[2049], id[48];
        auto used=static_cast<size_t>(snprintf(text,sizeof(text),"SYNTHETIC #%lu: ",static_cast<unsigned long>(sequence)));
        if (used > fixture_length) used=fixture_length;
        const auto token_size=strlen(token);
        while (used+token_size<=fixture_length) {memcpy(text+used,token,token_size);used+=token_size;}
        while (used<fixture_length) text[used++]='.';
        text[used]=0;
        snprintf(id,sizeof(id),"fixture-load-%lu",static_cast<unsigned long>(sequence));
        if (!app_ui_append("fixture",id,"fixture-user","SYNTHETIC FIXTURE",text,now)) ++fixture_append_rejected;
    }
}
void status(cJSON* response) {
    const auto b = board_stats(); const auto u = app_ui_stats(); const auto t = twitch_status();
    char firmware[80];
    snprintf(firmware,sizeof(firmware),"forearm-chat/%s",esp_app_get_description()->version);
    cJSON_AddStringToObject(response, "firmware", firmware);
    cJSON_AddStringToObject(response,"source_commit",FOREARM_SOURCE_COMMIT);
    cJSON_AddStringToObject(response,"sdk_version",esp_app_get_description()->idf_ver);
    char elf_hash[65];
    for(unsigned i=0;i<32;++i)snprintf(elf_hash+2*i,3,"%02x",esp_app_get_description()->app_elf_sha256[i]);
    cJSON_AddStringToObject(response,"app_elf_sha256",elf_hash);
    metric(response, "uptime_ms", esp_timer_get_time()/1000);
    metric(response,"usb_reply_errors",usb_reply_errors);
    auto touch_check=cJSON_AddObjectToObject(response,"touch_check");
    flag(touch_check,"active",touch_check_overlay!=nullptr);metric(touch_check,"accepted",touch_check_count);
    metric(touch_check,"missed",touch_check_missed);
    auto points=cJSON_AddArrayToObject(touch_check,"points");
    for(unsigned i=0;i<touch_check_count;++i) {
        auto point=cJSON_CreateObject();metric(point,"x",touch_check_points[i].x);metric(point,"y",touch_check_points[i].y);
        metric(point,"expected_x",touch_target_x[i%5]);metric(point,"expected_y",touch_target_y[i%5]);
        metric(point,"squared_error_px",touch_check_points[i].squared_error);cJSON_AddItemToArray(points,point);
    }
    metric(response,"main_stack_free",uxTaskGetStackHighWaterMark(nullptr));
    flag(response, "network_started", network_started);
    if (ui_frame_count) {
        auto sorted=ui_frame_times;
        std::sort(sorted.begin(),sorted.begin()+ui_frame_count);
        metric(response,"ui_frame_samples",ui_frame_count);
        metric(response,"ui_frame_p50_us",sorted[(ui_frame_count-1)/2]);
        metric(response,"ui_frame_p95_us",sorted[(ui_frame_count*95+99)/100-1]);
        metric(response,"ui_frame_max_us",ui_frame_max);
    }
    auto board = cJSON_AddObjectToObject(response, "board");
#define BM(name) metric(board, #name, b.name)
    BM(frames); BM(frame_us_total); BM(last_frame_us); BM(max_frame_us); BM(flush_errors);
    BM(recent_frame_samples); BM(recent_frame_p50_us); BM(recent_frame_p95_us);
    BM(render_submissions);BM(frame_queue_wait_us_total);BM(frame_queue_wait_us_last);BM(frame_queue_wait_us_max);
    BM(te_wait_us_total);BM(te_wait_us_last);BM(te_wait_us_max);
    BM(convert_us_total);BM(convert_us_last);BM(convert_us_max);
    BM(dma_wait_us_total);BM(dma_wait_us_last);BM(dma_wait_us_max);
    BM(spi_submit_us_total);BM(spi_submit_us_last);BM(spi_submit_us_max);
    BM(payload_us_last);BM(payload_us_max);BM(job_dispatch_us_last);BM(job_dispatch_us_max);
    BM(lvgl_refresh_us_total);BM(lvgl_refresh_us_last);BM(lvgl_refresh_us_max);
    BM(render_cpu_us_total);BM(render_cpu_us_last);BM(render_cpu_us_max);
    BM(sync_copy_us_total);BM(sync_copy_us_last);BM(sync_copy_us_max);BM(sync_copy_calls);BM(sync_copy_pixels);
    BM(physical_press_sequence);BM(touch_last_delivered_sequence);BM(touch_last_delivered_sample_us);
    BM(touch_frame_latency_samples);BM(touch_frame_latency_recent_samples);BM(touch_frame_latency_pending);
    BM(touch_frame_latency_us_last);BM(touch_frame_latency_us_max);BM(touch_frame_latency_us_p50);BM(touch_frame_latency_us_p95);
    BM(touch_frame_latency_us_total);BM(touch_frame_latency_last_sequence);
    BM(touch_frame_latency_dropped);BM(touch_frame_latency_ignored_injected);
    BM(te_edges); BM(te_wait_timeouts); BM(touch_polls); BM(touch_errors); BM(touch_contacts);
    BM(touch_write_errors);BM(touch_read_errors);BM(touch_timeout_errors);BM(touch_fail_errors);BM(touch_other_errors);
    BM(touch_last_error_code);BM(touch_last_error_operation);BM(touch_last_error_at_us);
    BM(touch_last_error_duration_us);BM(touch_last_error_cycle_us);BM(touch_last_error_poll_gap_us);
    BM(touch_write_us_last);BM(touch_write_us_max);BM(touch_read_us_last);BM(touch_read_us_max);
    BM(touch_pressed_samples); BM(injected_samples); BM(invalid_touch_samples);
    BM(touch_zero_points); BM(touch_many_points); BM(touch_out_of_bounds); BM(touch_all_ff);
    BM(touch_idle_packets); BM(touch_nonzero_headers);
    BM(touch_down_events); BM(touch_up_events); BM(touch_contact_events); BM(touch_reserved_events);
    BM(touch_queue_events);BM(touch_queue_coalesced);BM(touch_queue_overflows);
    BM(touch_queue_depth);BM(touch_queue_peak);BM(touch_sample_gap_max_us);
    BM(touch_sample_gap_min_us);BM(touch_fast_polls);
    BM(touch_sampler_core);
    BM(touch_release_edges);BM(touch_error_cancels);BM(touch_no_data_held_samples);
    BM(touch_consecutive_errors);BM(touch_consecutive_errors_max);
    BM(logical_flushes);BM(logical_nonfinal_flushes);
    BM(compose_copy_us_total);BM(compose_copy_us_max);BM(composed_pixels);
    BM(last_raw_x); BM(last_raw_y); BM(last_screen_x); BM(last_screen_y); BM(last_physical_touch_us);
    BM(free_internal); BM(largest_dma_block); BM(free_psram);
#undef BM
    auto latency_histogram=cJSON_AddArrayToObject(board,"touch_frame_latency_histogram");
    for(unsigned i=0;i<16;++i)cJSON_AddItemToArray(latency_histogram,cJSON_CreateNumber(b.touch_frame_latency_histogram[i]));
    for (const auto* key : {"last_touch_packet","last_contact_packet","last_invalid_packet"}) {
        auto a=cJSON_AddArrayToObject(board,key);
        const uint8_t* bytes=!strcmp(key,"last_touch_packet")?b.last_touch_packet:
                             !strcmp(key,"last_contact_packet")?b.last_contact_packet:b.last_invalid_packet;
        for (unsigned i=0;i<8;++i) cJSON_AddItemToArray(a,cJSON_CreateNumber(bytes[i]));
    }
    flag(board, "physical_pressed", b.physical_pressed); flag(board, "touch_present", b.touch_present);
    flag(board,"touch_sampler_running",b.touch_sampler_running);
    flag(board,"internal_strip_render",b.internal_strip_render);
    flag(board,"asynchronous_transport",b.asynchronous_transport);
    flag(board,"display_worker_running",b.display_worker_running);
    flag(board,"display_transfer_pending",b.display_transfer_pending);
    flag(board,"display_fault",b.display_fault);
    flag(board,"staged_transport_copy",b.staged_transport_copy);
    flag(board,"touch_i2c_iram",b.touch_i2c_iram);
    auto ui = cJSON_AddObjectToObject(response, "ui");
#define UM(name) metric(ui, #name, u.name)
    UM(unread); UM(retained); UM(anchor_sequence); UM(anchor_offset); UM(scroll_y);
    UM(history_height); UM(selected_sequence); UM(appended); UM(rejected); UM(taps); UM(scrolls);
    UM(tick_last_us);UM(tick_max_us);UM(layout_last_us);UM(layout_max_us);
    UM(render_last_us);UM(render_max_us);UM(measure_last_us);UM(measure_max_us);UM(label_updates);
    UM(text_draw_total_us);UM(text_draw_calls);UM(text_draw_max_us);
    UM(sprite_cache_bytes);UM(sprite_cache_hits);UM(sprite_cache_misses);UM(sprite_cache_builds);
    UM(sprite_cache_evictions);UM(sprite_cache_rejected);UM(sprite_cache_build_total_us);UM(sprite_cache_build_max_us);
    UM(author_cache_bytes);UM(author_cache_hits);UM(author_cache_misses);UM(author_cache_builds);
    UM(author_cache_evictions);UM(author_cache_rejected);UM(author_cache_build_total_us);UM(author_cache_build_max_us);
    UM(backdrop_draw_total_us);UM(backdrop_draw_calls);UM(backdrop_draw_max_us);
    UM(author_draw_total_us);UM(author_draw_calls);UM(author_draw_max_us);
    UM(author_style_total_us);UM(author_style_calls);UM(author_style_max_us);
    UM(opaque_body_total_us);UM(opaque_body_calls);UM(opaque_body_max_us);UM(author_blit_total_us);
    UM(backdrop_pixels_skipped);UM(backdrop_culled_draws);UM(backdrop_fallback_draws);
    UM(moderation_request_id);UM(moderation_submitted);
    metric(ui,"moderation_state",static_cast<unsigned>(u.moderation_state));
#undef UM
    flag(ui, "paused", u.paused); flag(ui, "demo", u.demo); flag(ui, "authenticated", u.authenticated);
    flag(ui,"fixture_mode",u.fixture_mode);
    flag(ui, "login_visible", u.login_visible); flag(ui, "selection_visible", u.selection_visible);
    flag(ui,"sprite_cache_enabled",u.sprite_cache_enabled);
    flag(ui,"author_cache_enabled",u.author_cache_enabled);
    flag(ui,"backdrop_culling_enabled",u.backdrop_culling_enabled);
    flag(ui,"read_only",u.read_only);
    flag(ui,"moderation_pending",u.moderation_pending);
    flag(ui,"moderation_confirmation_visible",u.moderation_confirmation_visible);
    auto net = cJSON_AddObjectToObject(response, "twitch");
    flag(net, "worker_running", t.worker_running);
    flag(net, "configured", t.configured); flag(net, "wifi_connected", t.wifi_connected);
    flag(net, "time_synchronized", t.time_synchronized); flag(net, "authenticated", t.authenticated);
    flag(net, "chat_connected", t.chat_connected); flag(net, "login_pending", t.login_pending);
    flag(net,"read_only",t.read_only);
    metric(net, "received_messages", t.received_messages); metric(net, "dropped_events", t.dropped_events);
    metric(net, "reconnects", t.reconnects); metric(net, "malformed_frames", t.malformed_frames);
    metric(net,"worker_stack_free",t.worker_stack_free);metric(net,"websocket_stack_free",t.websocket_stack_free);
    metric(net,"moderation_generation",t.moderation_generation);
    metric(net,"moderation_requests",t.moderation_requests);
    metric(net,"moderation_succeeded",t.moderation_succeeded);
    metric(net,"moderation_unknown",t.moderation_unknown);
    metric(net,"login_poll_count",t.login_poll_count);
    metric(net,"last_login_http_status",t.last_login_http_status);
    metric(net,"last_login_poll_result",static_cast<unsigned>(t.last_login_poll_result));
    metric(net,"login_seconds_remaining",t.login_seconds_remaining);
    metric(net,"refresh_attempts",t.refresh_attempts);metric(net,"refresh_succeeded",t.refresh_succeeded);metric(net,"refresh_failed",t.refresh_failed);
    flag(net,"moderation_delete_granted",t.moderation_delete_granted);
    flag(net,"moderation_ban_granted",t.moderation_ban_granted);
    flag(net,"moderation_role_verified",t.moderation_role_verified);
    cJSON_AddStringToObject(net, "channel", t.channel); cJSON_AddStringToObject(net, "login", t.login);
    cJSON_AddStringToObject(net, "state", t.state);
    const auto em=emote_cache_stats();
    auto cache=cJSON_AddObjectToObject(response,"emotes");
    flag(cache,"worker_running",em.worker_running);
    metric(cache,"worker_stack_free",em.worker_stack_free);
    metric(cache,"requested",em.requested);metric(cache,"ready",em.ready);
    metric(cache,"failed",em.failed);metric(cache,"rejected",em.rejected);
    metric(cache,"downloads",em.downloads);metric(cache,"bytes",em.bytes);metric(cache,"decoded_bytes",em.decoded_bytes);
    metric(ui,"emote_rows",u.emote_rows);metric(ui,"visible_emote_images",u.visible_emote_images);
    auto fixtures=cJSON_AddObjectToObject(response,"fixtures");
    flag(fixtures,"enabled",fixture_mode);flag(fixtures,"load_active",fixture_load.active());
    metric(fixtures,"rate",fixture_load.rate());metric(fixtures,"duration_ms",fixture_load.duration());
    metric(fixtures,"elapsed_ms",fixture_load.elapsed(esp_timer_get_time()/1000));
    metric(fixtures,"generated",fixture_load.generated());metric(fixtures,"schedule_dropped",fixture_load.dropped());
    metric(fixtures,"append_rejected",fixture_append_rejected);metric(fixtures,"live_events_suppressed",fixture_live_suppressed);
    metric(fixtures,"length_bytes",fixture_length);cJSON_AddStringToObject(fixtures,"pattern",fixture_pattern);
}
void command(char* input) {
    cJSON* request = cJSON_Parse(input); if (!request) return;
    const char* cmd = str(request, "cmd");
    cJSON* response = cJSON_CreateObject(); metric(response, "reply", num(request, "id"));
    bool ok = true;
    if (!strcmp(cmd, "status")) status(response);
    else if(!strcmp(cmd,"touch_check")) {
        if(num(request,"enabled",1)) {
            ok=!twitch_status().login_pending && !app_ui_stats().moderation_pending && !app_ui_stats().selection_visible;
            if(ok)start_touch_check();
        } else stop_touch_check();
    }
    else if (!strcmp(cmd,"perf_reset")) {
        ui_frame_count=ui_frame_position=ui_frame_max=0;ui_frame_times.fill(0);app_ui_reset_perf();board_reset_touch_latency();
    }
    else if (!strcmp(cmd,"sprite_cache")) app_ui_set_sprite_cache(num(request,"enabled",1)!=0);
    else if (!strcmp(cmd,"author_cache")) app_ui_set_author_cache(num(request,"enabled",1)!=0);
    else if (!strcmp(cmd,"backdrop_culling")) app_ui_set_backdrop_culling(num(request,"enabled",1)!=0);
    else if (!strcmp(cmd,"transport_copy")) board_set_staged_transport_copy(num(request,"staged",0)!=0);
    else if (!strcmp(cmd,"network_disconnect")) twitch_request_disconnect_for_test(std::clamp(num(request,"duration_ms",5000),1000,30000));
    else if (!strcmp(cmd,"wifi_disconnect")) twitch_request_wifi_disconnect_for_test(std::clamp(num(request,"duration_ms",5000),1000,30000));
    else if (!strcmp(cmd,"validate_login")) twitch_request_validation();
    else if (!strcmp(cmd,"refresh_login")) ok=twitch_request_refresh_for_test();
    else if (!strcmp(cmd,"fixture_mode")) {
        const auto ui=app_ui_stats();
        ok=!ui.moderation_pending && !ui.moderation_confirmation_visible;
        if(ok)set_fixture_mode(num(request,"enabled",1)!=0);
    }
    else if (!strcmp(cmd,"load_start")) {
        const auto rate=num(request,"rate",20), duration=num(request,"duration_ms",600000), length=num(request,"length",160);
        const char* pattern=str(request,"pattern"); if(!*pattern)pattern="cycle";
        ok=fixture_mode && !fixture_load.active() && length>=1 && length<=2048 &&
            (!strcmp(pattern,"ascii")||!strcmp(pattern,"emoji")||!strcmp(pattern,"mixed")||!strcmp(pattern,"cycle")) &&
            fixture_load.start(rate,duration,esp_timer_get_time()/1000);
        if(ok){fixture_length=length;snprintf(fixture_pattern,sizeof(fixture_pattern),"%s",pattern);fixture_append_rejected=0;}
    }
    else if (!strcmp(cmd,"load_stop")) fixture_load.stop(esp_timer_get_time()/1000);
    else if (!strcmp(cmd, "pause")) app_ui_pause();
    else if (!strcmp(cmd, "live")) app_ui_go_live();
    else if (!strcmp(cmd, "scroll")) app_ui_scroll(std::clamp(num(request, "pixels"), -20000, 20000));
    else if (!strcmp(cmd, "burst")) app_ui_demo_burst(std::clamp(num(request, "count", 25), 1, 1000));
    else if (!strcmp(cmd, "message")) {
        const char* text=str(request,"text"); const char* name=str(request,"name");
        ok=app_ui_stats().demo && *text && strlen(text)<=2048;
        if (ok) {
            char id[48]; snprintf(id,sizeof(id),"fixture-%lu",static_cast<unsigned long>(++fixture_sequence));
            AppUiEmote emotes[8]{}; unsigned count=0;
            auto array=cJSON_GetObjectItemCaseSensitive(request,"emotes");
            cJSON* item=nullptr;
            cJSON_ArrayForEach(item,array) {
                if(count==8) break;
                auto& emote=emotes[count++];
                emote.id=str(item,"id");emote.text=str(item,"text");
                const auto* offset=cJSON_GetObjectItemCaseSensitive(item,"byte_offset");
                const auto* length=cJSON_GetObjectItemCaseSensitive(item,"byte_length");
                const int start=num(item,"byte_offset",-1),bytes=num(item,"byte_length",-1);
                const auto* positioned=cJSON_GetObjectItemCaseSensitive(item,"position_valid");
                emote.position_valid=cJSON_IsNumber(offset) && cJSON_IsNumber(length) &&
                    offset->valuedouble==start && length->valuedouble==bytes && start>=0 && start<=2048 &&
                    bytes>0 && bytes<=2048 && start<=2048-bytes && (!positioned || cJSON_IsTrue(positioned));
                if(emote.position_valid) {
                    emote.byte_offset=static_cast<uint16_t>(start);emote.byte_length=static_cast<uint16_t>(bytes);
                }
            }
            ok=app_ui_append_emotes("demo",id,"fixture-viewer",*name?name:"layout_test",text,emotes,count,
                                    esp_timer_get_time()/1000,str(request,"user_login"));
        }
    }
    else if (!strcmp(cmd,"emote_seed")) {
        const char* hex=str(request,"png_hex"); const size_t length=strlen(hex);
        uint8_t png[1400];ok=app_ui_stats().demo && length>0 && length%2==0 && length<=sizeof(png)*2;
        if(ok) {
            auto digit=[](char c)->int { if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;};
            for(size_t i=0;i<length/2;++i) { int a=digit(hex[i*2]),b=digit(hex[i*2+1]);if(a<0||b<0){ok=false;break;}png[i]=static_cast<uint8_t>(a*16+b); }
            if(ok)ok=emote_cache_seed_png(str(request,"emote_id"),png,length/2);
        }
    }
    else if (!strcmp(cmd, "demo")) app_ui_set_demo(num(request, "enabled", 1) != 0);
    else if (!strcmp(cmd, "brightness")) board_set_brightness(std::clamp(num(request, "percent", 70), 5, 100));
    else if (!strcmp(cmd, "touch")) {
        int x = num(request, "x"), y = num(request, "y");
        ok = x >= 0 && x < BOARD_WIDTH && y >= 0 && y < BOARD_HEIGHT &&
             (!touch_check_overlay || num(request,"enabled",1)==0);
        if (ok) board_inject_touch(x, y, num(request, "pressed") != 0, num(request, "enabled", 1) != 0);
    } else if (!strcmp(cmd, "capture")) {
        if (!snapshot) snapshot = static_cast<uint16_t*>(heap_caps_malloc(BOARD_WIDTH*BOARD_HEIGHT*2, MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
        ok = snapshot && board_framebuffer();
        if (ok) {
            memcpy(snapshot, board_framebuffer(), BOARD_WIDTH*BOARD_HEIGHT*2);
            metric(response, "capture", ++capture_generation); metric(response, "width", BOARD_WIDTH); metric(response, "height", BOARD_HEIGHT);
        }
    } else if (!strcmp(cmd, "row")) {
        int y = num(request, "y", -1);
        ok = snapshot && y >= 0 && y < BOARD_HEIGHT && num(request, "capture") == static_cast<int>(capture_generation);
        if (ok) {
            const uint8_t* row = reinterpret_cast<const uint8_t*>(snapshot + y*BOARD_WIDTH);
            char hex[BOARD_WIDTH*4+1]; constexpr char digits[] = "0123456789abcdef";
            for (int i=0;i<BOARD_WIDTH*2;++i) { hex[2*i]=digits[row[i]>>4]; hex[2*i+1]=digits[row[i]&15]; }
            hex[BOARD_WIDTH*4]=0; cJSON_AddStringToObject(response, "data", hex);
            metric(response, "checksum", fnv(row, BOARD_WIDTH*2)); metric(response, "y", y);
        }
    } else if (!strcmp(cmd,"watch")) {
        const auto* mode=cJSON_GetObjectItemCaseSensitive(request,"read_only");
        ok=!mode || cJSON_IsBool(mode) || (cJSON_IsNumber(mode) && (mode->valuedouble==0 || mode->valuedouble==1));
        if(ok)ok=twitch_watch_channel(str(request,"channel"),!mode || cJSON_IsTrue(mode) ||
                                     (cJSON_IsNumber(mode) && mode->valuedouble==1));
    } else if (!strcmp(cmd, "config")) ok = twitch_set_config(str(request,"ssid"),str(request,"password"),str(request,"client_id"),str(request,"channel"));
    else if (!strcmp(cmd, "login")) twitch_request_login();
    else if (!strcmp(cmd,"login_info")) {
        ok=twitch_status().login_pending && *displayed_login_code;
        if(ok){cJSON_AddStringToObject(response,"verification_url",displayed_login_url);cJSON_AddStringToObject(response,"user_code",displayed_login_code);}
    }
    else if (!strcmp(cmd, "logout")) twitch_logout();
    else ok = false;
    flag(response, "ok", ok); cJSON_Delete(request); reply(response);
}
void usb_poll() {
    static char input[3072]; static size_t used; static bool overflow;
    uint8_t chunk[256]; int got = usb_serial_jtag_read_bytes(chunk,sizeof(chunk),0);
    for (int i=0;i<got;++i) {
        if (chunk[i]=='\n') {
            if (!overflow) { input[used]=0; command(input); }
            memset(input,0,sizeof(input)); used=0; overflow=false;
        } else if (chunk[i]!='\r') { if (used+1<sizeof(input)) input[used++]=chunk[i]; else overflow=true; }
    }
}
void network_events() {
    app_ui_set_read_only(fixture_mode || twitch_status().read_only);
    app_ui_set_moderation_capabilities(fixture_mode?forearm::ModerationCapabilities{}:twitch_moderation_capabilities());
    TwitchEvent e;
    for (unsigned i=0;i<12 && twitch_next_event(e);++i) {
        if (fixture_mode && e.kind != TwitchEventKind::ModerationResult) {
            if (e.kind==TwitchEventKind::Chat || e.kind==TwitchEventKind::DeleteMessage ||
                e.kind==TwitchEventKind::ClearUser || e.kind==TwitchEventKind::ClearChannel ||
                e.kind==TwitchEventKind::Gap) ++fixture_live_suppressed;
            continue;
        }
        switch(e.kind) {
            case TwitchEventKind::Status:
                app_ui_set_network(e.text);
                if (!twitch_status().login_pending) app_ui_hide_login();
                break;
            case TwitchEventKind::Login:
                snprintf(displayed_login_url,sizeof(displayed_login_url),"%s",e.verification_url);
                snprintf(displayed_login_code,sizeof(displayed_login_code),"%s",e.user_code);
                app_ui_set_login(e.verification_url,e.user_code); break;
            case TwitchEventKind::Authenticated:
                app_ui_set_demo(false); app_ui_set_authenticated(e.display_name,e.text); app_ui_hide_login(); break;
            case TwitchEventKind::Chat:
                {
                    app_ui_set_demo(false);AppUiEmote emotes[8]{};
                    unsigned count=std::min<unsigned>(e.emote_count,8);
                    for(unsigned j=0;j<count;++j)emotes[j]={e.emotes[j].id,e.emotes[j].text,
                        e.emotes[j].byte_offset,e.emotes[j].byte_length,e.emotes[j].position_valid};
                    app_ui_append_emotes(e.channel_id,e.message_id,e.user_id,e.display_name,e.text,emotes,count,
                                        esp_timer_get_time()/1000,e.user_login);
                    if(e.moderation_metadata_valid)
                        app_ui_set_message_moderation_metadata(e.channel_id,e.message_id,e.source_channel_id,e.source_message_id,e.moderator);
                    break;
                }
            case TwitchEventKind::DeleteMessage: app_ui_delete_message(e.channel_id,e.message_id); break;
            case TwitchEventKind::ClearUser: app_ui_clear_user(e.channel_id,e.user_id); break;
            case TwitchEventKind::ClearChannel: app_ui_clear_channel(e.channel_id); break;
            case TwitchEventKind::ModerationResult: app_ui_moderation_result(e.moderation); break;
            case TwitchEventKind::Gap:
                {
                    app_ui_set_status("Connection gap - chat may be missing");
                    if(!app_ui_stats().demo){
                        char id[48];snprintf(id,sizeof(id),"gap-%lu",static_cast<unsigned long>(++fixture_sequence));
                        app_ui_append("system",id,"system","Connection gap",e.text,esp_timer_get_time()/1000);
                    }
                    break;
                }
        }
    }
}
bool submit_moderation(const forearm::ModerationRequest& request) {
    if (fixture_mode) return false;
    const auto result=twitch_moderate(request);
    if(result.state==forearm::ModerationState::Pending) return true;
    app_ui_moderation_result(result);
    return false;
}
}
extern "C" void app_main() {
    usb_serial_jtag_driver_config_t usb_config{4096,4096};
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb_config));
    usb_output_mutex=xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(usb_output_mutex?ESP_OK:ESP_ERR_NO_MEM);
    usb_serial_jtag_vfs_use_driver();
    usb_serial_jtag_vfs_set_tx_line_endings(ESP_LINE_ENDINGS_LF);
    setvbuf(stdout,nullptr,_IONBF,0);
    esp_log_set_vprintf(usb_log_vprintf);
    esp_log_level_set("wifi",ESP_LOG_WARN);
    esp_log_level_set("wifi_init",ESP_LOG_WARN);
    esp_log_level_set("net80211",ESP_LOG_WARN);
    lv_init(); board_display_init(); app_ui_set_font_fallback(forearm_fallback_font());
    app_ui_init();
    app_ui_set_login_callback(twitch_request_login); board_set_brightness(75);
    app_ui_set_moderation_login_callback(twitch_request_login);
    app_ui_set_moderation_callback(submit_moderation);
    network_started=twitch_start();
    emote_cache_init();
    if (!network_started) app_ui_set_status("Network worker unavailable");
    ESP_LOGI("forearm","Native chat ready; demo messages until Twitch authorization");
    for (;;) {
        usb_poll(); network_events(); fixture_tick(); update_touch_check();
        const uint32_t before_frames=board_render_count();
        const int64_t frame_start=esp_timer_get_time();
        app_ui_tick(); auto wait_ms=std::clamp(lv_timer_handler(),uint32_t(1),uint32_t(8));
        if (board_render_count()!=before_frames) {
            const auto elapsed=static_cast<uint32_t>(esp_timer_get_time()-frame_start);
            ui_frame_times[ui_frame_position]=elapsed;
            ui_frame_position=(ui_frame_position+1)%ui_frame_times.size();
            ui_frame_count=std::min(ui_frame_count+1,ui_frame_times.size());
            ui_frame_max=std::max(ui_frame_max,elapsed);
        }
        vTaskDelay(pdMS_TO_TICKS(wait_ms));
    }
}
