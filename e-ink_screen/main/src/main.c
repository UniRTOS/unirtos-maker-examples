#include "qosa_def.h"
#include "qosa_sys.h"
#include "qosa_log.h"
#include "qosa_gpio.h"
#include "qosa_dev_eigen.h"
#include "epaper_demo_config.h"
#include "unirtos_app_init_registry.h"

#define QOS_LOG_TAG LOG_TAG_DEMO

#if EPD_DEMO_ENABLED
static qosa_task_t g_epaper_task = QOSA_NULL;
extern void unir_epaper_demo_process(void *context);
#endif

void unir_epaper_demo_init(void)
{
    if(qosa_gpio_set_voltage(VOL_3_30V) != (qosa_gpio_error_e)QOSA_OK) {
        QLOGE("[chatbot] failed to set GPIO voltage");
    }
#if EPD_DEMO_ENABLED
    int result;

    if (g_epaper_task != QOSA_NULL)
    {
        return;
    }
    QLOGI("EPD app init: creating display task");
    result = qosa_task_create(&g_epaper_task, 4096, QOSA_PRIORITY_NORMAL,
                              "epaper_demo", unir_epaper_demo_process, QOSA_NULL);
    if (result != QOSA_ERROR_OK)
    {
        g_epaper_task = QOSA_NULL;
        QLOGE("EPD task creation failed: %d", result);
    }
#else
    QLOGW("EPD demo disabled");
#endif
}

UNIRTOS_APP_EXPORT(700, "unir_epaper_demo", unir_epaper_demo_init);