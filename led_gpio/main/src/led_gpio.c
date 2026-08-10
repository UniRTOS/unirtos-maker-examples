/*****************************************************************/ /**
* @file led_gpio.c
* @brief
* The demo initializes a GPIO pin connected to an LED, then toggles the LED on and off in a loop with a delay. 
* @author lysander.li@quectel.com
* @date 2026-03-27
*
**********************************************************************/


#include "qosa_sys.h"
#include "qosa_gpio.h"
#include "qosa_pinctrl.h"
#include "qosa_def.h"
#include "qosa_log.h"
#include "unirtos_app_init_registry.h"
#include "led_gpio.h"

#define QOS_LOG_TAG   LOG_TAG_DEMO

#define UniRTOS_LED_DEMO_TASK_STACK_SIZE 1024  // Task stack size 1KB

#define UniRTOS_LED_DEMO_TASK_PRIO QOSA_PRIORITY_NORMAL // Normal priority

static qosa_task_t g_led_gpio_demo_task = QOSA_NULL;

#define LED_PIN_NUM 19

qosa_pin_cfg_t pin_cfg; // Global variable to store the LED pin configuration, used for both initialization and level setting

/*
    Name: unir_led_init
    Description: Initialize the LED GPIO pin.
    @return 0 on success, 1 on failure
*/
static qosa_uint8_t unir_led_init(void)
{
    qosa_memset(&pin_cfg, 0, sizeof(qosa_pin_cfg_t));
    qosa_get_pin_default_cfg(LED_PIN_NUM, &pin_cfg);
    qosa_pin_set_func(LED_PIN_NUM, pin_cfg.gpio_func);
    // Initialize the LED GPIO pin as output, with pull-up and default level high (LED off)
    if (qosa_gpio_init(pin_cfg.gpio_num, QOSA_GPIO_DIRECTION_OUTPUT, QOSA_GPIO_PULL_UP, QOSA_GPIO_LEVEL_HIGH) != QOSA_GPIO_SUCCESS)
    {
        QLOGD("[led]Failed to initialize LED GPIO");
        return 1; // Return 1 on failure
    }
    QLOGI("[led]LED GPIO initialized successfully, pin_num: %d, gpio_num: %d, level: %d", LED_PIN_NUM, pin_cfg.gpio_num, QOSA_GPIO_LEVEL_HIGH);
    return 0;
}

/*
    Name: unir_led_set
    Description: Set the LED GPIO level to on or off.
    @param gpio_level: The desired GPIO level for the LED, where QOSA_GPIO_LEVEL_LOW turns the LED on and QOSA_GPIO_LEVEL_HIGH turns it off.
    @return 0 on success, 1 on failure
*/
static qosa_uint8_t unir_led_set(qosa_gpio_level_e gpio_level)
{ 
    if(qosa_gpio_set_level(pin_cfg.gpio_num, gpio_level) != QOSA_GPIO_SUCCESS)
    {
        QLOGD("[led]Failed to set LED GPIO level");
        return 1; // Return 1 on failure
    }
    return 0;
}

/*
    Name: unir_led_demo_process
    Description: The main process function for the LED GPIO Demo, which initializes the LED and toggles it on and off in a loop.
    @param ctx: Task context pointer, reserved for future use, currently not used
    @return None
*/
static void unir_led_demo_process(void *ctx)
{
    unir_led_init();
    while (1)
    {
        unir_led_set(QOSA_GPIO_LEVEL_LOW);
        QLOGI("[led]LED ON");
        qosa_task_sleep_ms(1000);
        unir_led_set(QOSA_GPIO_LEVEL_HIGH);
        QLOGI("[led]LED OFF");
        qosa_task_sleep_ms(1000);
    }
    
}

/*
    Name: unir_led_demo_init
    Description: Initialize the LED GPIO Demo, create a task to run the demo.
    @param None
*/
void unir_led_demo_init(void)
{
    // Log the entry of the LED GPIO Demo initialization
    QLOGV("[led]enter LED GPIO DEMO !!!");

    // Create a task for the LED GPIO Demo using qosa_task_create, with specified stack size, priority, name, and entry function
    if (g_led_gpio_demo_task == QOSA_NULL) // Check if the LED GPIO Demo task has already been created
    {       
        
        qosa_task_create(
            &g_led_gpio_demo_task,
            UniRTOS_LED_DEMO_TASK_STACK_SIZE,     // Task stack size
            UniRTOS_LED_DEMO_TASK_PRIO,           // Task priority
            "led_gpio_demo",                       // Task name
            unir_led_demo_process,                // Task entry function
            QOSA_NULL                             // Task context (not used in this case
        );
    }
}

UNIRTOS_APP_EXPORT(700, "unir_led_demo", unir_led_demo_init);