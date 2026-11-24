#include "qosa_def.h"
#include "qosa_lcd.h"
#include "qosa_log.h"
#include "qosa_sys.h"

#include "lvgl.h"
#include "lvgl_touch_ft6336.h"

#define UNIR_LVGL_LCD_PORT         QOSA_LCD_SPECIAL_PORT
#define UNIR_LVGL_WIDTH            480
#define UNIR_LVGL_HEIGHT           320
#define UNIR_LVGL_DRAW_BUF_LINES   40

static lv_color_t *g_unir_lvgl_draw_buf;
static lv_disp_draw_buf_t g_unir_lvgl_disp_buf;
static lv_disp_drv_t g_unir_lvgl_disp_drv;

void unir_lvgl_port_paint_base(void)
{
    qosa_uint16_t y;
    qosa_uint32_t i;

    if (g_unir_lvgl_draw_buf == QOSA_NULL) {
        QOSA_LOG_E(LOG_TAG_LCD_API, "LVGL DMA draw buffer is null");
        return;
    }

    for (i = 0; i < (UNIR_LVGL_WIDTH * UNIR_LVGL_DRAW_BUF_LINES); ++i) {
        g_unir_lvgl_draw_buf[i].full = 0x1084U;
    }

    for (y = 0; y < UNIR_LVGL_HEIGHT; y += UNIR_LVGL_DRAW_BUF_LINES) {
        qosa_lcd_error_e ret = qosa_lcd_write(UNIR_LVGL_LCD_PORT,
                                               (qosa_uint8_t *)g_unir_lvgl_draw_buf,
                                               0U,
                                               y,
                                               UNIR_LVGL_WIDTH - 1U,
                                               y + UNIR_LVGL_DRAW_BUF_LINES - 1U);
        if (ret != QOSA_LCD_SUCCESS) {
            QOSA_LOG_E(LOG_TAG_LCD_API, "LVGL base paint failed at y=%u: %d", y, ret);
            return;
        }
        qosa_task_sleep_ms(5U);
    }
}

static void unir_lvgl_flush_cb(lv_disp_drv_t *disp_drv,
                               const lv_area_t *area,
                               lv_color_t *color_p)
{
    qosa_lcd_error_e ret;

    ret = qosa_lcd_write(UNIR_LVGL_LCD_PORT,
                         (qosa_uint8_t *)color_p,
                         (qosa_uint16_t)area->x1,
                         (qosa_uint16_t)area->y1,
                         (qosa_uint16_t)area->x2,
                         (qosa_uint16_t)area->y2);
    if (ret != QOSA_LCD_SUCCESS) {
        QOSA_LOG_E(LOG_TAG_LCD_API, "LVGL flush failed: %d", ret);
    }

    qosa_task_sleep_ms(1U);
    lv_disp_flush_ready(disp_drv);
}

qosa_bool_t unir_lvgl_port_display_init(void)
{
    if (g_unir_lvgl_draw_buf == QOSA_NULL) {
        g_unir_lvgl_draw_buf = (lv_color_t *)qosa_malloc(UNIR_LVGL_WIDTH *
                                                          UNIR_LVGL_DRAW_BUF_LINES *
                                                          sizeof(lv_color_t));
    }
    if (g_unir_lvgl_draw_buf == QOSA_NULL) {
        QOSA_LOG_E(LOG_TAG_LCD_API, "Unable to allocate LVGL LSPI draw buffer");
        return QOSA_FALSE;
    }

    lv_disp_draw_buf_init(&g_unir_lvgl_disp_buf,
                          g_unir_lvgl_draw_buf,
                          QOSA_NULL,
                          UNIR_LVGL_WIDTH * UNIR_LVGL_DRAW_BUF_LINES);
    lv_disp_drv_init(&g_unir_lvgl_disp_drv);
    g_unir_lvgl_disp_drv.hor_res = UNIR_LVGL_WIDTH;
    g_unir_lvgl_disp_drv.ver_res = UNIR_LVGL_HEIGHT;
    g_unir_lvgl_disp_drv.flush_cb = unir_lvgl_flush_cb;
    g_unir_lvgl_disp_drv.draw_buf = &g_unir_lvgl_disp_buf;
    (void)lv_disp_drv_register(&g_unir_lvgl_disp_drv);

    if (unir_lvgl_touch_init() == QOSA_FALSE) {
        QOSA_LOG_E(LOG_TAG_LCD_API,
                   "LVGL touch initialization failed; display will continue without touch");
    }
    return QOSA_TRUE;
}
