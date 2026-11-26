#ifndef SI4731_H
#define SI4731_H
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t resp1; //VALID = true if the tune status is valid, BLTF = true if the band limit was reached
    uint16_t freq_10khz;
    uint8_t rssi_dBuV;
    uint8_t snr_dB;
} si4731_tune_status_t;

typedef struct {
    uint16_t freq_10khz;
    uint8_t rssi_dBuV;
    uint8_t snr_dB;
} si4731_tune_status_noresp_t;

typedef struct {
    bool valid;
    bool stereo;
    uint8_t rssi_dBuV;
    uint8_t snr_dB;
    uint8_t multipath;
} si4731_rsq_status_t;

/**
 * Radio demo configuration and SI4731 definitions.
 */
#define SI4731_RST_PIN             25U
/* SEN is low on this module: 7-bit 0x11, write 0x22, read 0x23. */
#define SI4731_WRITE_ADDR          0x22U


/* Audio and tuning defaults. */
#define SI4731_FM_BAND_BOTTOM      8750U    /* 87.50 MHz, 10 kHz units */
#define SI4731_FM_BAND_TOP         10800U    /* 108.00 MHz, 10 kHz units */
#define SI4731_FM_MAX_SEEKS        60U



#define SI4731_CTS_POLL_COUNT      100U
#define SI4731_CTS_POLL_DELAY_MS   5U
#define SI4731_STC_POLL_COUNT      800U
#define SI4731_STC_POLL_DELAY_MS   10U
#define SI4731_XTAL_STARTUP_MS     500U



/**
 * SI4731 command definitions.
 */
#define SI4731_POWER_UP            0x01U
#define SI4731_GET_REV             0x10U
#define SI4731_SET_PROPERTY        0x12U
#define SI4731_GET_INT_STATUS      0x14U
#define SI4731_FM_TUNE_FREQ        0x20U
#define SI4731_FM_SEEK_START       0x21U
#define SI4731_FM_TUNE_STATUS      0x22U
#define SI4731_FM_RSQ_STATUS       0x23U

/**
 * SI4731 command parameters and response constants.
 */
#define SI4731_POWER_UP_XTAL       0x10U
#define SI4731_AUDIO_ANALOG_LR     0x05U
#define SI4731_SEEK_UP_NO_WRAP     0x08U
#define SI4731_TUNE_STATUS_INTACK  0x01U
#define SI4731_STATUS_CTS          0x80U
#define SI4731_STATUS_ERR          0x40U
#define SI4731_STATUS_STCINT       0x01U
#define SI4731_REV_LENGTH          9U
#define SI4731_TUNE_STATUS_LENGTH  8U
#define SI4731_RSQ_STATUS_LENGTH   8U
#define SI4731_RESP1_BLTF          0x80U
#define SI4731_RESP1_VALID         0x01U

/**
 * SI4731 property definitions.
 */
#define SI4731_PROP_FM_DEEMPHASIS  0x1100U
#define SI4731_PROP_RX_VOLUME      0x4000U
#define SI4731_PROP_RX_HARD_MUTE   0x4001U

/**
 * SI4731 property values.
 */
#define SI4731_DEEMPHASIS_50US     0x0001U  /* China/Europe */
#define SI4731_FM_DEFAULT_VOLUME           25U      /* 0..63, chip default is 63 */
#define SI4731_HARD_MUTE_LR        0x0003U
#define SI4731_HARD_MUTE_OFF       0x0000U

bool si4731_set_mute(bool mute);
bool si4731_set_volume(uint8_t volume);
bool si4731_fm_tune(uint16_t freq_10khz, si4731_tune_status_t *result);
/* Called after each seek step with the frequency the chip stopped on. */
typedef void (*si4731_scan_progress_cb_t)(uint16_t freq_10khz);
uint32_t si4731_fm_scan(si4731_tune_status_noresp_t *stations, uint32_t max_stations,
                        si4731_scan_progress_cb_t progress);
bool si4731_fm_rsq_status(si4731_rsq_status_t *result);
bool si4731_init(void);

#endif /* SI4731_H */