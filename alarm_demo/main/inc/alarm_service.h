#ifndef __ALARM_SERVICE_H__
#define __ALARM_SERVICE_H__

/**
 * @file alarm_service.h
 * @brief 服务层接口：IMS 语音通话与短信群发。
 *
 * 本层把底层 IMS/SMS 事件翻译成报警事件后投递到队列，不直接修改会话状态。
 * 音频 Codec 已独立到 alarm_audio.h，避免反馈层反向依赖本层。
 */

#include "alarm_types.h"

/**
 * @brief 初始化业务服务模块（音频 Codec + IMS 事件注册）。
 *
 * @return int 0 表示成功，非 0 表示部分能力降级。
 */
int service_init(void);

/**
 * @brief 发起语音呼叫。
 *
 * @param[in] phone_number 目标号码字符串。
 * @return int 0 表示发起成功，非 0 表示失败。
 */
int service_start_call(const char *phone_number);

/**
 * @brief 停止当前语音呼叫。
 *
 * @return int 0 表示请求成功，非 0 表示失败。
 */
int service_stop_call(void);

/**
 * @brief 对联系人执行短信群发。
 *
 * @param[in] contacts 联系人数组。
 * @param[in] count 联系人数。
 * @note 逐联系人异步提交，单个失败不影响后续联系人，也不阻塞主状态机。
 */
void service_start_sms_fanout(const alarm_contact_t *contacts, qosa_uint8_t count);

#endif /* __ALARM_SERVICE_H__ */
