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
#include "chatbot_app.h"
#include "qosa_dev_eigen.h"
#include "chat_bus.h"
#include "chatbot_types.h"

/**
 * @brief AI 语音聊天机器人应用初始化入口。
 *
 * 输出版本信息后启动 chatbot 应用，详见 chatbot_app.c 的分层初始化流程。
 *
 * @return void
 */
void unir_chatbot_demo_init(void)
{
    if(qosa_gpio_set_voltage(VOL_3_30V) != (qosa_gpio_error_e)QOSA_OK) {
        QLOGE("[chatbot] failed to set GPIO voltage");
    }

    // 打印启动信息，便于串口定位应用启动时刻。
    QLOGV("[chatbot] enter AI chatbot demo !!!");
    QLOGV("[chatbot] sdk version(tag): %s", qosa_sdk_get_version());
    QLOGV("[chatbot] fw version: %s", qosa_get_fw_version());

    // 初始化 AI 语音聊天机器人应用（核心状态机、音频层、通信层、工具层等）。
    chatbot_init();
    // 预留网络与外设就绪时间，避免启动瞬间初始化导致不稳定。
    // qosa_task_sleep_sec(8);
    // chat_bus_post_event(&(chat_event_t){
    //     .type = CHAT_EVT_WAKE_DETECTED,
    //     .text = ""
    // });
}
UNIRTOS_APP_EXPORT(700, "unir_chatbot_demo", unir_chatbot_demo_init);