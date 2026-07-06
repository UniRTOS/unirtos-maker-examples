/*****************************************************************/ /**
* @file pwrkey_demo.c
* @brief
* @author Silas.sun@quectel.com
* @date 2025-12-18
*
* @copyright Copyright (c) 2023 Quectel Wireless Solution, Co., Ltd.
* All Rights Reserved. Quectel Wireless Solution Proprietary and Confidential.
*
* @par EDIT HISTORY FOR MODULE
* <table>
* <tr><th>Date <th>Version <th>Author <th>Description"
* <tr><td>2025-12-18 <td>1.0 <td>Silas.sun <td> Init
* </table>
**********************************************************************/
#include "qosa_power.h"
#include "qosa_log.h"

#include "unirtos_app_init_registry.h"

#define QOS_LOG_TAG LOG_TAG_DEMO
static qosa_task_t          g_unir_pwrkey_demo_task = QOSA_NULL;

/**
 * @brief power down callback function
 *
 * This function serves as a callback handler for shutdown events.
 * This function will be called when a shutdown event is triggered.
 * Internally, this function will print log information and can perform 
 * simple callback processing as needed.
 *
 * @param pinlevel Context parameter for the callback function, used to pass
 *            callback-related context information
 * @return No return value
 */
static void pwrkey_demo_callback(qosa_uint8_t pinlevel)
{
    QLOGV("[boot_reason]]pwrkey level: %d",pinlevel);
}

/**
 * @brief Handle boot cause detection and logging
 * @return No return value
 */
void unir_pwrkey_demo_boot_cause(void)
{
    qosa_power_error_e ret;
    qosa_boot_cause_e boot_cause;

    // Get the boot reason
    ret = qosa_power_get_boot_cause(&boot_cause);
    if (ret == QOSA_POWER_SUCCESS)
    {
        switch (boot_cause)
        {
            case QOSA_BOOT_CAUSE_PSM_WAKE:
                QLOGV("[boot_reason]]Boot from PSM wake");
                break;
            case QOSA_BOOT_CAUSE_PWRKEY:
                QLOGV("[boot_reason]]Boot from power key");
                break;
            case QOSA_BOOT_CAUSE_RESET:
                QLOGV("[boot_reason]]Boot from reset key");
                break;
            case QOSA_BOOT_CAUSE_WDG:
                QLOGV("[boot_reason]]Boot from watchdog reset");
                break;
            case QOSA_BOOT_CAUSE_PANIC:
                QLOGV("[boot_reason]]Boot from panic reset");
                break;
            case QOSA_BOOT_CAUSE_SWRESET:
                QLOGV("[boot_reason]]Boot from software reset");
                break;
            default:
                QLOGV("[boot_reason]]Boot from unknown cause");
                break;
        }
    }
    else
    {
        QLOGE("[boot_reason]]Get boot cause failed, ret: %d", ret);
    }
}

/**
 * @brief Handle device reset functionality
 */
void unir_pwrkey_demo_device_reset(void)
{
    qosa_power_error_e ret;
    
    QLOGV("[boot_reason]]Will reset device after 3 seconds...");
    qosa_task_sleep_sec(3);
    ret = qosa_power_reset(QOSA_RESET_NORMAL);
    if (ret != QOSA_POWER_SUCCESS)
    {
        QLOGE("[boot_reason]]Reset failed, ret: %d", ret);
    }
}

/**
 * @brief Handle normal device power down
 */
void unir_pwrkey_demo_power_down(void)
{
    qosa_power_error_e ret;
    
    QLOGV("[boot_reason]]Will power down device after 3 seconds...");
    qosa_task_sleep_sec(3);
    ret = qosa_power_down(QOSA_POWD_NORMAL);
    if (ret != QOSA_POWER_SUCCESS)
    {
        QLOGE("[boot_reason]]Power down failed, ret: %d", ret);
    }
}

/**
 * @brief Handle power down via callback function
 */
void unir_pwrkey_demo_callback_power_down(void)
{
    // Register a pwrkey callback function
    qosa_pwrkey_callback_register(pwrkey_demo_callback);

    // Use a shutdown callback function to manually shut down the device.
    QLOGV("[boot_reason]]Will power down device via button callback function...");
    qosa_task_sleep_sec(3);
}

/**
 * @brief Power Management Demonstration Task
 * @param ctx Task context pointer
 */
static void unir_pwrkey_demo_process(void *ctx)
{
    // Waiting for system initialization to complete
    qosa_task_sleep_sec(2);

    // 1. Handle boot cause detection
    unir_pwrkey_demo_boot_cause();

    // 2. Demonstrate the restart function (comment it out to avoid actually restarting).
    unir_pwrkey_demo_device_reset();

    // 3. Demonstrates the normal shutdown function (commented out to avoid actually shutting down the device).
    unir_pwrkey_demo_power_down();

    // 4. Manually shut down the device using the shutdown callback function.
    unir_pwrkey_demo_callback_power_down();
}

/**
 * @brief Initialize power management demonstration
 */
void unir_pwrkey_demo_init(void)
{
    QLOGV("[boot_reason]]Enter UniRTOS Power DEMO!");
    
    // Create a power management demo task
    if (g_unir_pwrkey_demo_task == QOSA_NULL)
    {
         qosa_task_create(&g_unir_pwrkey_demo_task, 
                    4096, 
                    QOSA_PRIORITY_NORMAL, 
                    "power_demo", 
                    unir_pwrkey_demo_process, 
                    QOSA_NULL);
    }
   
}

UNIRTOS_APP_EXPORT(700, "unir_boot_reason_demo", unir_pwrkey_demo_init);