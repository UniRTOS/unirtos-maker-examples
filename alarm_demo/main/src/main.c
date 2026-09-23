//QOSA core definition header
#include "qosa_def.h"
//Include QOSA system API header
#include "qosa_sys.h"
//Include QOSA log system header file
#include "qosa_log.h"

//Define log information
#define QOS_LOG_TAG   LOG_TAG_DEMO
#include "qosa_sdk_version.h"
#include "unirtos_app_init_registry.h"
#include "alarm_app.h"
#include "qosa_dev_eigen.h"

/**
 * @brief 报警器示例初始化入口。
 *
 * 设置 GPIO 电压、输出版本信息，随后启动报警应用。
 *
 * @return void
 * @note 本函数只负责应用启动，不含任何触发注入逻辑；报警由 SOS 按键中断
 *       或 ASR 串口关键词驱动，具体处理在状态机中完成。
 */
void unir_alarm_demo_init(void)
{
    if(qosa_gpio_set_voltage(VOL_3_30V) != QOSA_OK) {
        QLOGE("[alarm] failed to set GPIO voltage");
    }

    // 打印启动信息，便于串口定位应用启动时刻。
    QLOGV("[alarm] enter alarm demo !!!");
    QLOGV("[alarm] sdk version(tag): %s", qosa_sdk_get_version());
    QLOGV("[alarm] fw version: %s", qosa_get_fw_version());

    // 初始化报警任务与消息队列（不阻塞：实质工作由 alarm_task 承担）。
    alarm_init();
}
UNIRTOS_APP_EXPORT(700, "unir_alarm_demo", unir_alarm_demo_init);