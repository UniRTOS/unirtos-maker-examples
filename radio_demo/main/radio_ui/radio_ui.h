#ifndef RADIO_UI_H
#define RADIO_UI_H

#include <stdbool.h>
#include <stdint.h>

/* Everything the screen shows. The control task fills it; the UI only reads it. */
typedef struct
{
    uint16_t freq_10khz;  /* displayed frequency, 10 kHz units */
    uint8_t volume;       /* 0..63 */
    bool volume_mode;     /* true: knob adjusts volume, false: knob tunes */
    bool scanning;        /* initial band scan in progress */
    bool tuning;          /* target frequency not applied to the chip yet */
    bool rsq_ok;          /* rssi/snr/valid below hold real data */
    bool valid;
    bool stereo_known; /* stereo below was read from RSQ for this station */
    bool stereo;
    uint8_t rssi_dBuV;
    uint8_t snr_dB;
} radio_ui_state_t;

/* Initialise the LCD and draw the static background. Call once from the control task. */
bool radio_ui_init(void);

/* Redraw only the parts of the screen whose data changed since the last call. */
void radio_ui_update(const radio_ui_state_t *state);

/* Run the GUI timer handler from the radio control task. */
void radio_ui_process(void);

#endif /* RADIO_UI_H */
