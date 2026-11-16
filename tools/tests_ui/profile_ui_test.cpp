// Saved profile and session UX with real LVGL contacts; no network or mutations.
#include "app_ui.h"
#include "emote_cache.h"
#include "lvgl.h"
#include <cassert>
#include <cstdio>
#include <cstring>

static std::int64_t fake_us;
static lv_point_t point{};
static lv_indev_state_t contact=LV_INDEV_STATE_REL;
static unsigned submitted;
std::int64_t esp_timer_get_time(){return fake_us;}
bool emote_cache_init(){return false;}
bool emote_cache_request(const char*){return false;}
const lv_img_dsc_t* test_cached_asset(const char*){return nullptr;}
bool emote_cache_dimensions(const char* id,unsigned& width,unsigned& height) {
    const auto* image=test_cached_asset(id);
    width=image?image->header.w:0; height=image?image->header.h:0;
    return width&&height;
}
const lv_img_dsc_t* emote_cache_acquire(const char* id){return test_cached_asset(id);}
bool emote_cache_matches(const char* id,const lv_img_dsc_t* image){return image&&test_cached_asset(id)==image;}
void emote_cache_release(const lv_img_dsc_t*){}
unsigned emote_cache_collect(const char*,const char*,forearm::MediaOccurrence*,unsigned count,unsigned){return count;}

EmoteCacheStats emote_cache_stats(){return {};}
static void pump(unsigned milliseconds=64){
    for(unsigned i=0;i<milliseconds;i+=8){fake_us+=8000;lv_tick_inc(8);app_ui_tick();lv_timer_handler();}
}
static void touch(int x,int y,lv_indev_state_t state){point={static_cast<lv_coord_t>(x),static_cast<lv_coord_t>(y)};contact=state;pump(32);}
static void tap(int x,int y){touch(x,y,LV_INDEV_STATE_PR);touch(x,y,LV_INDEV_STATE_REL);pump();}
static lv_obj_t* find(lv_obj_t* obj,const char* text){
    if(lv_obj_has_flag(obj,LV_OBJ_FLAG_HIDDEN))return nullptr;
    for(unsigned i=0;i<lv_obj_get_child_cnt(obj);++i)if(auto* result=find(lv_obj_get_child(obj,i),text))return result;
    return lv_obj_check_type(obj,&lv_label_class)&&!std::strcmp(lv_label_get_text(obj),text)?obj:nullptr;
}
static lv_area_t bounds(lv_obj_t* obj){assert(obj);lv_obj_update_layout(obj);lv_area_t area{};lv_obj_get_coords(obj,&area);return area;}
static lv_obj_t* button(const char* text){auto* label=find(lv_scr_act(),text);assert(label);return lv_obj_get_parent(label);}
static void tap_button(const char* text){auto* obj=button(text);lv_obj_scroll_to_view(obj,LV_ANIM_OFF);pump();const auto a=bounds(obj);tap((a.x1+a.x2)/2,(a.y1+a.y2)/2);}
static void settings(){tap(284,36);pump();}
static void back(){tap_button(LV_SYMBOL_LEFT "  Back");pump(500);}
static void back_chat(){tap_button(LV_SYMBOL_LEFT "  Back to chat");pump(500);}
static void select(){pump(64);static unsigned attempt; ++attempt; tap(30,45);pump(500); if(!app_ui_stats().selection_visible)std::fprintf(stderr,"Profile selection attempt%u failed; retained%u paused%d\n",attempt,app_ui_stats().retained,app_ui_stats().paused);assert(app_ui_stats().selection_visible);}
int main(){
    lv_init();static lv_color_t pixels[320*32];static lv_disp_draw_buf_t buffer;
    lv_disp_draw_buf_init(&buffer,pixels,nullptr,320*32);
    lv_disp_drv_t display;lv_disp_drv_init(&display);display.hor_res=320;display.ver_res=480;display.draw_buf=&buffer;
    display.flush_cb=[](lv_disp_drv_t* driver,const lv_area_t*,lv_color_t*){lv_disp_flush_ready(driver);};lv_disp_drv_register(&display);
    lv_indev_drv_t input;lv_indev_drv_init(&input);input.type=LV_INDEV_TYPE_POINTER;
    input.read_cb=[](lv_indev_drv_t*,lv_indev_data_t* data){data->point=point;data->state=contact;};lv_indev_drv_register(&input);
    assert(!*app_ui_channel()); // No invented room before profile hydration.
    app_ui_set_profile("Saved_Room",true);app_ui_set_session(false,nullptr);app_ui_set_network("Wi-Fi unavailable");
    app_ui_init();pump();
    assert(!std::strcmp(app_ui_channel(),"#saved_room")&&!app_ui_stats().authenticated&&app_ui_stats().read_only);
    settings();assert(find(lv_scr_act(),"#saved_room")&&!find(lv_scr_act(),"#forearm"));
    assert(find(lv_scr_act(),"Not connected to Twitch")&&find(lv_scr_act(),"Wi-Fi unavailable"));
    back();assert(app_ui_append("100","saved-message","300","viewer","Offline scrollback stays readable."));app_ui_go_live();select();
    assert(find(lv_scr_act(),"#saved_room"));back_chat();
    const auto retained=app_ui_stats().retained;
    app_ui_set_profile("#SAVED_ROOM",true);pump();assert(app_ui_stats().retained==retained);
    app_ui_set_profile("saved_room",false);app_ui_set_session(true,"synthetic_account");
    forearm::ModerationCapabilities caps{};caps.authenticated=caps.online=caps.role_verified=caps.can_delete=caps.can_ban=true;
    caps.generation=7;std::strcpy(caps.broadcaster_id,"100");std::strcpy(caps.moderator_id,"200");
    app_ui_set_moderation_capabilities(caps);
    app_ui_set_moderation_callback([](const forearm::ModerationRequest&){++submitted;return true;});
    app_ui_set_message_moderation_metadata("100","saved-message","100","saved-message",false);
    select();assert(!lv_obj_has_state(button("Ban"),LV_STATE_DISABLED));tap_button("Ban");pump(500);
    const auto confirm=bounds(button("Ban user"));touch((confirm.x1+confirm.x2)/2,(confirm.y1+confirm.y2)/2,LV_INDEV_STATE_PR);
    app_ui_set_session(false,nullptr);app_ui_set_network("Saved authorization needs login");pump(500);
    touch((confirm.x1+confirm.x2)/2,(confirm.y1+confirm.y2)/2,LV_INDEV_STATE_REL);pump(500);
    assert(!submitted&&!app_ui_stats().authenticated&&app_ui_stats().retained==retained);
    assert(!std::strcmp(app_ui_channel(),"#saved_room"));
    if(find(lv_scr_act(),"Cancel")){tap_button("Cancel");pump(500);}
    back_chat();select();assert(lv_obj_has_state(button("Ban"),LV_STATE_DISABLED));
    assert(find(lv_scr_act(),"Connect Twitch to moderate."));back_chat();settings();
    assert(find(lv_scr_act(),"#saved_room")&&find(lv_scr_act(),"Not connected to Twitch"));
    assert(find(lv_scr_act(),"Saved authorization needs login")&&!find(lv_scr_act(),"synthetic_account"));
    auto* stable=find(lv_scr_act(),"#saved_room");assert(stable);
    touch(100,120,LV_INDEV_STATE_PR);
    app_ui_set_network("Waiting for account authorization");app_ui_set_session(false,nullptr);pump();
    assert(find(lv_scr_act(),"#saved_room")==stable); // Do not replace a settings tree under a held contact.
    touch(100,120,LV_INDEV_STATE_REL);pump();assert(find(lv_scr_act(),"Waiting for account authorization"));
    app_ui_set_profile("next_room",true);pump();
    assert(find(lv_scr_act(),"#next_room")&&!app_ui_stats().retained&&!app_ui_stats().selected_sequence);
    assert(!app_ui_stats().selection_visible&&!submitted);
    app_ui_set_profile("",true);pump();assert(!*app_ui_channel()&&find(lv_scr_act(),"Not configured"));
    back();assert(app_ui_append("456","unconfigured-message","300","viewer","Exact transport room remains identifiable."));app_ui_go_live();select();
    assert(find(lv_scr_act(),"Channel 456")&&!find(lv_scr_act(),"#forearm"));back_chat();
    app_ui_set_profile("next_room",true);app_ui_set_authenticated("synthetic_account","#NEXT_ROOM");
    assert(!std::strcmp(app_ui_channel(),"#next_room")&&app_ui_stats().authenticated);
    std::puts("Saved-profile UI passed: pre-auth hydration, no invented room, offline/relogin room and exact source fallback, session-loss denial despite stale grants, same-room retention, real-room invalidation, and held-contact-safe settings refresh.");
}

const lv_img_dsc_t* emote_cache_frame(const lv_img_dsc_t* image, std::uint32_t) { return image; }
