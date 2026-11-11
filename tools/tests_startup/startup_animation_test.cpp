#include "startup_animation.h"
#include "lvgl.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
std::array<lv_color_t, 320 * 480> framebuffer{};
lv_indev_state_t pointer_state = LV_INDEV_STATE_REL;
lv_point_t pointer_point{40, 20};
unsigned clicks = 0;
unsigned external_ticks = 0;
lv_disp_t *display = nullptr;
void pump(std::uint64_t now, unsigned count = 1) {
    for (unsigned i = 0; i < count; ++i) {
        lv_tick_inc(8);
        startup_animation_tick(now + i * 8);
        lv_timer_handler();
    }
}
unsigned timers() {
    unsigned count = 0;
    for (auto *timer = lv_timer_get_next(nullptr); timer; timer = lv_timer_get_next(timer)) ++count;
    return count;
}
void capture(const std::string &directory, unsigned age) {
    if (directory.empty()) return;
    const auto name = directory + "/startup-" + std::to_string(age) + ".ppm";
    auto *file = std::fopen(name.c_str(), "wb"); assert(file);
    std::fprintf(file, "P6\n320 480\n255\n");
    for (const auto pixel : framebuffer) {
        const auto rgb = lv_color_to32(pixel);
        const unsigned char bytes[]{static_cast<unsigned char>(rgb >> 16),
                                    static_cast<unsigned char>(rgb >> 8),
                                    static_cast<unsigned char>(rgb)};
        assert(std::fwrite(bytes, 1, 3, file) == 3);
    }
    std::fclose(file);
}
}

int main(int argc, char **argv) {
    lv_init();
    assert(!startup_animation_start(0)); // A missing display is a safe no-op.
    assert(!startup_animation_active());
    static lv_disp_draw_buf_t buffer;
    lv_disp_draw_buf_init(&buffer, framebuffer.data(), nullptr, framebuffer.size());
    lv_disp_drv_t driver; lv_disp_drv_init(&driver);
    driver.hor_res = 320; driver.ver_res = 480;
    driver.direct_mode = 1; driver.draw_buf = &buffer;
    driver.flush_cb = [](lv_disp_drv_t *d, const lv_area_t *, lv_color_t *) { lv_disp_flush_ready(d); };
    display = lv_disp_drv_register(&driver); assert(display);
    lv_indev_drv_t pointer; lv_indev_drv_init(&pointer);
    pointer.type = LV_INDEV_TYPE_POINTER;
    pointer.read_cb = [](lv_indev_drv_t *, lv_indev_data_t *d) { d->state = pointer_state; d->point = pointer_point; };
    lv_indev_drv_register(&pointer);
    auto *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x182332), 0);
    auto *button = lv_btn_create(screen);
    lv_obj_set_pos(button, 10, 10); lv_obj_set_size(button, 90, 40);
    lv_obj_add_event_cb(button, [](lv_event_t *) { ++clicks; }, LV_EVENT_CLICKED, nullptr);
    lv_timer_create([](lv_timer_t *) { ++external_ticks; }, 20, nullptr);
    const auto timer_count = timers();
    const auto screen_children = lv_obj_get_child_cnt(screen);
    const auto top_children = lv_obj_get_child_cnt(lv_layer_top());
    lv_refr_now(display);
    const auto original = framebuffer;

    // Exhaust the actual LVGL arena; startup must reject a failed first-object
    // allocation without publishing an object, corrupting chat, or blocking.
    std::vector<void *> allocations;
    while (auto *memory = lv_mem_alloc(1)) allocations.push_back(memory);
    assert(!startup_animation_start(100));
    assert(!startup_animation_active());
    assert(lv_obj_get_child_cnt(lv_layer_top()) == top_children);
    for (auto *memory : allocations) lv_mem_free(memory);

    assert(startup_animation_start(1000));
    assert(startup_animation_active());
    assert(timers() == timer_count);
    assert(lv_obj_get_child_cnt(screen) == screen_children);
    assert(lv_obj_get_child_cnt(lv_layer_top()) == top_children + 1);
    const std::string captures = argc > 1 ? argv[1] : "";
    unsigned captures_written = 0;
    for (unsigned age = 0; age < 2200; age += 8) {
        if (age == 1400) startup_animation_set_status("Connecting to Wi-Fi");
        pump(1000 + age);
        if (age == 320 || age == 880 || age == 1248 || age == 1648 || age == 2000 || age == 2120) {
            lv_refr_now(display); capture(captures, age); ++captures_written;
        }
        assert(startup_animation_active());
    }
    assert(captures_written == 6);
    assert(external_ticks > 50); // Ordinary timers kept running throughout.
    startup_animation_tick(3200);
    assert(!startup_animation_active());
    assert(lv_obj_get_child_cnt(lv_layer_top()) == top_children);
    assert(lv_obj_get_child_cnt(screen) == screen_children);
    lv_refr_now(display);
    assert(std::memcmp(framebuffer.data(), original.data(), sizeof(framebuffer)) == 0);
    assert(timers() == timer_count);

    // A startup tap is consumed through its release; it cannot become a click
    // on the button underneath the dismissed overlay.
    assert(startup_animation_start(4000));
    pointer_state = LV_INDEV_STATE_PR;
    pump(4000, 8);
    assert(!startup_animation_active());
    assert(clicks == 0);
    pointer_state = LV_INDEV_STATE_REL; pump(4100, 6);
    pointer_state = LV_INDEV_STATE_PR; pump(4200, 4);
    pointer_state = LV_INDEV_STATE_REL; pump(4300, 6);
    assert(clicks == 1);

    // Restart and external teardown retain one owner and no stale references.
    assert(startup_animation_start(5000));
    assert(startup_animation_start(5100));
    assert(lv_obj_get_child_cnt(lv_layer_top()) == top_children + 1);
    auto *overlay = lv_obj_get_child(lv_layer_top(), -1);
    lv_obj_del(overlay);
    assert(!startup_animation_active());
    startup_animation_tick(999999);
    startup_animation_finish();
    assert(lv_obj_get_child_cnt(lv_layer_top()) == top_children);
    assert(timers() == timer_count);
    assert(startup_animation_start(10000));
    startup_animation_set_status(std::string(200, 'W').c_str());
    pump(11600); lv_refr_now(display);
    startup_animation_tick(12200);
    assert(!startup_animation_active());
    std::puts("Startup animation: real LVGL OOM, finite duration, nonblocking timers, input isolation, restart/teardown and six rendered frames PASS.");
}
