#include "board_config.h"

/* Supplier-confirmed ordinary 4-wire SPI commands go here, including required
 * gamma/power settings, MADCTL, RGB565 (COLMOD), SLPOUT/DISPON and delays.
 * Do not paste QSPI transaction prefixes or another panel's init table.
 * Empty by design: even ENABLE=1 cannot start without a real command table. */
const robot_lcd_init_step_t *robot_lcd_vendor_init_steps(size_t *count)
{
    if (count != NULL) {
        *count = 0;
    }
    return NULL;
}