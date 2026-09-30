#ifndef __ALARM_APP_H__
#define __ALARM_APP_H__

/**
 * @file alarm_app.h
 * @brief 应用层接口：应用启动、事件投递与联系人表转发。
 *
 * 各层接口已按职责拆分为独立头文件，请按需包含：
 *   - alarm_core.h    状态机层（会话管理、事件处理）
 *   - alarm_service.h 服务层（IMS 通话、短信群发）
 *   - alarm_feedback.h 反馈层（LED/蜂鸣器/震动/TTS/电量）
 *   - alarm_machine.h 外设层（SOS 按键、ASR 串口）
 *   - alarm_audio.h   音频基础设施（外置 Codec）
 *   - alarm_cloud.h   云配置通道与联系人存储
 */

#include "alarm_types.h"

/**
 * @brief 初始化报警应用。
 *
 * 创建报警主任务与事件队列；重复调用会被忽略。
 */
void alarm_init(void);

/**
 * @brief 向报警状态机投递事件。
 *
 * @param[in] event 事件指针，类型为 alarm_event_t。
 * @return int 返回 qosa_msgq_release 的结果码；-1 表示参数或队列无效。
 * @note 使用无等待模式，队列满时会丢弃并打印日志。
 */
int alarm_post_event(const alarm_event_t *event);

/**
 * @brief 获取联系人表。
 *
 * @param[out] count 联系人数输出指针，可为 QOSA_NULL。
 * @return const alarm_contact_t* 联系人数组首地址。
 * @note 实际数据由联系人存储模块维护，可被云端下发配置覆盖。
 */
const alarm_contact_t *alarm_get_contacts(qosa_uint8_t *count);

#endif /* __ALARM_APP_H__ */
