#include "lvgl.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { W = 64, H = 96 };
static lv_color_t pixels[W * H], transmitted[W * H];
static unsigned logical_flushes, physical_frames;

static void flush(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* colors) {
    ++logical_flushes;
    // In LVGL8 direct mode these remain full-screen and buffer-base even when
    // logical rendering was clipped to a dirty rectangle.
    assert(area->x1 == 0 && area->y1 == 0 && area->x2 == W - 1 && area->y2 == H - 1);
    assert(colors == pixels);
    if (lv_disp_flush_is_last(drv)) {
        memcpy(transmitted, pixels, sizeof(pixels));
        ++physical_frames;
    }
    lv_disp_flush_ready(drv);
}

static lv_obj_t* rectangle(lv_obj_t* parent, int x, int y, lv_color_t color) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, 10, 10);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, color, 0);
    return o;
}

int main(void) {
    lv_init();
    lv_disp_draw_buf_t buffer;
    lv_disp_draw_buf_init(&buffer, pixels, NULL, W * H);
    lv_disp_drv_t driver;
    lv_disp_drv_init(&driver);
    driver.hor_res = W;
    driver.ver_res = H;
    driver.draw_buf = &buffer;
    driver.direct_mode = 1;
    driver.full_refresh = 0;
    driver.flush_cb = flush;
    lv_disp_t* display = lv_disp_drv_register(&driver);
    lv_obj_t* screen = lv_disp_get_scr_act(display);
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_t* one = rectangle(screen, 2, 2, lv_color_hex(0xff0000));
    lv_obj_t* two = rectangle(screen, 40, 70, lv_color_hex(0x0000ff));
    lv_refr_now(display);
    assert(physical_frames == 1);
    assert(transmitted[5 * W + 5].full == lv_color_hex(0xff0000).full);
    assert(transmitted[75 * W + 45].full == lv_color_hex(0x0000ff).full);
    assert(transmitted[40 * W + 25].full == lv_color_black().full);
    unsigned before = logical_flushes;
    lv_obj_set_style_bg_color(one, lv_color_hex(0x00ff00), 0);
    lv_obj_set_style_bg_color(two, lv_color_hex(0xffffff), 0);
    lv_refr_now(display);
    assert(logical_flushes - before == 2);
    assert(physical_frames == 2);
    assert(transmitted[5 * W + 5].full == lv_color_hex(0x00ff00).full);
    assert(transmitted[75 * W + 45].full == lv_color_hex(0xffffff).full);
    assert(transmitted[40 * W + 25].full == lv_color_black().full);
    lv_obj_set_style_bg_color(one, lv_color_hex(0xffff00), 0);
    lv_refr_now(display);
    assert(physical_frames == 3);
    assert(transmitted[5 * W + 5].full == lv_color_hex(0xffff00).full);
    assert(transmitted[75 * W + 45].full == lv_color_hex(0xffffff).full);
    assert(transmitted[40 * W + 25].full == lv_color_black().full);
    printf("LVGL8 direct composition passed: %u logical flushes, %u full-frame transmissions; disjoint dirty rectangles and untouched pixels preserved.\n", logical_flushes, physical_frames);
}
