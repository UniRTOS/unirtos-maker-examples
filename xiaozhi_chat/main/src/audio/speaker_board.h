#ifndef SPEAKER_TEST_BOARD_H
#define SPEAKER_TEST_BOARD_H

#define SPEAKER_BOARD_QUECDUINO 0
#define SPEAKER_BOARD_EG800Z_LCD 1

#ifndef SPEAKER_TEST_BOARD
/* Current target confirmed by user: EG800Z QuecDuino V1.0. */
#define SPEAKER_TEST_BOARD SPEAKER_BOARD_QUECDUINO
#endif

#if SPEAKER_TEST_BOARD != SPEAKER_BOARD_QUECDUINO && SPEAKER_TEST_BOARD != SPEAKER_BOARD_EG800Z_LCD
#error "Unsupported speaker test board"
#endif

/* Official QuecDuino/MSB reference; legacy LCD keeps its existing I2S path. */
#ifndef SPEAKER_TEST_OFFICIAL_REFERENCE
#define SPEAKER_TEST_OFFICIAL_REFERENCE (SPEAKER_TEST_BOARD == SPEAKER_BOARD_QUECDUINO)
#endif
#if SPEAKER_TEST_OFFICIAL_REFERENCE && SPEAKER_TEST_BOARD != SPEAKER_BOARD_QUECDUINO
#error "Official reference is only for the confirmed QuecDuino board"
#endif

/* Physical module terminals and actual SDK controller, NOT schematic net names.
 * QuecDuino's nets named I2C0_SCL/SDA route to ALT2 I2C1 on pins67/66. */
#if SPEAKER_TEST_BOARD == SPEAKER_BOARD_QUECDUINO
#define SPEAKER_BOARD_NAME "EG800Z QuecDuino V1.0"
#define SPEAKER_CODEC_I2C_BUS 1U
#define SPEAKER_CODEC_SCL_MODULE_PIN 67U
#define SPEAKER_CODEC_SDA_MODULE_PIN 66U
/* J13-1 via R30; effective only when J13 connects 1-2. Not GPIO enum54. */
#define SPEAKER_PA_MODULE_PIN 54U
#else
#define SPEAKER_BOARD_NAME "EG800Z_LCD V1.1"
#define SPEAKER_CODEC_I2C_BUS 0U
#define SPEAKER_CODEC_SCL_MODULE_PIN 39U
#define SPEAKER_CODEC_SDA_MODULE_PIN 38U
#define SPEAKER_PA_MODULE_PIN 26U
#endif

/* Physical MODULE terminal number, NOT a QOSA GPIO enum or MCU GPIO number. */
#define SPEAKER_LCD_PA_MODULE_PIN 26U

#endif