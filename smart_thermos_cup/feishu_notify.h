#ifndef __FEISHU_NOTIFY_H__
#define __FEISHU_NOTIFY_H__

/* 创建消息队列与推送任务，成功返回 0 */
int feishu_notify_init(void);

/* 把一条文本消息放入发送队列，立即返回（实际联网发送在后台任务完成，不阻塞调用者）。
   成功入队返回 0；队列已满或未初始化返回 -1。text 会被拷贝，超长部分截断。 */
int feishu_notify_send(const char *text);

#endif /* __FEISHU_NOTIFY_H__ */
