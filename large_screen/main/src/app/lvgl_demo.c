#include "qosa_def.h"
#include "qosa_lcd.h"
#include "qosa_log.h"
#include "qosa_pinctrl.h"
#include "qosa_sys.h"
#include "unirtos_app_init_registry.h"

#include "lvgl.h"
#include "lvgl_home.h"

#define QOS_LOG_TAG                LOG_TAG_LCD_API
#define UNIR_LVGL_LCD_PORT          QOSA_LCD_SPECIAL_PORT
#define UNIR_LVGL_LCD_RST_PIN       49
#define UNIR_LVGL_LCD_RST_FUNC      0
#define UNIR_LVGL_LCD_DS_PIN        51
#define UNIR_LVGL_LCD_DS_FUNC       2
#define UNIR_LVGL_LCD_CS_PIN        52
#define UNIR_LVGL_LCD_CS_FUNC       1
#define UNIR_LVGL_LCD_DOUT_PIN      50
#define UNIR_LVGL_LCD_DOUT_FUNC     1
#define UNIR_LVGL_LCD_CLK_PIN       53
#define UNIR_LVGL_LCD_CLK_FUNC      1
#define UNIR_LVGL_TASK_STACK_SIZE   (12U * 1024U)
#define UNIR_LVGL_TASK_PRIORITY     4
#define UNIR_LVGL_TICK_MS           5

extern qosa_lcd_drv_cfg_t g_unir_lcd_drv_st7796s_info;
qosa_bool_t unir_lvgl_port_display_init(void);
void unir_lvgl_port_paint_base(void);

static qosa_task_t g_unir_lvgl_task = QOSA_NULL;
static qosa_timer_t g_unir_lvgl_tick_timer = QOSA_NULL;

static void unir_lvgl_tick_timer(void *context)
{
    (void)context;
    lv_tick_inc(UNIR_LVGL_TICK_MS);
}

static void unir_lvgl_demo_process(void *ctx)
{
    qosa_lcd_error_e ret;
    lv_mem_monitor_t memory;
    lv_obj_t *startup_screen;
    qosa_bool_t timer_tick = QOSA_FALSE;

    (void)ctx;
    qosa_task_sleep_ms(3000);
    qosa_pin_set_func(UNIR_LVGL_LCD_RST_PIN, UNIR_LVGL_LCD_RST_FUNC);
    qosa_pin_set_func(UNIR_LVGL_LCD_CS_PIN, UNIR_LVGL_LCD_CS_FUNC);
    qosa_pin_set_func(UNIR_LVGL_LCD_DS_PIN, UNIR_LVGL_LCD_DS_FUNC);
    qosa_pin_set_func(UNIR_LVGL_LCD_DOUT_PIN, UNIR_LVGL_LCD_DOUT_FUNC);
    qosa_pin_set_func(UNIR_LVGL_LCD_CLK_PIN, UNIR_LVGL_LCD_CLK_FUNC);
    ret = qosa_lcd_ioctl(UNIR_LVGL_LCD_PORT, QOSA_LCD_IOCTL_SET_DRV_CFG, &g_unir_lcd_drv_st7796s_info);
    if (ret != QOSA_LCD_SUCCESS) {
        QLOGE("Unable to configure ST7796S: %d", ret);
        return;
    }
    ret = qosa_lcd_init(UNIR_LVGL_LCD_PORT);
    if (ret != QOSA_LCD_SUCCESS) {
        QLOGE("Unable to initialize ST7796S: %d", ret);
        return;
    }
    lv_init();
    if (qosa_timer_create(&g_unir_lvgl_tick_timer, unir_lvgl_tick_timer, QOSA_NULL) == QOSA_ERROR_OK) {
        if (qosa_timer_start(g_unir_lvgl_tick_timer, UNIR_LVGL_TICK_MS, QOSA_TRUE) == QOSA_ERROR_OK) {
            timer_tick = QOSA_TRUE;
        } else {
            qosa_timer_delete(g_unir_lvgl_tick_timer);
            g_unir_lvgl_tick_timer = QOSA_NULL;
        }
    }
    if (!timer_tick) QLOGE("LVGL tick timer unavailable; using task tick fallback");
    if (unir_lvgl_port_display_init() == QOSA_FALSE) {
        if (timer_tick) {
            qosa_timer_stop(g_unir_lvgl_tick_timer);
            qosa_timer_delete(g_unir_lvgl_tick_timer);
            g_unir_lvgl_tick_timer = QOSA_NULL;
        }
        QLOGE("Unable to create LVGL DMA draw buffer");
        return;
    }
    unir_lvgl_port_paint_base();
    startup_screen = lv_scr_act();
    unir_home_create();
    lv_obj_del(startup_screen);
    lv_obj_invalidate(lv_scr_act());
    lv_refr_now(QOSA_NULL);
    lv_mem_monitor(&memory);
    QLOGI("Home screen initialized, LVGL free=%lu largest=%lu",
          (unsigned long)memory.free_size, (unsigned long)memory.free_biggest_size);
    for (;;) {
        if (!timer_tick) lv_tick_inc(UNIR_LVGL_TICK_MS);
        (void)lv_timer_handler();
        qosa_task_sleep_ms(UNIR_LVGL_TICK_MS);
    }
}

void unir_lvgl_demo_init(void)
{
    if (g_unir_lvgl_task == QOSA_NULL) {
        qosa_task_create(&g_unir_lvgl_task,
                         UNIR_LVGL_TASK_STACK_SIZE,
                         UNIR_LVGL_TASK_PRIORITY,
                         "lvgl_demo",
                         unir_lvgl_demo_process,
                         QOSA_NULL,
                         1);
        QLOGI("LVGL demo task created");
    }
}

UNIRTOS_APP_EXPORT(327, "lvgl_demo", unir_lvgl_demo_init);