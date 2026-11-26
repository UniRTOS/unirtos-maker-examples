#include <stdbool.h>
#include <stdint.h>
#include "si4731.h"
#include "qosa_def.h"
#include "qosa_sys.h"
#include "qosa_log.h"
#include "qosa_rtc.h"
#include "qosa_gpio.h"

#include "radio_event.h"
#include "radio_ui.h"
#include "radio_demo.h"
#include "unirtos_app_init_registry.h"

#define QOS_LOG_TAG LOG_TAG_DEMO
#define FM_MAX_STATIONS 16

#define SI4731_TASK_STACK_SIZE 8192U

#define BUTTON_MODE_FREQUENCY 0
#define BUTTON_MODE_VOLUME 1
/* Keep audio muted until the knob has been still this long after a tuning step. */
#define RADIO_TUNE_UNMUTE_DELAY_MS 300U

static qosa_task_t s_si4731_task = QOSA_NULL;
static qosa_msgq_t s_evt_msgq = QOSA_NULL;
static qosa_timer_t s_fm_timer_get_rsq_status = QOSA_NULL;
static uint32_t s_fm_cur_freq_10khz;    // Current FM frequency in 10 kHz units （SI4731_FM_BAND_BOTTOM-SI4731_FM_BAND_TOP）
static uint32_t s_fm_target_freq_10khz; // Target FM frequency in 10 kHz units （SI4731_FM_BAND_BOTTOM-SI4731_FM_BAND_TOP）
static uint8_t s_fm_cur_volume;         // FM volume (0-63)
static uint8_t s_fm_target_volume;      // FM target volume (0-63)
static bool s_fm_button_mode;           // BUTTON_MODE_FREQUENCY- frequency mode, BUTTON_MODE_VOLUME- volume mode

static si4731_tune_status_noresp_t s_tune_stations[FM_MAX_STATIONS];

static si4731_rsq_status_t s_rsq; // Last signal quality of s_fm_cur_freq_10khz
static bool s_stereo_known;       // stereo has been read from RSQ for the current frequency
static bool s_rsq_ok;             // s_rsq holds real data
static bool s_scanning;           // Initial band scan in progress
static bool s_tune_muted;         // Audio muted while the knob changes frequency
static uint32_t s_last_tune_tick; // System tick of the last frequency knob step

/* Signal data from a tune result; stereo is unknown until the next RSQ poll. */
static void radio_rsq_from_tune(const si4731_tune_status_t *status)
{
    s_rsq.valid = (status->resp1 & SI4731_RESP1_VALID) != 0U;
    s_rsq.stereo = false;
    s_stereo_known = false;
    s_rsq.rssi_dBuV = status->rssi_dBuV;
    s_rsq.snr_dB = status->snr_dB;
    s_rsq.multipath = 0U;
    s_rsq_ok = true;
}

/* Copy the radio state into the UI. Only the control task calls this, so only it touches the LCD. */
static void radio_ui_refresh(void)
{
    radio_ui_state_t ui;
    qosa_uint32_t pending = 0U;

    /* More events already queued (fast knob turning): skip, the last one redraws the final value. */
    if ((qosa_msgq_get_cnt(s_evt_msgq, &pending) == QOSA_ERROR_OK) && (pending > 0U))
    {
        return;
    }

    ui.freq_10khz = (uint16_t)s_fm_target_freq_10khz;
    ui.volume = s_fm_target_volume;
    ui.volume_mode = (s_fm_button_mode == BUTTON_MODE_VOLUME);
    ui.scanning = s_scanning;
    ui.tuning = (s_fm_cur_freq_10khz != s_fm_target_freq_10khz);
    ui.rsq_ok = s_rsq_ok;
    ui.valid = s_rsq.valid;
    ui.stereo_known = s_stereo_known;
    ui.stereo = s_rsq.stereo;
    ui.rssi_dBuV = s_rsq.rssi_dBuV;
    ui.snr_dB = s_rsq.snr_dB;
    radio_ui_update(&ui);
}

static void ec11_event_handler(const ec11_event_type_t *evt)
{

    if (evt == NULL)
    {
        return;
    }
    ec11_event_type_t evt_type = *evt;
    if (evt_type == EC11_EVT_ROTATE_CW || evt_type == EC11_EVT_ROTATE_CCW || evt_type == EC11_EVT_BUTTON_PRESS)
    {

        radio_evt_t radio_evt = {
            .src = RADIO_EVT_SRC_EC11,
            .u.ec11 = evt_type};
        qosa_msgq_release(s_evt_msgq, sizeof(radio_evt), (qosa_uint8_t*)&radio_evt, QOSA_NO_WAIT);
    }
}
static void fm_timer_get_rsq_status_callback(void *ctx)
{
    (void)ctx;
    radio_evt_t radio_evt = {
        .src = RADIO_EVT_SRC_TIMER,
        .u.timer = RADIO_TIMER_RSQ_POLL
    };
    qosa_msgq_release(s_evt_msgq, sizeof(radio_evt), (qosa_uint8_t*)&radio_evt, QOSA_NO_WAIT);
}
/* Mute on the first tuning step so stepping through empty channels is silent. */
static void radio_mute_for_tuning(void)
{
    s_last_tune_tick = (uint32_t)qosa_get_system_tick_cnt();
    if (!s_tune_muted)
    {
        if (si4731_set_mute(true))
        {
            s_tune_muted = true;
        }
        else
        {
            QLOGE("[radio_demo] Failed to mute while tuning");
        }
    }
}

static void radio_handle_event_ec11(const ec11_event_type_t evt)
{
    if (evt == EC11_EVT_ROTATE_CW)
    {
        // Handle clockwise rotation
        if (s_fm_button_mode == BUTTON_MODE_FREQUENCY)
        {
            s_fm_target_freq_10khz = (s_fm_target_freq_10khz < SI4731_FM_BAND_TOP) ? (s_fm_target_freq_10khz + 10U) : SI4731_FM_BAND_TOP;
            radio_mute_for_tuning();
            QLOGI("[radio_demo] Set target frequency %u.%02u MHz", (unsigned int)(s_fm_target_freq_10khz / 100U), (unsigned int)(s_fm_target_freq_10khz % 100U));
        }
        else if (s_fm_button_mode == BUTTON_MODE_VOLUME)
        {
            s_fm_target_volume = (s_fm_target_volume < 63U) ? (s_fm_target_volume + 1U) : 63U;
            QLOGI("[radio_demo] Set target volume %u", (unsigned int)s_fm_target_volume);
        }
    }

    else if (evt == EC11_EVT_ROTATE_CCW)
    {
        // Handle counter-clockwise rotation
        if (s_fm_button_mode == BUTTON_MODE_FREQUENCY)
        {
            s_fm_target_freq_10khz = (s_fm_target_freq_10khz > SI4731_FM_BAND_BOTTOM) ? (s_fm_target_freq_10khz - 10U) : SI4731_FM_BAND_BOTTOM;
            radio_mute_for_tuning();
            QLOGI("[radio_demo] Set target frequency %u.%02u MHz", (unsigned int)(s_fm_target_freq_10khz / 100U), (unsigned int)(s_fm_target_freq_10khz % 100U));
        }
        else if (s_fm_button_mode == BUTTON_MODE_VOLUME)
        {
            s_fm_target_volume = (s_fm_target_volume > 0U) ? (s_fm_target_volume - 1U) : 0U;
            QLOGI("[radio_demo] Set target volume %u", (unsigned int)s_fm_target_volume);
        }
    }
    else if (evt == EC11_EVT_BUTTON_PRESS)
    {
        // Handle button press
        s_fm_button_mode = (s_fm_button_mode == BUTTON_MODE_FREQUENCY) ? BUTTON_MODE_VOLUME : BUTTON_MODE_FREQUENCY;
        QLOGI("[radio_demo] Button mode changed to %s", (s_fm_button_mode == BUTTON_MODE_FREQUENCY) ? "FREQUENCY" : "VOLUME");
    }
}
static void radio_handle_event_timer(const radio_timer_id_t evt)
{
    si4731_rsq_status_t rsq_status;
    (void)evt;
    if(si4731_fm_rsq_status(&rsq_status))
    {
        s_rsq = rsq_status;
        s_stereo_known = true;
        s_rsq_ok = true;
        QLOGI("[radio_demo] RSQ Status: valid=0x%02X stereo=%u RSSI=%u dBuV SNR=%u dB multipath=%u",
              (unsigned int)rsq_status.valid , (unsigned int)rsq_status.stereo,
              (unsigned int)rsq_status.rssi_dBuV, (unsigned int)rsq_status.snr_dB,
              (unsigned int)rsq_status.multipath);
    }
}
/* Move the dial along with the band scan so the final station needs only a small redraw. */
static void radio_scan_progress(uint16_t freq_10khz)
{
    s_fm_cur_freq_10khz = freq_10khz;
    s_fm_target_freq_10khz = freq_10khz;
    radio_ui_refresh();
}

static void radio_task_entry(void *ctx)
{
    (void)ctx;

    s_scanning = true;
    if (radio_ui_init())
    {
        radio_ui_refresh();
        qosa_task_sleep_ms(5000); /* Give EC11 task some time to initialize */
    }
    else
    {
        QLOGE("[radio_demo] LCD init failed, radio runs without display");
    }

    if (!si4731_init())
    {
        QLOGE("[radio_demo] SI4731 init failed");
        for (;;)
        {
            qosa_task_sleep_ms(60000U);
        }
    }
    int i;
    si4731_tune_status_t status;
    si4731_tune_status_noresp_t *stations = s_tune_stations;
    int count = si4731_fm_scan(stations, FM_MAX_STATIONS, radio_scan_progress);
    if (count == 0U)
    {
        QLOGE("[radio_demo] FM scan found no valid station; staying muted. Check antenna on FMI or move near a window");
        goto exit;
    }

    for (i = 0U; i < count; i++)
    {
        if (!si4731_fm_tune(stations[i].freq_10khz, &status))
        {
            goto exit;
        }
        if ((status.resp1 & SI4731_RESP1_VALID) != 0U)
        {
            break;
        }
        QLOGW("[radio_demo] FM %u.%02u MHz not valid after retune (RSSI=%u SNR=%u), trying next",
              (unsigned int)(status.freq_10khz / 100U), (unsigned int)(status.freq_10khz % 100U),
              (unsigned int)status.rssi_dBuV, (unsigned int)status.snr_dB);
    }
    if (i == count)
    {
        QLOGE("[radio_demo] FM no station stayed valid after retune; staying muted");
        goto exit;
    }
    else
    {
        if (!si4731_set_mute(false))
        {
            goto exit;
        }

        QLOGI("[radio_demo] FM playing %u.%02u MHz volume=%u/63 RSSI=%u dBuV SNR=%u dB",
              (unsigned int)(status.freq_10khz / 100U), (unsigned int)(status.freq_10khz % 100U),
              (unsigned int)s_fm_cur_volume, (unsigned int)status.rssi_dBuV,
              (unsigned int)status.snr_dB);

        s_fm_cur_freq_10khz = status.freq_10khz;
        s_fm_target_freq_10khz = status.freq_10khz;
        radio_rsq_from_tune(&status);
    }
    s_scanning = false;
    radio_ui_refresh();

    qosa_timer_start(s_fm_timer_get_rsq_status, 1000U,QOSA_TRUE); // Start RSQ status timer with 1 second interval

    while (1)
    {
        radio_evt_t evt;
        qosa_errcode_os_e ret = qosa_msgq_wait(s_evt_msgq, (qosa_uint8_t*)&evt, sizeof(radio_evt_t), 20U);
        if (ret == QOSA_ERROR_OK)
        {
            if (evt.src == RADIO_EVT_SRC_EC11)
            {
                radio_handle_event_ec11(evt.u.ec11);
            }
            else if (evt.src == RADIO_EVT_SRC_TIMER)
            {
                radio_handle_event_timer(evt.u.timer);
            }
        }
        else if (ret == QOSA_ERROR_MSGQ_TIMEOUT_ERR)
        {
            if (s_fm_cur_freq_10khz != s_fm_target_freq_10khz)
            {
                if (!si4731_fm_tune(s_fm_target_freq_10khz, &status))
                {
                    QLOGE("[radio_demo] Failed to tune to target frequency %u.%02u MHz", (unsigned int)(s_fm_target_freq_10khz / 100U), (unsigned int)(s_fm_target_freq_10khz % 100U));
                    continue;
                }
                s_fm_cur_freq_10khz = s_fm_target_freq_10khz;
                radio_rsq_from_tune(&status);
                QLOGI("[radio_demo] Tuned to target frequency %u.%02u MHz", (unsigned int)(s_fm_cur_freq_10khz / 100U), (unsigned int)(s_fm_cur_freq_10khz % 100U));
            }
            if (s_tune_muted && s_fm_cur_freq_10khz == s_fm_target_freq_10khz &&
                (uint32_t)qosa_get_system_tick_cnt() - s_last_tune_tick >= RADIO_TUNE_UNMUTE_DELAY_MS)
            {
                if (si4731_set_mute(false))
                {
                    s_tune_muted = false;
                }
                else
                {
                    QLOGE("[radio_demo] Failed to unmute after tuning");
                }
            }
            if (s_fm_cur_volume != s_fm_target_volume)
            {
                if (!si4731_set_volume(s_fm_target_volume))
                {
                    QLOGE("[radio_demo] Failed to set target volume %u", (unsigned int)s_fm_target_volume);
                    continue;
                }
                s_fm_cur_volume = s_fm_target_volume;
                QLOGI("[radio_demo] Set target volume %u", (unsigned int)s_fm_cur_volume);
            }
        }
        else
        {
            QLOGE("[radio_demo] Failed to wait for EC11 event, error=%d", ret);
        }
        radio_ui_refresh();
        radio_ui_process();
    }

exit:
    s_scanning = false;
    radio_ui_refresh();
    while (1)
    {
        qosa_task_sleep_ms(60000U);
    }
}

void radio_demo_start(void)
{
    int ret;

    if (s_si4731_task != QOSA_NULL)
    {
        return;
    }

    // general parameters initialization
    ret = qosa_msgq_create(&s_evt_msgq, sizeof(radio_evt_t), 10);
    if (ret != QOSA_OK)
    {
        QLOGE("[radio_demo] Event message queue create failed: %d", ret);
        return;
    }

    ret = qosa_timer_create(&s_fm_timer_get_rsq_status, fm_timer_get_rsq_status_callback, QOSA_NULL);
    if (ret != QOSA_OK)
    {
        QLOGE("[radio_demo] FM timer get RSQ status create failed: %d", ret);
        return;
    }

    s_fm_cur_freq_10khz = SI4731_FM_BAND_BOTTOM;    // Default FM frequency
    s_fm_target_freq_10khz = SI4731_FM_BAND_BOTTOM; // Default target FM frequency
    s_fm_cur_volume = SI4731_FM_DEFAULT_VOLUME;     // Default FM volume
    s_fm_target_volume = SI4731_FM_DEFAULT_VOLUME;  // Default target FM volume
    s_fm_button_mode = BUTTON_MODE_FREQUENCY;

    ec11_init(ec11_event_handler);
    qosa_task_sleep_ms(1000); /* Give EC11 task some time to initialize */

    ret = qosa_task_create(&s_si4731_task, SI4731_TASK_STACK_SIZE,
                           QOSA_PRIORITY_NORMAL, "si4731",
                           radio_task_entry, QOSA_NULL);
    if (ret != QOSA_OK)
    {
        QLOGE("[radio_demo] SI4731 task create failed: %d", ret);
        s_si4731_task = QOSA_NULL;
    }
}

UNIRTOS_APP_EXPORT(700, "si4731_radio", radio_demo_start);
