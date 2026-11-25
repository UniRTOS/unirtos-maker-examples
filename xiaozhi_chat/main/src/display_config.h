#ifndef XIAOZHI_DISPLAY_CONFIG_H
#define XIAOZHI_DISPLAY_CONFIG_H

/* Library/test default OFF. Main CMake explicitly enables the current W180
 * 360x360 ordinary SPI profile and touch-only face selection. Old generic
 * backends retain their own configuration gates in graphics/board_config.*.
 * This flag alone never supplies pins, enables SDK SPI or changes voltage. */
#ifndef XIAOZHI_DISPLAY_ENABLE
#define XIAOZHI_DISPLAY_ENABLE 0
#endif
#if XIAOZHI_DISPLAY_ENABLE != 0 && XIAOZHI_DISPLAY_ENABLE != 1
#error "XIAOZHI_DISPLAY_ENABLE must be 0 or 1"
#endif
#define XIAOZHI_DISPLAY_FRAME_MS 240U
#define XIAOZHI_DISPLAY_POLL_MS 20U
#define XIAOZHI_DISPLAY_STACK_BYTES 4096U

#endif