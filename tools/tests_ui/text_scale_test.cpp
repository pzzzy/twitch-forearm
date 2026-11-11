// Real LVGL pointer, glyph and layout checks; synthetic text/immutable assets only.
#include "app_ui.h"
#include "emote_cache.h"
#include "forearm_fonts.h"
#include "lvgl.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

static std::int64_t fake_us;
static lv_indev_state_t contact = LV_INDEV_STATE_REL;
static lv_point_t point{};
static std::array<lv_color_t, 320 * 480> pixels{};
static lv_img_dsc_t wide{};
static std::array<std::uint8_t, 96 * 24 * 3> wide_pixels{};
static bool ready = true;
static unsigned commits, saved_scale, moderation_calls, startup_calls;
static const std::string asset_id = "7tv:" + std::string(124, 'a');
std::int64_t esp_timer_get_time() { return fake_us; }
bool emote_cache_init() { return true; }
bool emote_cache_request(const char *) { return true; }
const lv_img_dsc_t *emote_cache_get(const char *id) { return ready && asset_id == id ? &wide : nullptr; }
EmoteCacheStats emote_cache_stats() { EmoteCacheStats value{}; value.ready = ready ? 1 : 0; return value; }
static void pump(unsigned ms = 64) {
    for (unsigned i = 0; i < ms; i += 8) { fake_us += 8000; lv_tick_inc(8); app_ui_tick(); lv_timer_handler(); }
}
static void touch(int x, int y, lv_indev_state_t state) {
    point = {static_cast<lv_coord_t>(x), static_cast<lv_coord_t>(y)}; contact = state; pump(32);
}
static void tap(int x, int y) { touch(x,y,LV_INDEV_STATE_PR); touch(x,y,LV_INDEV_STATE_REL); pump(); }
static lv_obj_t *find(lv_obj_t *obj, const char *text) {
    if (lv_obj_has_flag(obj,LV_OBJ_FLAG_HIDDEN)) return nullptr;
    for (unsigned i=0; i<lv_obj_get_child_cnt(obj); ++i) if (auto *v=find(lv_obj_get_child(obj,i),text)) return v;
    return lv_obj_check_type(obj,&lv_label_class) && !std::strcmp(lv_label_get_text(obj),text) ? obj : nullptr;
}
static lv_obj_t *slider(lv_obj_t *obj) {
    if (lv_obj_check_type(obj,&lv_slider_class)) return obj;
    for (unsigned i=0; i<lv_obj_get_child_cnt(obj); ++i) if (auto *v=slider(lv_obj_get_child(obj,i))) return v;
    return nullptr;
}
static lv_area_t bounds(lv_obj_t *obj) { assert(obj); lv_obj_update_layout(obj); lv_area_t a; lv_obj_get_coords(obj,&a); return a; }
static void back() {
    auto *label=find(lv_scr_act(),LV_SYMBOL_LEFT "  Back"); assert(label);
    const auto a=bounds(lv_obj_get_parent(label)); tap((a.x1+a.x2)/2,(a.y1+a.y2)/2); pump(500);
}
static void fixtures() { app_ui_set_fixture_mode(false); app_ui_set_fixture_mode(true); }
int main() {
    lv_init();
    static lv_disp_draw_buf_t buffer; lv_disp_draw_buf_init(&buffer,pixels.data(),nullptr,pixels.size());
    lv_disp_drv_t display; lv_disp_drv_init(&display); display.hor_res=320; display.ver_res=480;
    display.direct_mode=1; display.draw_buf=&buffer;
    display.flush_cb=[](lv_disp_drv_t *driver,const lv_area_t *,lv_color_t *){lv_disp_flush_ready(driver);};
    lv_disp_drv_register(&display);
    lv_indev_drv_t input; lv_indev_drv_init(&input); input.type=LV_INDEV_TYPE_POINTER;
    input.read_cb=[](lv_indev_drv_t *,lv_indev_data_t *data){data->point=point;data->state=contact;}; lv_indev_drv_register(&input);
    app_ui_set_scale_change_handler([](unsigned percent){++commits;saved_scale=percent;});
    app_ui_set_startup_preview_callback([](){++startup_calls;});
    app_ui_set_moderation_callback([](const forearm::ModerationRequest &){++moderation_calls;return true;});
    app_ui_set_ui_scale(0); assert(app_ui_ui_scale()==67 && !commits);
    app_ui_set_ui_scale(100); app_ui_init(); pump(10000);
    assert(!app_ui_stats().demo && !app_ui_stats().retained && app_ui_ui_scale()==100);
    assert(find(lv_scr_act(),"Your chat will appear here."));
    app_ui_set_authenticated("Synthetic owner","synthetic_room"); fixtures();
    std::string message; for(unsigned i=0;i<20;++i) message += "Readable words with emoji 💜. ";
    for(unsigned i=0;i<20;++i) {
        const auto id="sizing-"+std::to_string(i), name="viewer_"+std::to_string(i);
        assert(app_ui_append("100",id.c_str(),"300",name.c_str(),message.c_str()));
    }
    app_ui_go_live(); app_ui_scroll(-100000); app_ui_scroll(120); pump();
    const auto anchor=app_ui_stats().anchor_sequence; const auto offset=app_ui_stats().anchor_offset;
    std::array<int,9> heights{};
    const std::array<unsigned,9> levels{67,75,80,90,100,115,125,133,150};
    for(unsigned i=0;i<levels.size();++i) {
        app_ui_set_ui_scale(levels[i]); pump(); const auto state=app_ui_stats();
        assert(state.paused && state.anchor_sequence==anchor && state.anchor_offset==offset);
        assert(state.ui_scale_percent==levels[i] && !commits && !moderation_calls);
        heights[i]=state.history_height;
        auto *name=find(lv_scr_act(),"viewer_0"); assert(name);
        const auto *font=lv_obj_get_style_text_font(name,0);
        const unsigned expected[]={16,16,16,18,20,22,24,24,24};
        assert(lv_font_get_line_height(font)==lv_font_get_line_height(expected[i]==16?&lv_font_montserrat_16:
            expected[i]==18?&lv_font_montserrat_18:expected[i]==20?&lv_font_montserrat_20:
            expected[i]==22?&lv_font_montserrat_22:&lv_font_montserrat_24));
    }
    assert(heights.front()<heights[4] && heights[4]<heights.back());
    tap(30,45); pump(500); assert(app_ui_stats().selection_visible);
    app_ui_set_ui_scale(90); pump(); assert(!app_ui_stats().selection_visible && !app_ui_stats().selected_sequence);
    assert(app_ui_stats().paused && app_ui_stats().anchor_sequence==anchor && !moderation_calls);
    tap(284,36); pump(); auto *control=slider(lv_scr_act()); assert(control);
    lv_area_t hit; lv_obj_get_click_area(control,&hit); assert(lv_area_get_height(&hit)>=44);
    const auto a=bounds(control); const int cy=(a.y1+a.y2)/2;
    auto *preview=find(lv_scr_act(),"A little chat,\na little closer."); assert(preview);
    const int small_height=lv_obj_get_height(preview);
    touch(a.x1,cy,LV_INDEV_STATE_PR);
    touch(a.x2,cy,LV_INDEV_STATE_PR);
    assert(app_ui_ui_scale()==150 && slider(lv_scr_act())==control && !commits);
    assert(lv_obj_get_height(preview)>small_height); // Live while the contact is held.
    touch(a.x2,cy,LV_INDEV_STATE_REL); pump(); assert(commits==1 && saved_scale==150);
    assert(find(lv_scr_act(),"Text size  150%"));
    assert(find(lv_scr_act(),"Emoji artwork: Twemoji\nCC BY 4.0 • see project notices"));
    back(); assert(app_ui_stats().paused && app_ui_stats().anchor_sequence==anchor && !moderation_calls);
    tap(284,36); pump();
    auto *startup_label=find(lv_scr_act(),"Preview startup"); assert(startup_label);
    auto *startup_button=lv_obj_get_parent(startup_label);
    lv_obj_scroll_to_view(startup_button,LV_ANIM_OFF); pump(); const auto startup=bounds(startup_button);
    assert(lv_area_get_height(&startup)>=44);
    touch((startup.x1+startup.x2)/2,(startup.y1+startup.y2)/2,LV_INDEV_STATE_PR); assert(!startup_calls);
    touch((startup.x1+startup.x2)/2,(startup.y1+startup.y2)/2,LV_INDEV_STATE_REL); pump();
    assert(startup_calls==1 && !slider(lv_scr_act()) && app_ui_stats().paused &&
           app_ui_stats().anchor_sequence==anchor && app_ui_stats().read_only && !moderation_calls);

    wide.header.cf=LV_IMG_CF_TRUE_COLOR_ALPHA; wide.header.w=96;wide.header.h=24;
    const auto green=lv_color_make(0,255,0).full;
    for(unsigned i=0;i<96*24;++i){wide_pixels[i*3]=green&255;wide_pixels[i*3+1]=green>>8;wide_pixels[i*3+2]=255;}
    wide.data=wide_pixels.data();wide.data_size=wide_pixels.size();
    for(const auto level:levels) {
        app_ui_set_ui_scale(level); fixtures();
        const AppUiEmote emotes[]={{asset_id.c_str(),"wide",0,4,true},{asset_id.c_str(),"wide",5,4,true}};
        assert(app_ui_append_emotes("100","wide-test","300","wide_viewer","wide wide",emotes,2));
        app_ui_go_live(); pump(); assert(app_ui_stats().visible_emote_images==2);
        auto *row=lv_obj_get_parent(find(lv_scr_act(),"wide_viewer"));
        std::array<lv_obj_t*,2> images{};unsigned count=0;
        for(unsigned j=0;j<lv_obj_get_child_cnt(row);++j){auto *child=lv_obj_get_child(row,j);
            if(lv_obj_check_type(child,&lv_img_class)&&!lv_obj_has_flag(child,LV_OBJ_FLAG_HIDDEN)){ assert(count<images.size()); images[count++]=child; }}
        assert(count==2);
        const auto first=bounds(images[0]),second=bounds(images[1]);
        const unsigned zoom=lv_img_get_zoom(images[0]);
        const unsigned width=(96*zoom+255)/256,height=(24*zoom+255)/256;
        assert(width>=4*height-3 && width<=4*height+3);
        if(second.y1==first.y1) assert(second.x1-first.x1>=static_cast<int>(width)+1);
        else assert(second.y1-first.y1>=static_cast<int>(height)); // Large wide assets wrap intact.
        const auto before=pixels; app_ui_set_sprite_cache(false);pump();lv_obj_invalidate(lv_scr_act());lv_refr_now(nullptr);
        assert(!std::memcmp(before.data(),pixels.data(),sizeof(pixels))); app_ui_set_sprite_cache(true);pump();
    }
    app_ui_set_ui_scale(100); assert(commits==1);
    fixtures();
    const auto overlong_id=asset_id+"a";
    const AppUiEmote rejected{overlong_id.c_str(),"wide",0,4,true};
    assert(app_ui_append_emotes("100","bounded-id","300","viewer","wide",&rejected,1)); pump();
    assert(!app_ui_stats().emote_rows && !app_ui_stats().visible_emote_images);
    std::puts("Native size control passed: clean startup, real nine-size fonts, anchored reflow, stale-selection reset,44px slider, live held-contact preview, one persistence callback, scaled wide emotes and cached/native pixel equality.");
}
