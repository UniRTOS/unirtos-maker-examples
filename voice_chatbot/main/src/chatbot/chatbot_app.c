#include "qosa_def.h"
#include "qosa_log.h"

#define QOS_LOG_TAG LOG_TAG_DEMO

#include "chatbot_app.h"
#include "peripherals.h"
#include "chatbot_tool.h"
#include "audio_pipeline.h"
#include "coze_network.h"
#include "chat_core.h"
#include "chat_bus.h"
/**
 * @brief 依次初始化各层。任一模块失败仅记录降级日志，不阻塞后续模块启动，
 *        避免单点故障导致整机无法运行（对应设计报告"能力降级"原则）。
 */
void chatbot_init(void)
{
    QLOGI("[chatbot] init start");

    if (coze_network_init() != 0)
    {
        QLOGW("[chatbot] coze_network_init failed, degraded: no network readiness detection");
    }

    if (wake_uart_init() != 0)
    {
        QLOGW("[chatbot] wake_uart_init failed, degraded: no voice wake capability");
    }

    if (indicator_init() != 0)
    {
        QLOGW("[chatbot] indicator_init failed, degraded: no LED feedback");
    }

    if (tool_registry_init_all() != 0)
    {
        QLOGW("[chatbot] tool_registry_init_all failed, degraded: no tool-calling capability");
    }

    if (codec_es8311_init() != 0)
    {
        QLOGW("[chatbot] codec_es8311_init failed at startup, will retry when session starts");
    }

    (void)audio_capture_init();
    (void)audio_player_init();

    if (chat_core_init() != 0)
    {
        QLOGE("[chatbot] chat_core_init failed, application cannot run");
        return;
    }

    if (talk_button_init() != 0)
    {
        QLOGW("[chatbot] talk_button_init failed, degraded: no push-to-talk capability");
    }

    QLOGI("[chatbot] init done, waiting for wake word");
}
