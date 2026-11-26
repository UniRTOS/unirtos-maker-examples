
#include "i2c.h"
#include "si4731.h"

#include <stdbool.h>
#include <stdint.h>

#include "qosa_def.h"
#include "qosa_sys.h"
#include "qosa_gpio.h"
#include "qosa_pinctrl.h"
#include "qosa_log.h"
#include "qosa_dev_eigen.h"
#include "unirtos_app_init_registry.h"

#define QOS_LOG_TAG LOG_TAG_DEMO


static qosa_gpio_num_e s_rst_gpio = QOSA_GPIO_MAX;
/**
 * @brief Configures the SI4731 reset pin and initializes I2C
 * 
 * @return true if the pin is successfully configured and I2C is initialized
 * @return false if any step fails
 */
static bool si4731_configure_pin(void)
{
    qosa_pin_cfg_t rst_cfg;

    // Initialize I2C and configure SI4731 reset pin
    if (!i2c_init())
    {
        QLOGE("[si4731] I2C initialization failed");
        return false;
    }

    if (qosa_get_pin_default_cfg((qosa_uint8_t)SI4731_RST_PIN, &rst_cfg) != QOSA_GPIO_SUCCESS)
    {
        QLOGE("[si4731] SI4731 RST pin config lookup failed: pin=%d", (int)SI4731_RST_PIN);
        return false;
    }

    if (qosa_pin_set_func((qosa_pin_num_e)SI4731_RST_PIN, rst_cfg.gpio_func) != QOSA_PINCTRL_SUCCESS)
    {
        QLOGE("[si4731] SI4731 RST pin mux failed");
        return false;
    }

    s_rst_gpio = rst_cfg.gpio_num;
    if (qosa_gpio_init(s_rst_gpio, QOSA_GPIO_DIRECTION_OUTPUT,QOSA_GPIO_PULL_NONE, QOSA_GPIO_LEVEL_HIGH) != QOSA_GPIO_SUCCESS)
    {
        QLOGE("[si4731] SI4731 RST GPIO init failed");
        return false;
    }
    return true;
}

/**
 * @brief Resets the SI4731 by toggling the reset pin
 * 
 * @return true if the reset sequence is successful
 * @return false if any step fails
 */
static bool si4731_reset(void)
{
    if (qosa_gpio_set_level(s_rst_gpio, QOSA_GPIO_LEVEL_LOW) != QOSA_GPIO_SUCCESS)
    {
        QLOGE("[si4731] SI4731 reset assert failed");
        return false;
    }
    qosa_task_sleep_ms(10U);

    if (qosa_gpio_set_level(s_rst_gpio, QOSA_GPIO_LEVEL_HIGH) != QOSA_GPIO_SUCCESS)
    {
        QLOGE("[si4731] SI4731 reset release failed");
        return false;
    }
    qosa_task_sleep_ms(10U);
    return true;
}

/**
 * @brief Waits for the SI4731 to assert the Clear To Send (CTS) signal
 * 
 * @return true if CTS is asserted and no error is reported
 * @return false if CTS times out or an error is reported
 */
static bool si4731_wait_cts(void)
{
    uint8_t status = 0U;
    uint32_t poll;

    for (poll = 0U; poll < SI4731_CTS_POLL_COUNT; poll++)
    {
        if (!i2c_read(SI4731_WRITE_ADDR, &status, 1U))
        {
            QLOGE("[si4731] SI4731 status read failed");
            return false;
        }
        if ((status & SI4731_STATUS_CTS) != 0U)
        {
            if ((status & SI4731_STATUS_ERR) != 0U)
            {
                QLOGE("[si4731] SI4731 command error: status=0x%02X", (unsigned int)status);
                return false;
            }
            return true;
        }
        qosa_task_sleep_ms(SI4731_CTS_POLL_DELAY_MS);
    }

    QLOGE("[si4731] SI4731 CTS timeout: status=0x%02X", (unsigned int)status);
    return false;
}

/**
 * @brief Sends a command to the SI4731 and waits for CTS
 * 
 * @param command Pointer to the command buffer
 * @param length Length of the command buffer
 * @return true if the command is acknowledged and CTS is asserted
 * @return false if the command is not acknowledged or CTS times out
 */
static bool si4731_command(const uint8_t *command, uint32_t length)
{
    if (!i2c_write(SI4731_WRITE_ADDR, command, length))
    {
        QLOGE("[si4731] SI4731 command 0x%02X not acknowledged", (unsigned int)command[0]);
        return false;
    }
    return si4731_wait_cts();
}

/**
 * @brief Powers up the SI4731 in FM mode with the crystal and audio configured
 * 
 * @return true if the power-up sequence is successful
 * @return false if any step fails
 */
static bool si4731_power_up_fm(void)
{
    const uint8_t command[3] = {
        SI4731_POWER_UP,
        SI4731_POWER_UP_XTAL,
        SI4731_AUDIO_ANALOG_LR};

    if (!i2c_write(SI4731_WRITE_ADDR, command, sizeof(command)))
    {
        QLOGE("[si4731] SI4731 POWER_UP not acknowledged");
        return false;
    }
    qosa_task_sleep_ms(SI4731_XTAL_STARTUP_MS);
    return si4731_wait_cts();
}
/**
 * @brief Reads the revision information from the SI4731
 * 
 * @return true if the revision was read successfully
 * @return false if the command failed or the response is invalid
 */
static bool si4731_read_revision(void)
{
    const uint8_t command = SI4731_GET_REV;
    uint8_t revision[SI4731_REV_LENGTH];

    if (!si4731_command(&command, 1U) ||
        !i2c_read(SI4731_WRITE_ADDR, revision, sizeof(revision)))
    {
        QLOGE("[si4731] SI4731 GET_REV failed");
        return false;
    }
    if ((revision[0] & (SI4731_STATUS_CTS | SI4731_STATUS_ERR)) != SI4731_STATUS_CTS ||
        (revision[1] != 30U && revision[1] != 31U))
    {
        QLOGE("[si4731] SI4731 invalid revision response: status=0x%02X PN=0x%02X",
              (unsigned int)revision[0], (unsigned int)revision[1]);
        return false;
    }

    QLOGI("[si4731] SI47%02u connected: firmware=%c.%c component=%c.%c chip=%c",
          (unsigned int)revision[1], revision[2], revision[3],
          revision[6], revision[7], revision[8]);
    return true;
}

/**
 * @brief Sets a property on the SI4731
 * 
 * @param property The property ID
 * @param value The value to set
 * @return true if the property was set successfully
 * @return false if the command failed
 */
static bool si4731_set_property(uint16_t property, uint16_t value)
{
    const uint8_t command[6] = {
        SI4731_SET_PROPERTY, 0x00U,
        (uint8_t)(property >> 8), (uint8_t)property,
        (uint8_t)(value >> 8), (uint8_t)value};

    if (!si4731_command(command, sizeof(command)))
    {
        QLOGE("[si4731] SI4731 SET_PROPERTY 0x%04X=0x%04X failed",
              (unsigned int)property, (unsigned int)value);
        return false;
    }
    /* SET_PROPERTY needs about 10 ms before the next command. */
    qosa_task_sleep_ms(10U);
    return true;
}

static bool si4731_wait_stc(void)
{
    const uint8_t command = SI4731_GET_INT_STATUS;
    uint8_t status = 0U;
    uint32_t poll;

    for (poll = 0U; poll < SI4731_STC_POLL_COUNT; poll++)
    {
        if (!si4731_command(&command, 1U) ||
            !i2c_read(SI4731_WRITE_ADDR, &status, 1U))
        {
            return false;
        }
        if ((status & SI4731_STATUS_STCINT) != 0U)
        {
            return true;
        }
        qosa_task_sleep_ms(SI4731_STC_POLL_DELAY_MS);
    }

    QLOGE("[si4731] SI4731 tune/seek timeout: status=0x%02X", (unsigned int)status);
    return false;
}

static bool si4731_fm_tune_status(si4731_tune_status_t *result)
{
    const uint8_t command[2] = {SI4731_FM_TUNE_STATUS, SI4731_TUNE_STATUS_INTACK};
    uint8_t response[SI4731_TUNE_STATUS_LENGTH];

    if (!si4731_command(command, sizeof(command)) ||
        !i2c_read(SI4731_WRITE_ADDR, response, sizeof(response)))
    {
        QLOGE("[si4731] SI4731 FM_TUNE_STATUS failed");
        return false;
    }
    result->resp1 = response[1];
    result->freq_10khz = (uint16_t)(((uint16_t)response[2] << 8) | response[3]);
    result->rssi_dBuV = response[4];
    result->snr_dB = response[5];
    return true;
}

/**
 * @brief Initiates an FM seek upwards and retrieves the tune status.
 * 
 * @param result Pointer to a structure to receive the tune status
 * @return true if the seek was successful
 * @return false if the seek failed
 */
static bool si4731_fm_seek_up(si4731_tune_status_t *result)
{
    const uint8_t command[2] = {SI4731_FM_SEEK_START, SI4731_SEEK_UP_NO_WRAP};

    return si4731_command(command, sizeof(command)) &&
           si4731_wait_stc() && si4731_fm_tune_status(result);
}

/* Properties are lost on power-down, so this runs after every FM power-up. Leaves audio muted. */
static bool si4731_fm_configure(void)
{
    return si4731_set_property(SI4731_PROP_FM_DEEMPHASIS, SI4731_DEEMPHASIS_50US) &&
           si4731_set_property(SI4731_PROP_RX_VOLUME, SI4731_FM_DEFAULT_VOLUME) &&
           si4731_set_property(SI4731_PROP_RX_HARD_MUTE, SI4731_HARD_MUTE_LR);
}

/**
 * @brief Tunes the SI4731 to the specified FM frequency and retrieves the tune status.
 * 
 * @param freq_10khz The frequency to tune to, in 10 kHz units
 * @param result Pointer to a structure to receive the tune status
 * @return true if the tune was successful
 * @return false if the tune failed
 */
 bool si4731_fm_tune(uint16_t freq_10khz, si4731_tune_status_t *result)
{
    const uint8_t command[5] = {
        SI4731_FM_TUNE_FREQ, 0x00U, (uint8_t)(freq_10khz >> 8), (uint8_t)freq_10khz, 0x00U};

    return si4731_command(command, sizeof(command)) &&
           si4731_wait_stc() && si4731_fm_tune_status(result);
}

bool si4731_set_mute(bool mute)
{
    return si4731_set_property(SI4731_PROP_RX_HARD_MUTE, mute ? SI4731_HARD_MUTE_LR : SI4731_HARD_MUTE_OFF);
}

bool si4731_set_volume(uint8_t volume)
{
    return si4731_set_property(SI4731_PROP_RX_VOLUME, volume);
}
bool si4731_fm_rsq_status(si4731_rsq_status_t *result)
{
    const uint8_t command[2] = {SI4731_FM_RSQ_STATUS,0};


    uint8_t response[SI4731_RSQ_STATUS_LENGTH];

    if (!si4731_command(command, sizeof(command)) ||
        !i2c_read(SI4731_WRITE_ADDR, response, sizeof(response)))
    {
        QLOGE("[si4731] SI4731 FM_RSQ_STATUS failed");
        return false;
    }
    result->valid = (response[2] & SI4731_RESP1_VALID) != 0U;
    result->stereo = (response[3] & 0x80U) != 0U;
    result->rssi_dBuV = response[4];
    result->snr_dB = response[5];
    result->multipath = response[6];

    return true;
}

/*
 * Scan the band once and collect valid stations, strongest RSSI first. Returns the count.
 * A station can be a transient spur (e.g. LCD/SPI noise during the scan), so the caller
 * retunes and re-checks each candidate instead of trusting the strongest one blindly.
 */
 uint32_t si4731_fm_scan(si4731_tune_status_noresp_t *stations, uint32_t max_stations,
                         si4731_scan_progress_cb_t progress)
{
    si4731_tune_status_t status;
    uint32_t count = 0U;
    uint32_t seeks;
    uint32_t i;

    if (!si4731_fm_tune(SI4731_FM_BAND_BOTTOM, &status))
    {
        return 0U;
    }

    for (seeks = 0U; seeks < SI4731_FM_MAX_SEEKS; seeks++)
    {
        if (!si4731_fm_seek_up(&status))
        {
            break;
        }
        if (progress != NULL)
        {
            progress(status.freq_10khz);
        }
        if ((status.resp1 & SI4731_RESP1_VALID) != 0U)
        {
            QLOGI("[si4731] FM station %u.%02u MHz RSSI=%u dBuV SNR=%u dB",
                  (unsigned int)(status.freq_10khz / 100U), (unsigned int)(status.freq_10khz % 100U),
                  (unsigned int)status.rssi_dBuV, (unsigned int)status.snr_dB);
            /* Insertion sort by RSSI, dropping the weakest when the list is full. */
            i = (count < max_stations) ? count++ : max_stations;
            while (i > 0U && stations[i - 1U].rssi_dBuV < status.rssi_dBuV)
            {
                if (i < max_stations)
                {
                    stations[i] = stations[i - 1U];
                }
                i--;
            }
            if (i < max_stations)
            {
                stations[i].freq_10khz = status.freq_10khz;
                stations[i].rssi_dBuV = status.rssi_dBuV;
                stations[i].snr_dB = status.snr_dB;
            }
        }
        if ((status.resp1 & SI4731_RESP1_BLTF) != 0U)
        {
            break;
        }
    }
    return count;
}

bool si4731_init(void)
{
   
    if (!si4731_configure_pin())
    {
        QLOGE("[si4731] SI4731 configuration failed");
        return false;
    }
    if (!si4731_reset())
    {
        QLOGE("[si4731] SI4731 reset failed");
        return false;
    }
    if (!si4731_power_up_fm())
    {
        QLOGE("[si4731] SI4731 power up FM failed");
        return false;
    }
    (void)si4731_read_revision();

    if (!si4731_fm_configure())
    {
        QLOGE("[si4731] SI4731 FM configuration failed");
        return false;
    }
    return true;
}