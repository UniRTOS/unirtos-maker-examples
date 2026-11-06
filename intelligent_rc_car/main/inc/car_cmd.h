#ifndef __CAR_CMD_H__
#define __CAR_CMD_H__

/**
 * @file car_cmd.h
 * @brief 云端控车命令协议：JSON 文本 -> 命令枚举 -> 目标板 UART 单字节指令。
 *
 * JSON 解析使用 SDK 自带的 Q_cJSON。
 * 下行格式：{"cmd":"forward","seq":12}，seq 可选。
 */

typedef enum
{
    CAR_CMD_FORWARD = 0, /*!< 前进（不避障） -> 'F' */
    CAR_CMD_BACKWARD,    /*!< 后退 -> 'B' */
    CAR_CMD_LEFT,        /*!< 左转 -> 'L' */
    CAR_CMD_RIGHT,       /*!< 右转 -> 'R' */
    CAR_CMD_STOP,        /*!< 停车 -> 'S' */
    CAR_CMD_SPEED_UP,    /*!< 速度 +1 档（+10%） -> '+' */
    CAR_CMD_SPEED_DOWN,  /*!< 速度 -1 档（-10%） -> '-' */
    CAR_CMD_AUTO,        /*!< 自动前进（目标板超声波避障） -> 'A' */
    CAR_CMD_INVALID,     /*!< 无法解析或未知命令 */
} car_cmd_e;

/**
 * @brief 解析云端下发的 JSON 命令。
 *
 * @param[in] json 以 '\0' 结尾的 JSON 文本，可为 NULL。
 * @param[out] seq 输出 "seq" 字段（非负整数），缺失或非法时为 -1；可为 NULL。
 * @return car_cmd_e 命令枚举，无法识别时返回 CAR_CMD_INVALID。
 */
car_cmd_e car_cmd_parse(const char *json, int *seq);

/**
 * @brief 命令对应的目标板 UART 字节。
 *
 * @param[in] cmd 命令枚举。
 * @return char UART 字节，CAR_CMD_INVALID 返回 0。
 */
char car_cmd_to_uart_byte(car_cmd_e cmd);

/**
 * @brief 命令的协议名（即 JSON 中的 "cmd" 值）。
 *
 * @param[in] cmd 命令枚举。
 * @return const char* 命令名，CAR_CMD_INVALID 返回 ""。
 */
const char *car_cmd_name(car_cmd_e cmd);

#endif /* __CAR_CMD_H__ */
