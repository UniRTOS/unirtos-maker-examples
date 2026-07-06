/*****************************************************************/ /**
* @file test_demo.c
* @author lysander.li@quectel.com
* @date 2026-03-31
* @brief This file contains the implementation of a mutex demonstration using UniRTOS.
**********************************************************************/
#include "qosa_sys.h"
#include "qosa_log.h"
#include "unirtos_app_init_registry.h"

#define QOS_LOG_TAG LOG_TAG_DEMO
static qosa_task_t          UNIRTOS_TEST_TASK_A = QOSA_NULL, UNIRTOS_TEST_TASK_B = QOSA_NULL;
static int share_count = 0;
static qosa_mutex_t count_mutex = QOSA_NULL;

/**
*    @brief: unirtos_task_a_handler
*    @description: This function serves as the entry point for Task A in the UniRTOS mutex demonstration. 
*    It continuously attempts to acquire a mutex lock on a shared resource, modifies the shared resource, and then releases the lock. 
*    The task simulates work by sleeping for a specified duration after each operation.
**/
static void unirtos_task_a_handler(void *arg)
{
    int ret;
    while (1)
    {
        ret = qosa_mutex_lock(count_mutex, QOSA_WAIT_FOREVER);
        if (ret != QOSA_OK)
        {
            QLOGE("[Mutex DEMO]Task A failed to acquire mutex, error code: %d\r\n", ret);
            continue;
        }
        share_count++;
        QLOGI("[Mutex DEMO]Task A add Count: %d\r\n", share_count);
        qosa_mutex_unlock(count_mutex);
        qosa_task_sleep_ms(100); // Simulate work by delaying for 100ms
    }
}
/**
 *    @brief: unirtos_task_b_handler
 *    @description: This function serves as the entry point for Task B in the UniRTOS mutex demonstration. 
 *    It continuously attempts to acquire a mutex lock on a shared resource, modifies the shared resource, and then releases the lock. 
 *    The task simulates work by sleeping for a specified duration after each operation.
 **/
static void unirtos_task_b_handler(void *arg)
{
    int ret;
    while (1)
    {
        ret = qosa_mutex_lock(count_mutex, QOSA_WAIT_FOREVER);
        if (ret != QOSA_OK)
        {
            QLOGE("[Mutex DEMO]Task B failed to acquire mutex, error code: %d\r\n", ret);
            continue;
        }
        share_count--;
        QLOGI("[Mutex DEMO]Task B subtract Count: %d\r\n", share_count);
        qosa_mutex_unlock(count_mutex);
        qosa_task_sleep_ms(150); // Simulate work by delaying for 150ms
    }
}

/**
 * @brief Initialize mutex demo task
 * @description: This function initializes the mutex demo task. It creates two tasks, Task A and Task B, 
 */
void unir_mutex_demo_init(void)
{
    QLOGV("[Mutex DEMO]Enter UniRTOS Mutex DEMO!");
    // Create a mutex for protecting the shared resource
    int ret;
    ret = qosa_mutex_create(&count_mutex);
    if (ret != QOSA_OK)
    {
        QLOGE("[Mutex DEMO]Failed to create mutex, error code: %d\r\n", ret);
        return;
    }
    // Create a power management demo task
    if (UNIRTOS_TEST_TASK_A == QOSA_NULL)
    {
         qosa_task_create(&UNIRTOS_TEST_TASK_A, 
                    4096, 
                    QOSA_PRIORITY_NORMAL, 
                    "mutex_demo_task_a", 
                    unirtos_task_a_handler, 
                    QOSA_NULL);
    }
    if (UNIRTOS_TEST_TASK_B == QOSA_NULL)
    {
         qosa_task_create(&UNIRTOS_TEST_TASK_B, 
                    4096, 
                    QOSA_PRIORITY_NORMAL, 
                    "mutex_demo_task_b", 
                    unirtos_task_b_handler, 
                    QOSA_NULL);
    }
   
}

UNIRTOS_APP_EXPORT(700, "unir_mutex_demo", unir_mutex_demo_init);