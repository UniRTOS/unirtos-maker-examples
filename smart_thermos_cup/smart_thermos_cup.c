/*****************************************************************/ /**
 * @file smart_thermos_cup.c
 * @brief 智能保温杯：DHT11 测温，并在 GC9A01 圆屏上用 LVGL 显示；超限和定时喝水通过飞书提醒，喝水间隔可在触摸屏上设置
 *
 * 屏幕驱动见 lcd_gc9a01.c，传感器驱动见 dht_sensor.c，触摸驱动见 touch_cst816t.c。
 * 界面控件只在启动时创建一次，运行时仅更新文本与圆环颜色，避免 LVGL 48KB 堆碎片。
 **********************************************************************/
#include "app_config.h"

#include "qosa_def.h"
#include "qosa_sys.h"
#include "qosa_log.h"
#include "qosa_gpio.h"
#include "qosa_dev_eigen.h"

#define QOS_LOG_TAG   LOG_TAG_DEMO

#include "unirtos_app_init_registry.h"
#include "lvgl.h"
#include "lcd_gc9a01.h"
#include "dht_sensor.h"
#include "touch_cst816t.h"
#include "feishu_notify.h"
#include "feishu_config.h"

/* 主循环周期：同时作为 LVGL tick 步进 */
#define UI_POLL_INTERVAL_MS     50

/* 连续读取失败多少次后，在界面上提示传感器异常 */
#define DHT_FAIL_ALERT_COUNT    5

/* 每轮连续采样次数，取中位数作为结果，可剔除 (SAMPLE_COUNT-1)/2 个异常值。
   注意日志中打印了前 5 个原始值，修改此值需同步调整该日志 */
#define SAMPLE_COUNT            5

/* 连续采样、失败重试的间隔。DHT11 最小采样间隔为 1 秒，贴着 1 秒读可能传感器还没准备好，留些余量 */
#define SAMPLE_GAP_MS           1500

/* 圆环参数：直径 232，线宽 8，留 4px 边距给 240x240 圆屏 */
#define RING_SIZE               232
#define RING_WIDTH              8

/* 局部绘图缓冲区：240 x 40 行，单缓冲 */
#define LVGL_DRAW_BUF_LINES     40
static lv_color_t         s_draw_buf[LCD_WIDTH * LVGL_DRAW_BUF_LINES];
static lv_disp_draw_buf_t s_disp_buf;
static lv_disp_drv_t      s_disp_drv;

static lv_obj_t *s_ring;
static lv_obj_t *s_lbl_temp;
static lv_obj_t *s_lbl_status;
static lv_obj_t *s_lbl_remind;

/* 喝水提醒间隔（分钟，0 = 关闭），在设置页用 -/+ 按钮每次调整 REMIND_STEP_MIN 分钟 */
#define REMIND_STEP_MIN       5
#define REMIND_MAX_MIN        180

/* 设置页无操作多久后自动返回主界面 */
#define SET_IDLE_TIMEOUT_MS   15000
/* 触摸命中区域在按钮四周放宽的像素 */
#define SET_HIT_MARGIN        6

static int       s_remind_min = DRINK_REMIND_DEFAULT_MIN;
static lv_obj_t *s_page_set;
static lv_obj_t *s_lbl_set_value;
static lv_obj_t *s_lbl_set_unit;
static lv_obj_t *s_btn_minus;
static lv_obj_t *s_btn_plus;
static lv_obj_t *s_btn_ok;
static int       s_in_settings;
static uint32_t  s_set_idle_ms;
static uint32_t  s_since_drink_ms;

/* LVGL -> 屏幕：LV_COLOR_16_SWAP=0，缓冲区为小端 RGB565，而屏幕要求大端，写出前逐像素交换字节 */
static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    int32_t  count = (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1);
    uint8_t *bytes = (uint8_t *)color_p;

    for (int32_t i = 0; i < count; i++)
    {
        uint8_t tmp      = bytes[i * 2];
        bytes[i * 2]     = bytes[i * 2 + 1];
        bytes[i * 2 + 1] = tmp;
    }

    lcd_display((uint16_t)area->x1, (uint16_t)area->y1, (uint16_t)area->x2, (uint16_t)area->y2,
                bytes, (int)(count * 2));

    lv_disp_flush_ready(drv);
}

static void lvgl_disp_init(void)
{
    lv_disp_draw_buf_init(&s_disp_buf, s_draw_buf, QOSA_NULL, LCD_WIDTH * LVGL_DRAW_BUF_LINES);

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res  = LCD_WIDTH;
    s_disp_drv.ver_res  = LCD_HEIGHT;
    s_disp_drv.flush_cb = lvgl_flush_cb;
    s_disp_drv.draw_buf = &s_disp_buf;
    lv_disp_drv_register(&s_disp_drv);
}

static lv_obj_t *ui_label_create(lv_obj_t *parent, int y, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_obj_set_width(label, LCD_WIDTH);
    lv_obj_set_pos(label, 0, y);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_font(label, font, 0);

    return label;
}

static void ui_build(void)
{
    lv_obj_t *scr = lv_scr_act();

    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* 装饰圆环：颜色随温度变化，纯显示不响应触摸 */
    s_ring = lv_arc_create(scr);
    lv_obj_set_size(s_ring, RING_SIZE, RING_SIZE);
    lv_obj_set_pos(s_ring, (LCD_WIDTH - RING_SIZE) / 2, (LCD_HEIGHT - RING_SIZE) / 2);
    lv_obj_remove_style(s_ring, QOSA_NULL, LV_PART_KNOB | LV_STATE_ANY);
    lv_obj_clear_flag(s_ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_opa(s_ring, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_ring, RING_WIDTH, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(s_ring, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_ring, lv_color_hex(0x606060), LV_PART_INDICATOR);
    lv_arc_set_bg_angles(s_ring, 0, 359);
    lv_arc_set_angles(s_ring, 0, 359);

    lv_label_set_text(ui_label_create(scr, 78, &lv_font_montserrat_20, lv_color_hex(0xA0A0A0)), "TEMPERATURE");
    s_lbl_temp   = ui_label_create(scr, 108, &lv_font_montserrat_40, lv_color_white());
    s_lbl_status = ui_label_create(scr, 172, &lv_font_montserrat_14, lv_color_hex(0xFF5A3C));
    s_lbl_remind = ui_label_create(scr, 200, &lv_font_montserrat_14, lv_color_hex(0x707070));

    lv_label_set_text(s_lbl_temp, "--.- C");
    lv_label_set_text(s_lbl_status, "");
}

/* 创建一个带居中文字的圆角按钮（普通对象，不使用 LVGL 输入设备，命中由坐标判断） */
static lv_obj_t *ui_btn_create(lv_obj_t *parent, int x, int y, int w, int h, const char *text, const lv_font_t *font,
                               lv_color_t bg)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_t *lbl;

    lv_obj_set_size(btn, w, h);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_radius(btn, 14, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

    lbl = lv_label_create(btn);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);

    return btn;
}

/* 设置页：与主界面在启动时一起创建，平时隐藏 */
static void ui_build_settings(void)
{
    lv_obj_t *p;
    lv_obj_t *title;

    s_page_set = lv_obj_create(lv_scr_act());
    p          = s_page_set;
    lv_obj_set_size(p, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_set_pos(p, 0, 0);
    lv_obj_set_style_bg_color(p, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(p, 0, 0);
    lv_obj_set_style_radius(p, 0, 0);
    lv_obj_set_style_pad_all(p, 0, 0);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);

    title           = ui_label_create(p, 34, &lv_font_montserrat_20, lv_color_hex(0xA0A0A0));
    s_lbl_set_value = ui_label_create(p, 66, &lv_font_montserrat_40, lv_color_white());
    s_lbl_set_unit  = ui_label_create(p, 114, &lv_font_montserrat_14, lv_color_hex(0xA0A0A0));
    lv_label_set_text(title, "DRINK REMIND");
    lv_label_set_text(s_lbl_set_value, "60");
    lv_label_set_text(s_lbl_set_unit, "min");

    s_btn_minus = ui_btn_create(p, 28, 138, 76, 46, "-", &lv_font_montserrat_40, lv_color_hex(0x3A5F8F));
    s_btn_plus  = ui_btn_create(p, 136, 138, 76, 46, "+", &lv_font_montserrat_40, lv_color_hex(0x3A5F8F));
    s_btn_ok    = ui_btn_create(p, 84, 192, 72, 34, "OK", &lv_font_montserrat_20, lv_color_hex(0x27A98B));

    lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
}

/* 把当前间隔显示到设置页和主界面 */
static void ui_remind_refresh(void)
{
    int minutes = s_remind_min;

    if (minutes == 0)
    {
        lv_label_set_text(s_lbl_set_value, "OFF");
        lv_label_set_text(s_lbl_set_unit, "reminder off");
        lv_label_set_text(s_lbl_remind, "Drink: OFF");
    }
    else
    {
        lv_label_set_text_fmt(s_lbl_set_value, "%d", minutes);
        lv_label_set_text(s_lbl_set_unit, "min");
        lv_label_set_text_fmt(s_lbl_remind, "Drink: %d min", minutes);
    }
}

static void ui_settings_open(void)
{
    s_in_settings = 1;
    s_set_idle_ms = 0;
    lv_obj_clear_flag(s_page_set, LV_OBJ_FLAG_HIDDEN);
}

static void ui_settings_close(void)
{
    s_in_settings = 0;
    lv_obj_add_flag(s_page_set, LV_OBJ_FLAG_HIDDEN);
}

static int ui_point_in_obj(lv_obj_t *obj, int margin, const tp_event_t *ev)
{
    lv_area_t area;

    lv_obj_get_coords(obj, &area);
    return (int)ev->x >= area.x1 - margin && (int)ev->x <= area.x2 + margin && (int)ev->y >= area.y1 - margin &&
           (int)ev->y <= area.y2 + margin;
}

/* 间隔增减 delta 分钟（到 0 即关闭，到上限为止）；修改后重新开始计时 */
static void remind_step(int delta)
{
    int minutes = s_remind_min + delta;

    if (minutes < 0 || minutes > REMIND_MAX_MIN)
    {
        return;
    }
    s_remind_min     = minutes;
    s_since_drink_ms = 0;
    ui_remind_refresh();
    QLOGI("[smart_thermos_cup] drink remind interval = %d min", s_remind_min);
}

static void ui_handle_touch(const tp_event_t *ev)
{
    int is_tap = (ev->gesture == TP_GESTURE_CLICK || ev->gesture == TP_GESTURE_DCLICK);

    QLOGI("[smart_thermos_cup] touch gesture=0x%02x x=%d y=%d valid=%d", ev->gesture, ev->x, ev->y, ev->has_point);

    if (!s_in_settings)
    {
        /* 主界面：点击或上滑进入设置页 */
        if (is_tap || ev->gesture == TP_GESTURE_UP)
        {
            ui_settings_open();
        }
        return;
    }

    s_set_idle_ms = 0;

    if (ev->gesture == TP_GESTURE_DOWN)
    {
        ui_settings_close();
    }
    else if (is_tap && ev->has_point)
    {
        if (ui_point_in_obj(s_btn_minus, SET_HIT_MARGIN, ev))
        {
            remind_step(-REMIND_STEP_MIN);
        }
        else if (ui_point_in_obj(s_btn_plus, SET_HIT_MARGIN, ev))
        {
            remind_step(REMIND_STEP_MIN);
        }
        else if (ui_point_in_obj(s_btn_ok, SET_HIT_MARGIN, ev))
        {
            ui_settings_close();
        }
    }
}

/* 温度(0.1C) -> 圆环颜色：低温蓝、舒适青绿、偏高黄、过热橙红 */
static lv_color_t ui_temp_color(int16_t temp_x10)
{
    if (temp_x10 < 100)
    {
        return lv_color_hex(0x4A90D9);
    }
    if (temp_x10 < 250)
    {
        return lv_color_hex(0x27C4A3);
    }
    if (temp_x10 < 350)
    {
        return lv_color_hex(0xFFD166);
    }
    return lv_color_hex(0xFF5A3C);
}

static void ui_show_data(const dht_data_t *data)
{
    int     t    = data->temp_x10;
    int     abs_t = (t < 0) ? -t : t;

    lv_label_set_text_fmt(s_lbl_temp, "%s%d.%d C", (t < 0) ? "-" : "", abs_t / 10, abs_t % 10);
    lv_label_set_text(s_lbl_status, "");
    lv_obj_set_style_arc_color(s_ring, ui_temp_color(data->temp_x10), LV_PART_INDICATOR);
}

static void ui_show_error(void)
{
    lv_label_set_text(s_lbl_status, "SENSOR ERROR");
    lv_obj_set_style_arc_color(s_ring, lv_color_hex(0x606060), LV_PART_INDICATOR);
}

typedef enum
{
    TEMP_STATE_NORMAL = 0,
    TEMP_STATE_HIGH,
    TEMP_STATE_LOW,
} temp_state_e;

static temp_state_e s_temp_state = TEMP_STATE_NORMAL;
static uint32_t     s_since_alert_ms;
static int          s_first_report_sent;

/* 根据温度更新状态（带回差，避免在阈值附近反复触发） */
static temp_state_e alert_next_state(int16_t t)
{
    if (t >= ALERT_HIGH_TEMP_X10)
    {
        return TEMP_STATE_HIGH;
    }
    if (t <= ALERT_LOW_TEMP_X10)
    {
        return TEMP_STATE_LOW;
    }
    if (s_temp_state == TEMP_STATE_HIGH && t > ALERT_HIGH_TEMP_X10 - ALERT_HYSTERESIS_X10)
    {
        return TEMP_STATE_HIGH;
    }
    if (s_temp_state == TEMP_STATE_LOW && t < ALERT_LOW_TEMP_X10 + ALERT_HYSTERESIS_X10)
    {
        return TEMP_STATE_LOW;
    }
    return TEMP_STATE_NORMAL;
}

static void alert_push(const char *title, const dht_data_t *data)
{
    char msg[FEISHU_TEXT_MAX];
    int  t     = data->temp_x10;
    int  abs_t = (t < 0) ? -t : t;

    qosa_snprintf(msg, sizeof(msg), "%s 当前温度 %s%d.%d℃", title, (t < 0) ? "-" : "", abs_t / 10, abs_t % 10);
    feishu_notify_send(msg);
}

/* 每次成功读到温度后调用：上线通知、越限提醒、恢复通知；持续越限时按冷却时间重复提醒 */
static void alert_check(const dht_data_t *data)
{
    temp_state_e next = alert_next_state(data->temp_x10);

    if (!s_first_report_sent)
    {
        s_first_report_sent = 1;
        alert_push("[智能保温杯] 已上线。", data);
    }

    if (next != s_temp_state)
    {
        s_temp_state     = next;
        s_since_alert_ms = 0;

        if (next == TEMP_STATE_HIGH)
        {
            alert_push("[智能保温杯] 水温过高，小心烫嘴，请降温后饮用", data);
        }
        else if (next == TEMP_STATE_LOW)
        {
            alert_push("[智能保温杯] 水温偏低，建议更换温水", data);
        }
        else
        {
            alert_push("[智能保温杯] 温度已恢复正常。", data);
        }
    }
    else if (s_temp_state != TEMP_STATE_NORMAL && s_since_alert_ms >= ALERT_REPEAT_INTERVAL_MS)
    {
        s_since_alert_ms = 0;
        alert_push(s_temp_state == TEMP_STATE_HIGH ? "[智能保温杯] 温度仍然过高！" : "[智能保温杯] 温度仍然过低！", data);
    }

    /* 喝水提醒：到点发送，并附带当前温度，方便判断水温是否合适；间隔为 0 表示关闭 */
    if (s_remind_min != 0 && s_since_drink_ms >= (uint32_t)s_remind_min * 60u * 1000u)
    {
        char msg[FEISHU_TEXT_MAX];
        int  t     = data->temp_x10;
        int  abs_t = (t < 0) ? -t : t;

        s_since_drink_ms = 0;
        qosa_snprintf(msg, sizeof(msg), "[智能保温杯] 叮咚！记得喝水，别让身体缺水啦！ 当前温度 %s%d.%d℃", (t < 0) ? "-" : "", abs_t / 10,
                      abs_t % 10);
        feishu_notify_send(msg);
    }
}

/* 对 n 个值排序后取中位数（单次坐标误差的异常值会被自动剔除） */
static int16_t median_i16(const int16_t *src, int n)
{
    int16_t v[SAMPLE_COUNT];

    for (int i = 0; i < n; i++)
    {
        int16_t key = src[i];
        int     j   = i - 1;

        while (j >= 0 && v[j] > key)
        {
            v[j + 1] = v[j];
            j--;
        }
        v[j + 1] = key;
    }

    return v[n / 2];
}

static void thermos_task(void *argv)
{
    dht_data_t data;
    int16_t    temp_buf[SAMPLE_COUNT];
    int        sample_cnt = 0;
    uint32_t   elapsed_ms = 0;
    int        fail_cnt   = 0;
    int        ret;
    tp_event_t tp_ev;

    (void)argv;

    for (;;)
    {
        elapsed_ms += UI_POLL_INTERVAL_MS;
        if (s_since_alert_ms < 0xFFFFFF00u)
        {
            s_since_alert_ms += UI_POLL_INTERVAL_MS;
        }
        if (s_since_drink_ms < 0xFFFFFF00u)
        {
            s_since_drink_ms += UI_POLL_INTERVAL_MS;
        }
        if (elapsed_ms >= DHT_MIN_INTERVAL_MS)
        {
            elapsed_ms = 0;

            ret = dht_read(&data);
            if (ret == 0)
            {
                fail_cnt = 0;
                temp_buf[sample_cnt] = data.temp_x10;
                sample_cnt++;

                if (sample_cnt < SAMPLE_COUNT)
                {
                    /* 继续采样 */
                    elapsed_ms = DHT_MIN_INTERVAL_MS - SAMPLE_GAP_MS;
                }
                else
                {
                    /* 一组采样完成：取中位数作为本轮结果，剔除异常值 */
                    data.temp_x10 = median_i16(temp_buf, SAMPLE_COUNT);
                    sample_cnt    = 0;

                    QLOGI("[smart_thermos_cup] temp=%d.%d (raw t: %d %d %d %d %d)", data.temp_x10 / 10,
                          (data.temp_x10 < 0 ? -data.temp_x10 : data.temp_x10) % 10, temp_buf[0], temp_buf[1], temp_buf[2],
                          temp_buf[3], temp_buf[4]);
                    ui_show_data(&data);
                    alert_check(&data);
                }
            }
            else
            {
                QLOGE("[smart_thermos_cup] dht read failed: %d", ret);
                /* 偶发的读取失败无需等满周期，稍后重试 */
                elapsed_ms = DHT_MIN_INTERVAL_MS - SAMPLE_GAP_MS;
                if (++fail_cnt >= DHT_FAIL_ALERT_COUNT)
                {
                    fail_cnt   = DHT_FAIL_ALERT_COUNT;
                    sample_cnt = 0; /* 传感器持续异常，丢弃已采样的旧数据 */
                    ui_show_error();
                }
            }
        }

        if (tp_poll(&tp_ev, UI_POLL_INTERVAL_MS))
        {
            ui_handle_touch(&tp_ev);
        }
        if (s_in_settings)
        {
            s_set_idle_ms += UI_POLL_INTERVAL_MS;
            if (s_set_idle_ms >= SET_IDLE_TIMEOUT_MS)
            {
                ui_settings_close();
            }
        }

        lv_tick_inc(UI_POLL_INTERVAL_MS);
        lv_timer_handler();
        qosa_task_sleep_ms(UI_POLL_INTERVAL_MS);
    }
}

static void smart_thermos_cup_init(void)
{
    qosa_task_t task_ref = QOSA_NULL;

    qosa_gpio_set_voltage(VOL_3_30V);

    lcd_init();

    lv_init();
    lvgl_disp_init();
    ui_build();
    ui_build_settings();
    ui_remind_refresh();

    if (tp_init() != 0)
    {
        QLOGE("[smart_thermos_cup] touch init failed, drink interval cannot be configured");
    }

    if (dht_init() != 0)
    {
        QLOGE("[smart_thermos_cup] dht init failed");
    }

    if (feishu_notify_init() != 0)
    {
        QLOGE("[smart_thermos_cup] feishu init failed");
    }

    /* LVGL 渲染较耗栈，与 small-screen-demo 保持一致使用 12288 */
    if (qosa_task_create(&task_ref, 12288, QOSA_PRIORITY_NORMAL, "thermos", thermos_task, QOSA_NULL) != 0)
    {
        QLOGE("[smart_thermos_cup] thermos task create failed");
    }
}

UNIRTOS_APP_EXPORT(700, "smart_thermos_cup", smart_thermos_cup_init);
