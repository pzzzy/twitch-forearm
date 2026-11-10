#include "lvgl.h"
#include <assert.h>
#include <stdio.h>

enum { W = 64, H = 96 };
static lv_color_t first[W * H], second[W * H];
static unsigned renders, final_flushes, refreshes, copy_calls, copy_pixels;
static bool rendering;
static lv_timer_cb_t original_refresh;
static void (*original_copy)(lv_draw_ctx_t*, void*, lv_coord_t, const lv_area_t*,
                             void*, lv_coord_t, const lv_area_t*);

static void copy(lv_draw_ctx_t* context, void* destination, lv_coord_t destination_stride,
                 const lv_area_t* destination_area, void* source, lv_coord_t source_stride,
                 const lv_area_t* source_area) {
    assert(!rendering); /* Direct-buffer synchronization precedes render_start. */
    ++copy_calls;
    copy_pixels += lv_area_get_size(destination_area);
    original_copy(context, destination, destination_stride, destination_area,
                  source, source_stride, source_area);
}

static void render_start(lv_disp_drv_t* driver) {
    (void)driver;
    assert(!rendering);
    rendering = true;
    ++renders;
}

static void flush(lv_disp_drv_t* driver, const lv_area_t* area, lv_color_t* colors) {
    (void)area;
    assert(rendering && (colors == first || colors == second));
    if (lv_disp_flush_is_last(driver)) {
        rendering = false;
        ++final_flushes;
    }
    lv_disp_flush_ready(driver);
}

static void refresh(lv_timer_t* timer) {
    assert(!rendering);
    const unsigned before = final_flushes;
    original_refresh(timer);
    assert(!rendering);
    if (final_flushes != before) ++refreshes;
    assert(refreshes == final_flushes && renders == final_flushes);
}

int main(void) {
    lv_init();
    lv_disp_draw_buf_t buffer;
    lv_disp_draw_buf_init(&buffer, first, second, W * H);
    lv_disp_drv_t driver;
    lv_disp_drv_init(&driver);
    driver.hor_res = W; driver.ver_res = H;
    driver.draw_buf = &buffer; driver.direct_mode = 1;
    driver.flush_cb = flush; driver.render_start_cb = render_start;
    lv_disp_t* display = lv_disp_drv_register(&driver);
    original_copy = driver.draw_ctx->buffer_copy;
    driver.draw_ctx->buffer_copy = copy;
    original_refresh = display->refr_timer->timer_cb;
    lv_timer_set_cb(display->refr_timer, refresh);
    lv_obj_t* screen = lv_disp_get_scr_act(display);
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    display->refr_timer->timer_cb(display->refr_timer);
    assert(final_flushes == 1 && copy_calls == 0);

    lv_area_t small = {4, 4, 13, 13};
    lv_obj_invalidate_area(screen, &small);
    display->refr_timer->timer_cb(display->refr_timer);
    assert(final_flushes == 2 && copy_calls > 0 && copy_pixels > 0 && copy_pixels < W * H);

    /* A repeated full viewport invalidation supersedes ALL previous sync
     * rectangles: the old dirty image need not be copied before redrawing it. */
    const unsigned copies_before_full = copy_calls;
    lv_obj_invalidate(screen);
    display->refr_timer->timer_cb(display->refr_timer);
    assert(final_flushes == 3 && copy_calls == copies_before_full);
    lv_obj_invalidate(screen);
    display->refr_timer->timer_cb(display->refr_timer);
    assert(final_flushes == 4 && copy_calls == copies_before_full);
    printf("LVGL8 profiling hook order passed: %u refresh/render/final-flush cycles; sync occurs before rendering and repeated full invalidation requires no buffer copy.\n", refreshes);
}
