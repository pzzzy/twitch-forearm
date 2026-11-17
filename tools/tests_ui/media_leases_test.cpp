// Real LVGL image-source ownership with a deliberately tiny retiring cache.
// Anonymous synthetic messages only; no HTTP, serial or moderation actions.
#include "app_ui.h"
#include "emote_cache.h"
#include "lvgl.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

struct Entry { std::string id; lv_img_dsc_t image{}; std::array<std::uint8_t,25*28*3> pixels{}; unsigned refs=0,stamp=0; bool ready=false; };
static std::array<Entry,2> entries{};
static std::array<lv_color_t,320*480> framebuffer{};
static std::int64_t fake_us;
static unsigned generation, catalog_generation, clock_stamp, evictions, acquisitions, releases, collect_calls;
static bool drawing, catalog_enabled, race_acquire;
static std::vector<std::string> requests;
static unsigned image_users(lv_obj_t* obj,const lv_img_dsc_t* image) {
    unsigned count=lv_obj_check_type(obj,&lv_img_class)&&lv_img_get_src(obj)==image;
    for(unsigned i=0;i<lv_obj_get_child_cnt(obj);++i)count+=image_users(lv_obj_get_child(obj,i),image);
    return count;
}
static Entry* lookup(const char* id){for(auto& entry:entries)if(entry.ready&&entry.id==id)return &entry;return nullptr;}
static bool publish(const char* id) {
    if(lookup(id))return true;
    Entry* slot=nullptr;
    for(auto& entry:entries)if(!entry.refs&&(!slot||entry.stamp<slot->stamp))slot=&entry;
    if(!slot)return false;
    assert(!image_users(lv_scr_act(),&slot->image));
    if(slot->ready)++evictions;
    slot->id=id;slot->ready=true;slot->stamp=++clock_stamp;
    const auto color=lv_color_make(clock_stamp*23,190,clock_stamp*31).full;
    for(unsigned i=0;i<25*28;++i){slot->pixels[i*3]=color&255;slot->pixels[i*3+1]=color>>8;slot->pixels[i*3+2]=255;}
    slot->image={};slot->image.header.cf=LV_IMG_CF_TRUE_COLOR_ALPHA;slot->image.header.w=25;slot->image.header.h=28;
    slot->image.data=slot->pixels.data();slot->image.data_size=slot->pixels.size();++generation;return true;
}
std::int64_t esp_timer_get_time(){return fake_us;}
bool emote_cache_init(){return true;}
bool emote_cache_request(const char* id){assert(!drawing);requests.emplace_back(id);return true;}
bool emote_cache_dimensions(const char* id,unsigned& w,unsigned& h){
    assert(!drawing);auto* entry=lookup(id);w=entry?25:0;h=entry?28:0;return entry;
}
const lv_img_dsc_t* emote_cache_acquire(const char* id){
    assert(!drawing);auto* entry=lookup(id);if(!entry)return nullptr;
    if(race_acquire){assert(!entry->refs);race_acquire=false;entry->ready=false;++generation;return nullptr;}
    ++entry->refs;++acquisitions;entry->stamp=++clock_stamp;return &entry->image;
}
bool emote_cache_matches(const char* id,const lv_img_dsc_t* image){assert(!drawing);auto* entry=lookup(id);return entry&&entry->refs&&image==&entry->image;}
void emote_cache_release(const lv_img_dsc_t* image){
    assert(!drawing);for(auto& entry:entries)if(image==&entry.image){
        assert(entry.refs&&image_users(lv_scr_act(),image)<=entry.refs-1);
        if(entry.refs==1)lv_img_cache_invalidate_src(image);
        --entry.refs;++releases;return;
    }assert(false);
}
EmoteCacheStats emote_cache_stats(){EmoteCacheStats stats{};stats.generation=generation;stats.catalog_generation=catalog_generation;
    for(const auto& entry:entries){stats.ready+=entry.ready;stats.pinned+=entry.refs;}stats.evicted=evictions;return stats;}
unsigned emote_cache_collect(const char*,const char* text,forearm::MediaOccurrence* output,unsigned count,unsigned capacity){
    assert(!drawing);++collect_calls;if(catalog_enabled&&count<capacity){const auto* token=std::strstr(text,"late");if(token){
        auto& value=output[count++];value={};std::strcpy(value.id,"7tv:late");std::strcpy(value.text,"late");
        value.byte_offset=token-text;value.byte_length=4;value.position_valid=true;
    }}return count;
}
static void pump(unsigned milliseconds=64){for(unsigned i=0;i<milliseconds;i+=8){
    fake_us+=8000;lv_tick_inc(8);const auto before=collect_calls;app_ui_tick();assert(collect_calls-before<=1);lv_timer_handler();}}
static void fresh(){app_ui_set_fixture_mode(false);app_ui_set_fixture_mode(true);requests.clear();}
static void append(const char* id,const char* asset,const char* text="Kappa"){
    const AppUiEmote occurrence{asset,"Kappa",0,5,true};
    assert(app_ui_append_emotes("100",id,"300","viewer",text,&occurrence,1));app_ui_go_live();pump();
}
int main(){
    lv_init();static lv_disp_draw_buf_t buffer;lv_disp_draw_buf_init(&buffer,framebuffer.data(),nullptr,framebuffer.size());
    lv_disp_drv_t display;lv_disp_drv_init(&display);display.hor_res=320;display.ver_res=480;display.direct_mode=1;display.draw_buf=&buffer;
    display.flush_cb=[](lv_disp_drv_t* driver,const lv_area_t*,lv_color_t*){lv_disp_flush_ready(driver);};lv_disp_drv_register(&display);
    app_ui_init();app_ui_set_authenticated("Synthetic owner","synthetic_room");
    lv_obj_add_event_cb(lv_scr_act(),[](lv_event_t*){assert(!drawing);drawing=true;},LV_EVENT_DRAW_MAIN_BEGIN,nullptr);
    lv_obj_add_event_cb(lv_scr_act(),[](lv_event_t*){assert(drawing);drawing=false;},LV_EVENT_DRAW_POST_END,nullptr);
    assert(publish("25"));fresh();append("first","25");assert(app_ui_stats().visible_emote_images==1&&emote_cache_stats().pinned==1);
    const auto unchanged=acquisitions;app_ui_scroll(1);pump();assert(acquisitions==unchanged); // Retain an exact existing lease.
    assert(publish("26"));assert(!publish("27")||lookup("25"));assert(lookup("25")&&lookup("25")->refs); // Pinned asset cannot retire.
    fresh();assert(!emote_cache_stats().pinned&&acquisitions==releases);
    append("pending","999999");
    const auto literal_pixels=framebuffer;fresh();
    assert(publish("25"));race_acquire=true;append("race","25");
    assert(!app_ui_stats().visible_emote_images&&!emote_cache_stats().pinned); // Acquisition race retains the same non-text pending slot.
    assert(!std::memcmp(literal_pixels.data(),framebuffer.data(),sizeof(framebuffer)));
    assert(publish("25"));pump(128);assert(app_ui_stats().visible_emote_images==1); // Generation restores image without a new message.
    for(unsigned i=0;i<70;++i){fresh();const auto id=std::to_string(1000+i);assert(publish(id.c_str()));append(id.c_str(),id.c_str());
        assert(app_ui_stats().visible_emote_images==1&&emote_cache_stats().pinned==1);}
    assert(evictions>=64);fresh();assert(!emote_cache_stats().pinned&&acquisitions==releases);
    // A late catalog must not turn already displayed words into graphics.
    // Existing native spans/anchors remain unchanged; unseen new rows can enrich.
    app_ui_set_fixture_mode(false);assert(publish("25"));assert(publish("7tv:late"));
    append("catalog-native","25","Kappa late");app_ui_pause();pump(128);const auto before=app_ui_stats();
    const auto shown_pixels = framebuffer;
    catalog_enabled=true;++catalog_generation;pump(128);
    assert(app_ui_stats().visible_emote_images==1&&app_ui_stats().appended==before.appended);
    assert(!std::memcmp(shown_pixels.data(),framebuffer.data(),sizeof(framebuffer)));
    assert(app_ui_stats().anchor_sequence==before.anchor_sequence&&app_ui_stats().anchor_offset==before.anchor_offset);
    // A delayed transport event may have been interpreted before this catalog
    // and delivered after the generation scan completed. It still catches up
    // without another catalog publication or another arriving message.
    const auto collected=collect_calls;pump(128);assert(collect_calls==collected);
    assert(app_ui_append("100","delayed-old-catalog","300","viewer","late"));
    app_ui_go_live();pump(128);
    assert(collect_calls==collected+1&&app_ui_stats().visible_emote_images==2);
    catalog_enabled=false;++catalog_generation;pump(128);assert(app_ui_stats().visible_emote_images==2);
    // Background history does not fill the cache request queue. Retries renew
    // only current viewport demand, without a new message being appended.
    fresh();for(unsigned i=0;i<40;++i){const auto id=std::to_string(2000+i);append(id.c_str(),id.c_str());}
    requests.clear();pump(1100);assert(!requests.empty());
    for(const auto& id:requests)assert(std::stoul(id)>=2033); // At most the last visible seven short rows.
    const auto appended=app_ui_stats().appended;const auto last=std::string("2039");assert(publish(last.c_str()));pump(128);
    assert(app_ui_stats().appended==appended&&app_ui_stats().visible_emote_images>=1);
    fresh();assert(!emote_cache_stats().pinned&&acquisitions==releases);
    std::puts("Media leases passed: exact retained leases, detach-before-release, race-safe pending slots, generation-only recovery,70-asset retirement, visible-only retries and late-catalog text stability and first-paint enrichment preserving native spans/anchors; no cache operations during DRAW.");
}

const lv_img_dsc_t* emote_cache_frame(const lv_img_dsc_t* image, std::uint32_t) { return image; }
