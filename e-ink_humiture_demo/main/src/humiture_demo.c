#include "humiture_demo.h"
#include "EPD_3in7.h"
#include "DEV_Config.h"
#include "GUI_Paint.h"
#include "Debug.h"
#include "aht20.h"
#include <stdio.h>
#include <string.h>

#define QOS_LOG_TAG LOG_TAG_DEMO

#include "unirtos_app_init_registry.h"

// Define  task priority as normal
#define UNIR_AHT20_COLLECT_TASK_PRIO QOSA_PRIORITY_NORMAL
#define UNIR_EINK_DISPLAY_TASK_PRIO QOSA_PRIORITY_NORMAL
#define HUMITURE_UART_TEXT_QUEUE_DEPTH 8
#define HUMITURE_LOGE(...) QLOGE("[e-ink_humiture_demo] " __VA_ARGS__)
#define HUMITURE_LOGW(...) QLOGW("[e-ink_humiture_demo] " __VA_ARGS__)
#define HUMITURE_LOGI(...) QLOGI("[e-ink_humiture_demo] " __VA_ARGS__)

// Declare global task handle and initialize as NULL
static qosa_task_t g_unir_aht20_collect_task = QOSA_NULL;
static qosa_task_t g_unir_eink_display_task = QOSA_NULL;

static qosa_sem_t g_humiture_data_semaphore = QOSA_NULL;
static qosa_mutex_t g_humiture_data_mutex = QOSA_NULL;
/**
 * @brief Humiture data structure
 * @param humidity Humidity value accuracy:0.024% range:0~100%
 * @param temperature Temperature value accuracy:0.01°C range:-40~85°C
 */
typedef struct
{
    int temperature_tenths;
    int humidity_tenths;

} humiture_data_t;
static humiture_data_t g_humiture_data;
static humiture_data_t g_humiture_data_last;
static UBYTE *image = NULL;
static char g_uart_text_queue[HUMITURE_UART_TEXT_QUEUE_DEPTH][HUMITURE_UART_TEXT_MAX_LEN + 1];
static unsigned int g_uart_text_head;
static unsigned int g_uart_text_tail;
static unsigned int g_uart_text_count;

void humiture_demo_submit_uart_text(const char *text)
{
    if (text == NULL || text[0] == '\0' || g_humiture_data_mutex == QOSA_NULL ||
        g_humiture_data_semaphore == QOSA_NULL)
    {
        return;
    }

    int ret = qosa_mutex_lock(g_humiture_data_mutex, HUMITURE_DATA_COLLECT_WAIT_MS);
    if (ret != QOSA_ERROR_OK)
    {
        HUMITURE_LOGW("UART text dropped: display queue lock timed out");
        return;
    }

    if (g_uart_text_count == HUMITURE_UART_TEXT_QUEUE_DEPTH)
    {
        g_uart_text_head = (g_uart_text_head + 1) % HUMITURE_UART_TEXT_QUEUE_DEPTH;
        g_uart_text_count--;
        HUMITURE_LOGW("UART display queue full; oldest text dropped");
    }

    size_t length = 0;
    while (length < HUMITURE_UART_TEXT_MAX_LEN && text[length] != '\0')
    {
        length++;
    }
    memcpy(g_uart_text_queue[g_uart_text_tail], text, length);
    g_uart_text_queue[g_uart_text_tail][length] = '\0';
    g_uart_text_tail = (g_uart_text_tail + 1) % HUMITURE_UART_TEXT_QUEUE_DEPTH;
    g_uart_text_count++;

    HUMITURE_LOGI("UART display text queued, length=%u/%u",
                  (unsigned int)length, (unsigned int)HUMITURE_UART_TEXT_MAX_LEN);
    qosa_mutex_unlock(g_humiture_data_mutex);
    qosa_sem_release(g_humiture_data_semaphore);
}

static int eink_display_init(void)
{
    UWORD image_size = ((EPD_3IN7_WIDTH + 7) / 8) * EPD_3IN7_HEIGHT;
    image = (UBYTE *)qosa_malloc(image_size);
    if (image == NULL)
    {
        HUMITURE_LOGE("Display image allocation failed");
        return -1;
    }

    if (DEV_Module_Init() != 0)
    {
        HUMITURE_LOGE("Display module initialization failed");
        qosa_free(image);
        image = NULL;
        return -1;
    }

    EPD_3IN7_4Gray_Init();
    EPD_3IN7_4Gray_Clear(); // 全屏刷白
    EPD_3IN7_1Gray_Init();  // 切回黑白模式

    Paint_NewImage(image, EPD_3IN7_WIDTH, EPD_3IN7_HEIGHT, ROTATE_270, WHITE);
    Paint_SetScale(2);

    return 0;
}

static int eink_display_refresh_region(UWORD x_start, UWORD y_start,
                                      UWORD x_end, UWORD y_end)
{
    UWORD physical_x_start;
    UWORD physical_x_end;
    UWORD physical_y_start;
    UWORD physical_y_end;

    if (image == NULL || x_start >= x_end || y_start >= y_end ||
        x_end > EPD_3IN7_HEIGHT || y_end > EPD_3IN7_WIDTH)
    {
        HUMITURE_LOGE("Invalid partial display region");
        return -1;
    }

    physical_x_start = y_start & (UWORD)~7U;
    physical_x_end = (UWORD)((y_end + 7U) & ~7U);
    if (physical_x_end > EPD_3IN7_WIDTH)
    {
        physical_x_end = EPD_3IN7_WIDTH;
    }
    physical_y_start = EPD_3IN7_HEIGHT - x_end;
    physical_y_end = EPD_3IN7_HEIGHT - x_start;

    UWORD bytes_per_row = (physical_x_end - physical_x_start) / 8;
    UWORD rows = physical_y_end - physical_y_start;
    size_t partial_size = (size_t)bytes_per_row * rows;
    UBYTE *partial_image = (UBYTE *)qosa_malloc(partial_size);
    if (partial_image == NULL)
    {
        HUMITURE_LOGE("Partial display buffer allocation failed");
        return -1;
    }

    for (UWORD row = 0; row < rows; row++)
    {
        memcpy(partial_image + (size_t)row * bytes_per_row,
               image + (size_t)(physical_y_start + row) * (EPD_3IN7_WIDTH / 8) +
                   physical_x_start / 8,
               bytes_per_row);
    }

    EPD_3IN7_1Gray_Display_Part(partial_image, physical_x_start, physical_y_start,
                                physical_x_end, physical_y_end);
    qosa_free(partial_image);
    return 0;
}

static int eink_display_refresh_data(humiture_data_t *data, bool *first_flag,
                                     const char *uart_text)
{
    if (data == NULL || first_flag == NULL || image == NULL)
    {
        HUMITURE_LOGE("Invalid display data, refresh flag, or image buffer");
        return -1;
    }

    bool initial_refresh = *first_flag;
    if (initial_refresh)
    {
        Paint_Clear(WHITE);
        Paint_DrawString_EN(10, 10, "Quectel", &FontAscii24, WHITE, BLACK);
        Paint_DrawLine(10, 40, EPD_3IN7_HEIGHT - 10, 40, BLACK, DOT_PIXEL_1X1, LINE_STYLE_DOTTED);
        Paint_DrawLine(317, 93, 317, 220, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
        Paint_DrawThermometerIcon(145, 47);
        Paint_DrawHumidityIcon(373, 48);
        Paint_DrawBernard28UnitText(280, 176, "\xE2\x84\x83");
    }

    char temp_text[16];
    char humidity_text[16];
    int temperature_tenths = data->temperature_tenths;
    unsigned int humidity_tenths = (unsigned int)data->humidity_tenths;
    unsigned int magnitude = temperature_tenths < 0
                                 ? 0u - (unsigned int)temperature_tenths
                                 : (unsigned int)temperature_tenths;

    snprintf(temp_text, sizeof(temp_text), "%s%u.%u",
             temperature_tenths < 0 ? "-" : "", magnitude / 10, magnitude % 10);
    snprintf(humidity_text, sizeof(humidity_text), "%u.%u%%",
             humidity_tenths / 10, humidity_tenths % 10);

    if (initial_refresh || temperature_tenths != g_humiture_data_last.temperature_tenths)
    {
        const UWORD x_start = 17;
        const UWORD y_start = 129;
        const UWORD value_x_end = 275;
        const UWORD value_y_end = 230;
        Paint_ClearWindows(x_start, y_start, value_x_end, value_y_end, WHITE);

        Paint_DrawDSEG7_64TextCentered(x_start, y_start,
                         value_x_end - x_start, temp_text);
        int refresh_result = initial_refresh ? 0 : eink_display_refresh_region(
            x_start, y_start, 317, value_y_end);
        if (refresh_result != 0)
        {
            HUMITURE_LOGW("Temperature display refresh failed");
        }
        if (!initial_refresh && refresh_result == 0)
        {
            g_humiture_data_last.temperature_tenths = temperature_tenths;
        }
    }
    if (initial_refresh || humidity_tenths != g_humiture_data_last.humidity_tenths)
    {
        const UWORD x_start = 318;
        const UWORD y_start = 139;
        const UWORD value_x_end = 470;
        const UWORD value_y_end = 221;
        Paint_ClearWindows(x_start, y_start, value_x_end, 175, WHITE);
        Paint_ClearWindows(374, 186, 414, value_y_end, WHITE);

        Paint_DrawDSEG7_32TextCentered(x_start, y_start,
                           value_x_end - x_start, humidity_text);
        Paint_DrawHumidityStatusIcon(394, 220, humidity_tenths);
        int refresh_result = initial_refresh ? 0 : eink_display_refresh_region(
            x_start, y_start, value_x_end, value_y_end);
        if (refresh_result != 0)
        {
            HUMITURE_LOGW("Humidity display refresh failed");
        }
        if (!initial_refresh && refresh_result == 0)
        {
            g_humiture_data_last.humidity_tenths = humidity_tenths;
        }
    }

    if (uart_text != NULL && uart_text[0] != '\0')
    {
        const UWORD x_start = 10;
        const UWORD y_start = 10;
        const UWORD x_end = 470;
        const UWORD y_end = 34;
        Paint_ClearWindows(x_start, y_start, x_end, y_end, WHITE);
        Paint_DrawString_EN(x_start, y_start, uart_text, &FontAscii24, WHITE, BLACK);
        if (!initial_refresh &&
            eink_display_refresh_region(x_start, y_start, x_end, y_end) != 0)
        {
            HUMITURE_LOGW("UART text display refresh failed");
        }
    }

    if (initial_refresh)
    {
        EPD_3IN7_1Gray_Display(image);
        g_humiture_data_last.temperature_tenths = temperature_tenths;
        g_humiture_data_last.humidity_tenths = (int)humidity_tenths;
        *first_flag = false;
    }

    return 0;
}

static void humiture_sensor_task(void *ctx)
{
    aht20_sample_t sample;
    int32_t temp_abs;
    (void)ctx;

    if (!aht20_init())
    {
        HUMITURE_LOGE("AHT20 initialization failed; retrying during sampling");
    }

    qosa_task_sleep_ms(5000);
    while (1)
    {
        if (aht20_read(&sample))
        {
            temp_abs = (sample.temperature_tenths < 0) ? -sample.temperature_tenths : sample.temperature_tenths;

            int ret = qosa_mutex_lock(g_humiture_data_mutex, HUMITURE_DATA_COLLECT_WAIT_MS);
            if (ret != QOSA_ERROR_OK)
            {
                HUMITURE_LOGE("Sensor data lock failed, error=%d", ret);

                goto next;
            }

            g_humiture_data.humidity_tenths = sample.humidity_tenths;
            g_humiture_data.temperature_tenths = sample.temperature_tenths;
            qosa_mutex_unlock(g_humiture_data_mutex);
            HUMITURE_LOGI("Sample: temperature=%s%d.%d C, humidity=%u.%u%%",
                  sample.temperature_tenths < 0 ? "-" : "",
                  (int)(temp_abs / 10), (int)(temp_abs % 10),
                (unsigned int)(sample.humidity_tenths / 10U),
                (unsigned int)(sample.humidity_tenths % 10U));
            qosa_sem_release(g_humiture_data_semaphore);
            goto next;
        }
        else
        {
        }

    next:
        qosa_task_sleep_ms(HUMITURE_DATA_COLLECT_INTERVAL_MS);
    }
}
static void humiture_display_task(void *ctx)
{
    humiture_data_t display_data;
    char uart_text[HUMITURE_UART_TEXT_MAX_LEN + 1];
    int ret;
    if (eink_display_init() != 0)
    {
        HUMITURE_LOGE("E-ink display initialization failed");
        return;
    }
    HUMITURE_LOGI("E-ink display initialized");

    bool first_flag = true;
    while (1)
    {
        ret = qosa_sem_wait(g_humiture_data_semaphore, QOSA_WAIT_FOREVER);
        if (ret != QOSA_ERROR_OK)
        {
            HUMITURE_LOGE("Display task semaphore wait failed, error=%d", ret);
            goto next;
        }
        ret = qosa_mutex_lock(g_humiture_data_mutex, QOSA_WAIT_FOREVER);
        if (ret != QOSA_ERROR_OK)
        {
            HUMITURE_LOGE("Display task data lock failed, error=%d", ret);
            goto next;
        }

        display_data = g_humiture_data;
        bool have_uart_text = false;
        if (g_uart_text_count > 0)
        {
            memcpy(uart_text, g_uart_text_queue[g_uart_text_head], sizeof(uart_text));
            g_uart_text_head = (g_uart_text_head + 1) % HUMITURE_UART_TEXT_QUEUE_DEPTH;
            g_uart_text_count--;
            have_uart_text = true;
        }
        qosa_mutex_unlock(g_humiture_data_mutex);

        eink_display_refresh_data(&display_data, &first_flag,
                      have_uart_text ? uart_text : QOSA_NULL);
        continue;
    next:
        qosa_task_sleep_ms(100);
    }
}

void unir_display_humiture_demo_init(void)
{
    HUMITURE_LOGI("Demo starting");

    if (g_humiture_data_mutex == QOSA_NULL &&
        qosa_mutex_create(&g_humiture_data_mutex) != QOSA_ERROR_OK)
    {
        HUMITURE_LOGE("Data mutex creation failed");
        return;
    }
    if (g_humiture_data_semaphore == QOSA_NULL &&
        qosa_sem_create(&g_humiture_data_semaphore, 0) != QOSA_ERROR_OK)
    {
        HUMITURE_LOGE("Data semaphore creation failed");
        return;
    }
    if (g_unir_eink_display_task == QOSA_NULL)
    {
        qosa_task_create(
            &g_unir_eink_display_task,         // Task handle pointer
            UNIR_EINK_DISPLAY_TASK_STACK_SIZE, // Task stack size
            UNIR_EINK_DISPLAY_TASK_PRIO,       // Task priority
            "unir_eink_display_task",          // Task name
            humiture_display_task,             // Task handler
            QOSA_NULL);                        // Task parameter
    }

    if (g_unir_aht20_collect_task == QOSA_NULL)
    {
        qosa_task_create(
            &g_unir_aht20_collect_task,         // Task handle pointer
            UNIR_AHT20_COLLECT_TASK_STACK_SIZE, // Task stack size
            UNIR_AHT20_COLLECT_TASK_PRIO,       // Task priority
            "unir_aht20_collect_task",          // Task name
            humiture_sensor_task,               // Task handler
            QOSA_NULL);                         // Task parameter
    }
}

UNIRTOS_APP_EXPORT(700, "unir_display_humiture_demo", unir_display_humiture_demo_init);