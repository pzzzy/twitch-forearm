#include "lvgl.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { W = 64, H = 96 };
static lv_color_t first[W * H], second[W * H], queued_snapshot[W * H], presented[W * H];
static lv_color_t* in_flight;
static unsigned logical_flushes, submissions, completed_frames, ownership_checks;

static void assert_source_immutable(void) {
    if (!in_flight) return;
    assert(memcmp(in_flight, queued_snapshot, sizeof(queued_snapshot)) == 0);
    ++ownership_checks;
}

static void complete_transport(void) {
    assert_source_immutable();
    memcpy(presented, in_flight, sizeof(presented));
    ++completed_frames;
    in_flight = NULL;
}

static void draw_event(lv_event_t* event) {
    (void)event;
    // Check the queued source throughout real rendering of the other buffer,
    // rather than only checking the final result after composition.
    assert_source_immutable();
}

static void flush(lv_disp_drv_t* driver, const lv_area_t* area, lv_color_t* colors) {
    ++logical_flushes;
    assert(area->x1 == 0 && area->y1 == 0 && area->x2 == W - 1 && area->y2 == H - 1);
    assert(colors == first || colors == second);
    if (in_flight) assert(colors != in_flight);
    assert_source_immutable();
    if (lv_disp_flush_is_last(driver)) {
        // Simulate a slow transport still owning the previous frame during all
        // of this frame's drawing. Completion must precede the next handover;
        // only then may LVGL swap back and reuse the released old buffer.
        if (in_flight) complete_transport();
        in_flight = colors;
        memcpy(queued_snapshot, colors, sizeof(queued_snapshot));
        ++submissions;
    }
    // This is an early logical acknowledgment, not physical completion.
    lv_disp_flush_ready(driver);
}

static lv_obj_t* rectangle(lv_obj_t* parent, int x, int y, lv_color_t color) {
    lv_obj_t* object = lv_obj_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, 10, 10);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(object, color, 0);
    lv_obj_add_event_cb(object, draw_event, LV_EVENT_DRAW_MAIN_BEGIN, NULL);
    lv_obj_add_event_cb(object, draw_event, LV_EVENT_DRAW_MAIN_END, NULL);
    return object;
}

int main(void) {
    lv_init();
    lv_disp_draw_buf_t buffer;
    lv_disp_draw_buf_init(&buffer, first, second, W * H);
    lv_disp_drv_t driver;
    lv_disp_drv_init(&driver);
    driver.hor_res = W; driver.ver_res = H;
    driver.draw_buf = &buffer;
    driver.direct_mode = 1; driver.full_refresh = 0;
    driver.flush_cb = flush;
    lv_disp_t* display = lv_disp_drv_register(&driver);
    lv_obj_t* screen = lv_disp_get_scr_act(display);
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_add_event_cb(screen, draw_event, LV_EVENT_DRAW_MAIN_BEGIN, NULL);
    lv_obj_add_event_cb(screen, draw_event, LV_EVENT_DRAW_MAIN_END, NULL);
    lv_obj_t* one = rectangle(screen, 2, 2, lv_color_hex(0xff0000));
    lv_obj_t* two = rectangle(screen, 40, 70, lv_color_hex(0x0000ff));
    lv_refr_now(display);
    assert(submissions == 1 && completed_frames == 0 && in_flight == first);
    assert(buffer.buf_act == second);
    assert(queued_snapshot[5 * W + 5].full == lv_color_hex(0xff0000).full);
    assert(queued_snapshot[75 * W + 45].full == lv_color_hex(0x0000ff).full);

    lv_obj_set_style_bg_color(one, lv_color_hex(0x00ff00), 0);
    lv_refr_now(display);
    assert(submissions == 2 && completed_frames == 1 && in_flight == second);
    assert(buffer.buf_act == first);
    assert(presented[5 * W + 5].full == lv_color_hex(0xff0000).full);
    assert(queued_snapshot[5 * W + 5].full == lv_color_hex(0x00ff00).full);
    assert(queued_snapshot[75 * W + 45].full == lv_color_hex(0x0000ff).full);
    assert(queued_snapshot[40 * W + 25].full == lv_color_black().full);

    const unsigned before = logical_flushes;
    lv_obj_set_style_bg_color(one, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_color(two, lv_color_hex(0xffff00), 0);
    lv_refr_now(display);
    assert(logical_flushes - before == 2);
    assert(submissions == 3 && completed_frames == 2 && in_flight == first);
    assert(buffer.buf_act == second);
    assert(presented[5 * W + 5].full == lv_color_hex(0x00ff00).full);
    assert(presented[75 * W + 45].full == lv_color_hex(0x0000ff).full);
    assert(queued_snapshot[5 * W + 5].full == lv_color_hex(0xffffff).full);
    assert(queued_snapshot[75 * W + 45].full == lv_color_hex(0xffff00).full);
    assert(queued_snapshot[40 * W + 25].full == lv_color_black().full);
    complete_transport();
    assert(completed_frames == submissions && ownership_checks > 5);
    printf("LVGL8 async direct ownership passed: %u compositions, %u completed whole frames, %u source-immutability checks; dirty synchronization and alternating ownership preserved.\n", submissions, completed_frames, ownership_checks);
}
