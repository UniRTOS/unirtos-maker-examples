#include "qosa_def.h"
#include "qosa_sys.h"
#include "qosa_log.h"
#include "epaper_demo_config.h"
#include "epaper_port.h"
#include "EPD_7in5_V2.h"
#include "price_tags.h"

#define QOS_LOG_TAG LOG_TAG_DEMO
static price_tag_page_t g_price_tag_page;

int price_tags_show(const price_tag_item_t *items, unsigned int count)
{
    int port_ready = 0;
    int result = -1;

    result = price_tags_prepare(&g_price_tag_page, items, count);
    if (result != PRICE_TAG_OK)
    {
        QLOGE("EPD price tag validation failed: %d", result);
        return result;
    }
    result = -1;
    if (epaper_port_init() != 0)
    {
        QLOGE("EPD software SPI GPIO initialization failed");
        goto cleanup;
    }
    port_ready = 1;
    if (EPD_7IN5_V2_Init() != 0)
    {
        QLOGE("EPD panel initialization failed");
        goto cleanup;
    }
    if (EPD_7IN5_V2_Clear() != 0)
    {
        QLOGE("EPD clear failed");
        goto cleanup;
    }
    if (EPD_7IN5_V2_DisplayRows(price_tags_render_panel_row, &g_price_tag_page) != 0)
    {
        QLOGE("EPD price tag display failed");
        goto cleanup;
    }
    result = 0;

cleanup:
    if (port_ready)
    {
        if (EPD_7IN5_V2_Sleep() != 0)
        {
            result = -1;
            QLOGE("EPD power off / deep sleep failed");
        }
        if (result != 0 && epaper_port_reset(QOSA_GPIO_LEVEL_LOW) != 0)
        {
            QLOGE("EPD failed to hold RST low; disconnect panel supply manually");
        }
        epaper_port_close(result != 0);
    }
    return result;
}

void unir_epaper_demo_process(void *context)
{
    int result;
    qosa_task_t current_task = QOSA_NULL;

    (void)context;
    QLOGI("EPD price tags: SOFTWARE SPI, 800x480, V2 OTP full refresh (0x1f)");
    result = price_tags_show(g_price_tag_defaults, PRICE_TAG_COUNT);
    if (result == 0)
    {
        QLOGI("EPD price tag transfer complete: BUSY idle, deep sleep entered; verify panel visually");
    }
    else
    {
        QLOGE("EPD DEMO FAILED: see preceding SPI, BUSY or V2 OTP stage error");
    }
    if (qosa_task_get_current_ref(&current_task) == QOSA_ERROR_OK)
    {
        qosa_task_delete(current_task);
    }
    QLOGE("EPD task could not exit; entering idle fallback");
    while (1)
    {
        qosa_task_sleep_ms(1000);
    }
}