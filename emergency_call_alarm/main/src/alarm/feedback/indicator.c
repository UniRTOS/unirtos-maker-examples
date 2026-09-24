#include "qosa_def.h"
#include "qosa_sys.h"
#include "qosa_audio.h"
#include "qosa_log.h"
#include "qosa_adc.h"
#include "qosa_gpio.h"
#include "qosa_power.h"
#include "qosa_pinctrl.h"
#include "qosa_pwm.h"
#include "qosa_tts.h"

#define QOS_LOG_TAG LOG_TAG_DEMO

#include "alarm_app.h"
#include "alarm_audio.h"
#include "alarm_config.h"
#include "alarm_feedback.h"

/* 蜂鸣器/振动仍为普通 GPIO 输出，未配置时保持 QOSA_GPIO_MAX。 */
static qosa_gpio_num_e g_buzzer_gpio = QOSA_GPIO_MAX;
static qosa_gpio_num_e g_vibration_gpio = QOSA_GPIO_MAX;

/* 状态灯使用 PWM 驱动（引脚 23 功能 5，对应 PWM1），实现真实硬件调光呼吸。 */
static qosa_bool_t g_led_pwm_ready = QOSA_FALSE;
static qosa_bool_t g_tts_ready = QOSA_FALSE;
static qosa_bool_t g_tts_play_pending = QOSA_FALSE;
static qosa_sem_t  g_tts_finish_sem = QOSA_NULL;

/**
 * @enum led_blink_mode_e
 * @brief 状态灯闪烁模式。
 */
typedef enum
{
    LED_BLINK_MODE_OFF = 0,   /*!< 常灭 */
    LED_BLINK_MODE_BREATH,    /*!< 缓慢呼吸（占空比三角波调光） */
    LED_BLINK_MODE_FAST,      /*!< 快速闪烁 */
    LED_BLINK_MODE_SOLID_ON,  /*!< 常亮 */
} led_blink_mode_e;

static qosa_timer_t     g_led_blink_timer = QOSA_NULL;
static led_blink_mode_e g_led_blink_mode = LED_BLINK_MODE_OFF;
static qosa_uint32_t    g_led_blink_elapsed_ms = 0;
static qosa_bool_t      g_led_blink_on = QOSA_FALSE;

/**
 * @brief 初始化单个输出引脚。
 *
 * @param[in] pin_num 逻辑引脚号。
 * @return qosa_gpio_num_e 有效 GPIO 号；失败返回 -1。
 */
static qosa_gpio_num_e init_output_pin(qosa_uint8_t pin_num)
{
    qosa_pin_cfg_t pin_cfg = {0};

    if (pin_num == ALARM_INVALID_PIN_NUM)
    {
        return -1;
    }

    if (qosa_get_pin_default_cfg(pin_num, &pin_cfg) != QOSA_GPIO_SUCCESS)
    {
        return -1;
    }

    (void)qosa_pin_set_func(pin_cfg.pin_num, pin_cfg.gpio_func);
    if (qosa_gpio_init(pin_cfg.gpio_num,
                       QOSA_GPIO_DIRECTION_OUTPUT,
                       QOSA_GPIO_PULL_NONE,
                       QOSA_GPIO_LEVEL_LOW) != QOSA_GPIO_SUCCESS)
    {
        return -1;
    }

    return pin_cfg.gpio_num;
}

/**
 * @brief 设置输出引脚电平。
 *
 * @param[in] gpio_num GPIO 号。
 * @param[in] enabled QOSA_TRUE 表示器件导通（蜂鸣器/马达为低有效，输出低电平）。
 * @return void
 */
static void set_output(qosa_gpio_num_e gpio_num, qosa_bool_t enabled)
{
    if (gpio_num == QOSA_GPIO_MAX || gpio_num == -1)
    {
        return;
    }

    (void)qosa_gpio_set_level(gpio_num, enabled ? QOSA_GPIO_LEVEL_LOW : QOSA_GPIO_LEVEL_HIGH);
}

/**
 * @brief 设置状态灯 PWM 占空比。
 *
 * @param[in] duty_count 0~ALARM_LED_PWM_PERIOD_COUNT 之间的高电平计数。
 * @return void
 */
static void set_led_duty(qosa_uint32_t duty_count)
{
    if (g_led_pwm_ready != QOSA_TRUE)
    {
        return;
    }

    if (duty_count == 0)
    {
        (void)qosa_pwm_disable(ALARM_STATUS_LED_PWM_SEL);
    }
    else
    {
        (void)qosa_pwm_enable(ALARM_STATUS_LED_PWM_SEL, duty_count);
    }
}

/**
 * @brief LED 效果定时器回调：呼吸模式按三角波调光，快闪模式按半周期翻转占空比。
 *
 * @param[in] argv 未使用。
 * @return void
 */
static void led_blink_timer_cb(void *argv)
{
    QOSA_UNUSED(argv);

    if (g_led_blink_mode == LED_BLINK_MODE_BREATH)
    {
        qosa_uint32_t half_cycle_ms = ALARM_LED_BREATH_CYCLE_MS / 2;
        qosa_uint32_t phase_ms = 0;
        qosa_uint32_t ramp_ms = 0;

        g_led_blink_elapsed_ms += ALARM_LED_TICK_MS;
        phase_ms = g_led_blink_elapsed_ms % ALARM_LED_BREATH_CYCLE_MS;
        ramp_ms = (phase_ms < half_cycle_ms) ? phase_ms : (ALARM_LED_BREATH_CYCLE_MS - phase_ms);
        set_led_duty((ramp_ms * ALARM_LED_PWM_PERIOD_COUNT) / half_cycle_ms);
    }
    else if (g_led_blink_mode == LED_BLINK_MODE_FAST)
    {
        g_led_blink_elapsed_ms += ALARM_LED_TICK_MS;
        if (g_led_blink_elapsed_ms >= ALARM_LED_FAST_BLINK_HALF_PERIOD_MS)
        {
            g_led_blink_elapsed_ms = 0;
            g_led_blink_on = (g_led_blink_on == QOSA_TRUE) ? QOSA_FALSE : QOSA_TRUE;
            set_led_duty((g_led_blink_on == QOSA_TRUE) ? ALARM_LED_PWM_PERIOD_COUNT : 0);
        }
    }
}

/**
 * @brief 切换状态灯闪烁模式（呼吸/快闪/常亮/关闭）。
 *
 * @param[in] mode 目标模式。
 * @return void
 * @note 呼吸与快闪依赖周期定时器驱动，其余模式直接设置占空比并停定时器。
 */
static void set_led_blink_mode(led_blink_mode_e mode)
{
    g_led_blink_mode = mode;
    g_led_blink_elapsed_ms = 0;

    if ((mode == LED_BLINK_MODE_BREATH) || (mode == LED_BLINK_MODE_FAST))
    {
        g_led_blink_on = QOSA_TRUE;
        set_led_duty((mode == LED_BLINK_MODE_BREATH) ? 0 : ALARM_LED_PWM_PERIOD_COUNT);

        if ((g_led_blink_timer != QOSA_NULL) && (qosa_timer_is_running(g_led_blink_timer) == QOSA_FALSE))
        {
            (void)qosa_timer_start(g_led_blink_timer, ALARM_LED_TICK_MS, QOSA_TRUE);
        }
    }
    else
    {
        if ((g_led_blink_timer != QOSA_NULL) && (qosa_timer_is_running(g_led_blink_timer) == QOSA_TRUE))
        {
            (void)qosa_timer_stop(g_led_blink_timer);
        }

        set_led_duty((mode == LED_BLINK_MODE_SOLID_ON) ? ALARM_LED_PWM_PERIOD_COUNT : 0);
    }
}

/**
 * @brief 将状态灯引脚复用为 PWM 功能并完成通道配置。
 *
 * @return void
 */
static void led_pwm_init(void)
{
    qosa_pwm_info_t pwm_info = {0};

    if (qosa_pin_set_func(ALARM_STATUS_LED_PIN_NUM, ALARM_STATUS_LED_PWM_FUNC) != QOSA_PINCTRL_SUCCESS)
    {
        QLOGW("[alarm] led pwm pin func set failed");
        return;
    }

    pwm_info.high_one_cycle_duration = 0;
    pwm_info.total_one_cycle_duration = ALARM_LED_PWM_PERIOD_COUNT;
    pwm_info.pwm_psc = ALARM_LED_PWM_PSC;
    pwm_info.clk_src = ALARM_LED_PWM_CLK_SRC;

    if (qosa_pwm_config(ALARM_STATUS_LED_PWM_SEL, &pwm_info) != QOSA_PWM_SUCESS)
    {
        QLOGW("[alarm] led pwm config failed");
        return;
    }

    g_led_pwm_ready = QOSA_TRUE;
}

/**
 * @brief TTS 播放事件回调。
 *
 * @param[in] event TTS 事件类型。
 * @param[in] data 事件数据，未使用。
 * @param[in] size 事件数据长度，未使用。
 * @return void
 * @note 仅在播放完成或被中断时释放完成信号量。
 */
static void tts_callback(qosa_tts_event_t event, qosa_uint8_t *data, qosa_uint32_t size)
{
    QOSA_UNUSED(data);
    QOSA_UNUSED(size);

    if ((event == QOSA_TTS_EVENT_PLAY_FINISH) ||
        (event == QOSA_TTS_EVENT_PLAY_INTERRUPT))
    {
        if (g_tts_finish_sem != QOSA_NULL)
        {
            (void)qosa_sem_release(g_tts_finish_sem);
        }
    }
}

/**
 * @brief 打开 TTS 通路。
 *
 * 恢复外部 Codec 音频路由、重新初始化 Codec 寄存器，再打开 TTS 引擎。
 *
 * @return int QOSA_OK 表示成功，-1 表示失败。
 * @note IMS 通话会切换音频路由，每次重新打开 TTS 前都需重新执行本流程。
 */
static int tts_open(void)
{
    qosa_aud_i2s_cfg_t i2s_cfg = {0, 0, 1, 1};
    qosa_tts_cfg_t     tts_cfg = {0};

    /* IMS 通话可能切换了音频路由，每次重开 TTS 前都恢复外部 Codec 输出。 */
    if (qosa_aud_output_ctrl(QOSA_AUDIO_OUTPUT_EXTERNAL, &i2s_cfg) != QOSA_AUD_SUCCESS)
    {
        QLOGW("[alarm] tts external audio route restore failed");
        return -1;
    }

    if (alarm_codec_init() != 0)
    {
        QLOGW("[alarm] tts codec prepare failed");
        return -1;
    }

    tts_cfg.langusge = QOSA_TTS_LANGUAGE_CHN;
    tts_cfg.callback = tts_callback;
    if (qosa_tts_open(&tts_cfg) != QOSA_AUD_SUCCESS)
    {
        QLOGW("[alarm] tts open failed");
        return -1;
    }

    g_tts_ready = QOSA_TRUE;
    return QOSA_OK;
}

/**
 * @brief 关闭 TTS 通路并复位就绪标志。
 *
 * @return void
 */
static void tts_close(void)
{
    if (g_tts_ready == QOSA_TRUE)
    {
        (void)qosa_tts_close(QOSA_FALSE);
        g_tts_ready = QOSA_FALSE;
    }
}

/**
 * @brief 创建 TTS 播放完成通知信号量。
 *
 * @return void
 * @note 此处只做同步原语初始化；Codec 音频通路在首次播放时由 tts_open() 建立。
 */
static void tts_init(void)
{
    if (qosa_sem_create(&g_tts_finish_sem, 0) != QOSA_OK)
    {
        QLOGW("[alarm] tts finish semaphore create failed");
        g_tts_finish_sem = QOSA_NULL;
        return;
    }
}

/**
 * @brief 初始化反馈外设。
 *
 * @return void
 */
void feedback_init(void)
{
    g_buzzer_gpio = init_output_pin(ALARM_BUZZER_PIN_NUM);
    g_vibration_gpio = init_output_pin(ALARM_VIBRATION_PIN_NUM);

    led_pwm_init();

    if (qosa_timer_create(&g_led_blink_timer, led_blink_timer_cb, QOSA_NULL) != QOSA_OK)
    {
        QLOGW("[alarm] led blink timer create failed");
        g_led_blink_timer = QOSA_NULL;
    }
}

/**
 * @brief 初始化 TTS 语音反馈。
 */
void feedback_tts_init(void)
{
    tts_init();
}

/**
 * @brief 异步播放中文反馈语音。
 *
 * @param[in] text UTF-8 文本。
 * @return void
 */
void feedback_notify_voice(const char *text)
{
    int ret = 0;

    if (text == QOSA_NULL)
    {
        return;
    }

    if (g_tts_ready == QOSA_FALSE)
    {
        if (tts_open() != QOSA_OK)
        {
            return;
        }
    }

    while (qosa_sem_wait(g_tts_finish_sem, QOSA_NO_WAIT) == QOSA_OK)
    {
    }

    ret = qosa_tts_play(QOSA_TTS_ENCODING_UTF8, text, qosa_strlen(text));
    g_tts_play_pending = (ret == QOSA_AUD_SUCCESS) ? QOSA_TRUE : QOSA_FALSE;
    if (ret != QOSA_AUD_SUCCESS)
    {
        QLOGW("[alarm] tts play failed");
    }
}

/**
 * @brief 等待当前反馈语音播放完成或超时。
 *
 * @return int QOSA_OK 表示播放完成/中断或本就没有在播，其他值表示等待超时。
 * @note 返回前会关闭 TTS 通路，释放音频路由给 IMS 通话使用。
 */
int feedback_wait_voice_finish(void)
{
    int ret = QOSA_OK;

    if (g_tts_ready == QOSA_FALSE)
    {
        return QOSA_OK;
    }

    if ((g_tts_finish_sem == QOSA_NULL) || (g_tts_play_pending == QOSA_FALSE))
    {
        tts_close();
        return QOSA_OK;
    }

    g_tts_play_pending = QOSA_FALSE;
    ret = qosa_sem_wait(g_tts_finish_sem, ALARM_TTS_PLAY_TIMEOUT_MS);
    tts_close();
    return ret;
}

/**
 * @brief 根据状态切换反馈表现。
 *
 * @param[in] state 报警状态。
 * @return void
 */
void feedback_apply_state(alarm_state_e state)
{
    switch (state)
    {
        case ALARM_STATE_IDLE:
            set_led_blink_mode(LED_BLINK_MODE_BREATH);
            set_output(g_buzzer_gpio, QOSA_FALSE);
            set_output(g_vibration_gpio, QOSA_FALSE);
            QLOGI("[alarm] state -> idle");
            break;

        case ALARM_STATE_TRIGGER_CONFIRM:
            QLOGI("[alarm] state -> trigger confirm");
            break;

        case ALARM_STATE_ALERT_PENDING:
            feedback_notify_voice(ALARM_TTS_TRIGGER_TEXT);
            set_led_blink_mode(LED_BLINK_MODE_FAST);
            set_output(g_buzzer_gpio, QOSA_TRUE);
            QLOGI("[alarm] state -> calling");
            break;

        case ALARM_STATE_CALLING:
        case ALARM_STATE_CALL_STOPPING:
            set_led_blink_mode(LED_BLINK_MODE_FAST);
            set_output(g_buzzer_gpio, QOSA_TRUE);
            QLOGI("[alarm] state -> calling");
            break;

        case ALARM_STATE_CONNECTED:
            set_led_blink_mode(LED_BLINK_MODE_SOLID_ON);
            set_output(g_buzzer_gpio, QOSA_FALSE);
            set_output(g_vibration_gpio, QOSA_TRUE);
            QLOGI("[alarm] state -> connected");
            break;

        case ALARM_STATE_FAILED:
            feedback_notify_voice(ALARM_TTS_FAILED_TEXT);
            (void)feedback_wait_voice_finish();
            set_led_blink_mode(LED_BLINK_MODE_OFF);
            set_output(g_buzzer_gpio, QOSA_TRUE);
            set_output(g_vibration_gpio, QOSA_FALSE);
            QLOGI("[alarm] state -> failed");
            break;

        default:
            break;
    }
}

/**
 * @brief 轮询电池状态并按阈值上报低电量事件。
 *
 * @return void
 */
void feedback_poll_battery(void)
{
    qosa_charge_status_e charge_status = 0;
    qosa_uint8_t         battery_percent = 0;
    qosa_uint32_t        voltage_mv = 0;
    int                  vbat_mv = 0;
    alarm_event_t        event = {0};

    if (qosa_power_get_charger_status(&charge_status, &battery_percent, &voltage_mv) != QOSA_POWER_SUCCESS)
    {
        return;
    }

    if (qosa_get_vbat_volt(&vbat_mv) == QOSA_ADC_SUCCESS)
    {
        voltage_mv = (qosa_uint32_t)vbat_mv;
    }

    /* 任一阈值命中即上报，兼顾电量百分比与电压波动场景。 */
    if ((battery_percent <= ALARM_LOW_BATTERY_PERCENT) ||
        (voltage_mv <= ALARM_LOW_BATTERY_MV))
    {
        event.type = ALARM_EVENT_LOW_BATTERY;
        event.data.battery.voltage_mv = voltage_mv;
        event.data.battery.percent = battery_percent;
        (void)alarm_post_event(&event);
    }
}

/**
 * @brief 输出低电量告警反馈。
 *
 * @param[in] voltage_mv 当前电压（mV）。
 * @param[in] percent 当前电量百分比。
 * @return void
 */
void feedback_notify_low_battery(qosa_uint32_t voltage_mv, qosa_uint8_t percent)
{
    QLOGW("[alarm] low battery warning: percent=%u voltage=%u", percent, voltage_mv);
    set_output(g_buzzer_gpio, QOSA_TRUE);
}
