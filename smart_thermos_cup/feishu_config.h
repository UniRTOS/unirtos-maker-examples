#ifndef __FEISHU_CONFIG_H__
#define __FEISHU_CONFIG_H__

/* 飞书群「自定义机器人」Webhook 地址。
   注意：该地址等同于发消息的密码，不要提交到公开仓库，泄露后请在飞书机器人设置中重置。 */
#define FEISHU_WEBHOOK_URL   "https://open.feishu.cn/open-apis/bot/v2/hook/<你的webhook令牌>"

/* 若机器人开启了「自定义关键词」安全设置，请把关键词填在这里，会自动加在每条消息开头；未开启则留空 ""。
   当前留空：飞书机器人的关键词请设置为消息正文里已有的「智能保温杯」 */
#define FEISHU_KEYWORD       ""

/* 联网参数：SIM 卡槽 0，PDP 上下文 1 */
#define FEISHU_SIM_ID        0
#define FEISHU_PDP_ID        1

/* 等待驻网与拨号的超时(秒) */
#define FEISHU_ATTACH_TIMEOUT_S   60
#define FEISHU_DATACALL_TIMEOUT_S 30

/* 单次 HTTPS 请求超时(ms)、TLS 握手超时(s)、失败重试次数 */
#define FEISHU_HTTP_TIMEOUT_MS    30000
#define FEISHU_TLS_TIMEOUT_S      30
#define FEISHU_RETRY_COUNT        3
#define FEISHU_RETRY_DELAY_MS     5000

/* 推送任务栈大小（TLS 握手较耗栈） */
#define FEISHU_TASK_STACK_SIZE    16384

/* 单条消息最大长度（含结尾 0，UTF-8 字节数） */
#define FEISHU_TEXT_MAX           128

#endif /* __FEISHU_CONFIG_H__ */
