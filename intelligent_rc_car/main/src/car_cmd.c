#include <string.h>

#include "qosa_def.h"
#include "qosa_cJSON.h"

#include "car_cmd.h"

#define CAR_CMD_SEQ_MAX (999999999) /*!< seq 上限（9 位），超出视为无效 */

typedef struct
{
    const char *name;      /*!< JSON 中的 cmd 值 */
    char        uart_byte; /*!< 目标板 UART 字节 */
} car_cmd_entry_t;

/* 顺序与 car_cmd_e 一致。 */
static const car_cmd_entry_t k_cmd_table[CAR_CMD_INVALID] = {
    {"forward", 'F'},
    {"backward", 'B'},
    {"left", 'L'},
    {"right", 'R'},
    {"stop", 'S'},
    {"speed_up", '+'},
    {"speed_down", '-'},
    {"auto", 'A'},
};

static car_cmd_e lookup(const char *name)
{
    int i = 0;

    for (i = 0; i < (int)CAR_CMD_INVALID; i++)
    {
        if (strcmp(name, k_cmd_table[i].name) == 0)
        {
            return (car_cmd_e)i;
        }
    }
    return CAR_CMD_INVALID;
}

car_cmd_e car_cmd_parse(const char *json, int *seq)
{
    Q_cJSON       *root = QOSA_NULL;
    const Q_cJSON *item = QOSA_NULL;
    car_cmd_e      cmd = CAR_CMD_INVALID;

    if (seq != QOSA_NULL)
    {
        *seq = -1;
    }
    if (json == QOSA_NULL)
    {
        return CAR_CMD_INVALID;
    }

    root = Q_cJSON_Parse(json);
    if (root == QOSA_NULL)
    {
        return CAR_CMD_INVALID;
    }

    /* seq 必须是 0..CAR_CMD_SEQ_MAX 的整数，否则视为缺失。 */
    item = Q_cJSON_GetObjectItemCaseSensitive(root, "seq");
    if ((seq != QOSA_NULL) && Q_cJSON_IsNumber(item) && (item->valuedouble >= 0) && (item->valuedouble <= CAR_CMD_SEQ_MAX)
        && (item->valuedouble == (double)item->valueint))
    {
        *seq = item->valueint;
    }

    item = Q_cJSON_GetObjectItemCaseSensitive(root, "cmd");
    if (Q_cJSON_IsString(item) && (item->valuestring != QOSA_NULL))
    {
        cmd = lookup(item->valuestring);
    }

    Q_cJSON_Delete(root);
    return cmd;
}

char car_cmd_to_uart_byte(car_cmd_e cmd)
{
    if ((cmd < CAR_CMD_FORWARD) || (cmd >= CAR_CMD_INVALID))
    {
        return 0;
    }
    return k_cmd_table[cmd].uart_byte;
}

const char *car_cmd_name(car_cmd_e cmd)
{
    if ((cmd < CAR_CMD_FORWARD) || (cmd >= CAR_CMD_INVALID))
    {
        return "";
    }
    return k_cmd_table[cmd].name;
}
