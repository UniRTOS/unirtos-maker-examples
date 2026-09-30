#include <string.h>

#include "qosa_def.h"
#include "qosa_log.h"
#include "qosa_event_notify.h"
#include "qosa_voice_call.h"

#define QOS_LOG_TAG LOG_TAG_DEMO

#include "alarm_app.h"
#include "alarm_audio.h"
#include "alarm_config.h"
#include "alarm_service.h"

#include "qosa_at_config.h"
#include "qosa_sms.h"

/**
 * @brief IMS 事件回调。
 *
 * @param[in] user_argv 用户参数，传入事件 ID。
 * @param[in] argv 事件数据，本模块仅关心事件类型。
 * @return int 始终返回 0。
 */
static int ims_event_cb(void *user_argv, void *argv)
{
    alarm_event_t      event = {0};
    qosa_notify_event_e event_id = (qosa_notify_event_e)(qosa_ptr)user_argv;

    QOSA_UNUSED(argv);

    switch (event_id)
    {
        case QOSA_EVENT_MODEM_IMS_RING_STATUS:
            event.type = ALARM_EVENT_CALL_RING;
            break;

        case QOSA_EVENT_MODEM_IMS_CONN_ID_EVNET:
            event.type = ALARM_EVENT_CALL_CONNECTED;
            break;

        case QOSA_EVENT_MODEM_IMS_DISCONNECT_STATUS:
            event.type = ALARM_EVENT_CALL_DISCONNECTED;
            break;

        default:
            return 0;
    }

    (void)alarm_post_event(&event);
    return 0;
}

/**
 * @brief 短信发送回调。
 *
 * @param[in] ctx 用户上下文，存放联系人下标。
 * @param[in] argv 发送结果参数，类型为 qosa_sms_send_pdu_cnf_t。
 * @return void
 */
static void sms_send_cb(void *ctx, void *argv)
{
    alarm_event_t         event = {0};
    qosa_sms_send_pdu_cnf_t *cnf = (qosa_sms_send_pdu_cnf_t *)argv;

    event.type = ALARM_EVENT_SMS_RESULT;
    event.data.sms.contact_index = (qosa_uint8_t)(qosa_ptr)ctx;
    event.data.sms.err_code = (cnf == QOSA_NULL) ? -1 : cnf->err_code;

    (void)alarm_post_event(&event);
}

#if defined(CONFIG_QOSA_SMS_FUNC)
/**
 * @brief 将 UTF-8 文本转换为 UCS2 大端 hex 字符串。
 *
 * 参考 easy_sms_demos：中文短信必须以 UCS2 编码发送，SDK 的 text->PDU 转换
 * 要求 data 字段存放预编译的 UCS2 hex 字符串（每个字符 4 个 hex 字符）。
 *
 * @param[in] utf8 UTF-8 源文本。
 * @param[out] hex_out hex 字符串输出缓冲。
 * @param[in] hex_out_size 输出缓冲大小（含字符串结尾符）。
 * @return size_t hex 字符串长度；0 表示转换失败（含 emoji 等非 BMP 字符）。
 * @note 输出缓冲写满时剩余字符将被截断。
 */
static size_t sms_utf8_to_ucs2_hex(const char *utf8, char *hex_out, size_t hex_out_size)
{
    static const char hex_digits[] = "0123456789ABCDEF";
    size_t            out_len = 0;
    qosa_uint8_t      index = 0;

    while (*utf8 != '\0')
    {
        qosa_uint32_t codepoint = 0;
        qosa_uint8_t  lead = (qosa_uint8_t)*utf8;
        qosa_uint8_t  seq_len = 0;

        if (lead < 0x80)
        {
            codepoint = lead;
            seq_len = 1;
        }
        else if ((lead & 0xE0) == 0xC0)
        {
            codepoint = lead & 0x1F;
            seq_len = 2;
        }
        else if ((lead & 0xF0) == 0xE0)
        {
            codepoint = lead & 0x0F;
            seq_len = 3;
        }
        else
        {
            /* 4 字节 UTF-8 字符超出 UCS2 表达能力，视为转换失败。 */
            return 0;
        }

        if (out_len + 4 + 1 > hex_out_size)
        {
            break;
        }

        for (index = 1; index < seq_len; index++)
        {
            qosa_uint8_t cont = (qosa_uint8_t)utf8[index];

            if ((cont & 0xC0) != 0x80)
            {
                return 0;
            }
            codepoint = (codepoint << 6) | (cont & 0x3F);
        }
        utf8 += seq_len;

        hex_out[out_len++] = hex_digits[(codepoint >> 12) & 0x0F];
        hex_out[out_len++] = hex_digits[(codepoint >> 8) & 0x0F];
        hex_out[out_len++] = hex_digits[(codepoint >> 4) & 0x0F];
        hex_out[out_len++] = hex_digits[codepoint & 0x0F];
    }

    hex_out[out_len] = '\0';
    return out_len;
}

/**
 * @brief 给单个联系人发送报警短信。
 *
 * @param[in] phone_number 联系人号码。
 * @param[in] contact_index 联系人下标。
 * @return int QOSA_SMS_SUCCESS 表示提交成功，其他值为失败。
 */
static int send_one_sms(const char *phone_number, qosa_uint8_t contact_index)
{
    qosa_sms_msg_t        message = {0};
    qosa_sms_send_param_t send_param = {0};
    qosa_sms_record_t     record = {0};
    qosa_sms_err_e        sms_ret = QOSA_SMS_SUCCESS;
    /* 单条 UCS2 短信用户数据上限 140 字节，即 70 个字符、280 个 hex 字符。 */
    char                  ucs2_hex[QOSA_SMS_USER_DATA_LEN * 2 + 1] = {0};
    size_t                hex_len = 0;

    if (phone_number == QOSA_NULL)
    {
        return -1;
    }

    /* 中文内容需走 UCS2：先把 UTF-8 文本转成 UCS2 hex 字符串（参考 easy_sms_demos）。 */
    hex_len = sms_utf8_to_ucs2_hex(ALARM_SMS_TEXT, ucs2_hex, sizeof(ucs2_hex));
    if (hex_len == 0)
    {
        QLOGW("[alarm] sms text utf8->ucs2 convert failed");
        return -1;
    }

    message.msg_type = QOSA_SMS_SUBMIT;
    message.text.send.status = 0xff;
    strcpy(message.text.send.da, phone_number);
    message.text.send.toda = 129;
    strcpy((char *)message.text.send.data, ucs2_hex);
    message.text.send.data_len = (qosa_uint16_t)hex_len;
    message.text.send.fo = 0x11;
    message.text.send.pid = 0x00;
    message.text.send.dcs = 0x08;
    message.text.send.vp = 0xAA;
    message.text.send.data_chset = QOSA_CS_UCS2;
    message.is_concatenated = QOSA_FALSE;

    (void)qosa_sms_set_charset(QOSA_CS_GSM);

    /* 统一走 text->PDU 转换，复用 SDK 发送接口。 */
    sms_ret = qosa_sms_text_to_pdu(&message, &record);
    if (sms_ret != QOSA_SMS_SUCCESS)
    {
        QLOGW("[alarm] sms text_to_pdu failed ret=%d", sms_ret);
        return sms_ret;
    }

    send_param.pdu.data_len = record.pdu.data_len;
    memcpy(send_param.pdu.data, record.pdu.data, record.pdu.data_len);

    sms_ret = qosa_sms_send_pdu_async(ALARM_SIM_ID,
                                      &send_param,
                                      sms_send_cb,
                                      (void *)(qosa_ptr)contact_index);
    if (sms_ret != QOSA_SMS_SUCCESS)
    {
        QLOGW("[alarm] sms send async failed ret=%d", sms_ret);
    }

    return sms_ret;
}
#endif

/**
 * @brief 初始化服务层（音频 Codec + IMS 事件注册）。
 *
 * @return int 0 表示成功；-1 表示存在事件注册失败或 Codec 初始化异常。
 */
int service_init(void)
{
    int ret = 0;

    /* Codec 由音频模块统一负责，服务层只关心失败与否。 */
    if (alarm_codec_init() != 0)
    {
        ret = -1;
    }

    if (qosa_event_notify_register(QOSA_EVENT_MODEM_IMS_RING_STATUS,
                                   ims_event_cb,
                                   (void *)QOSA_EVENT_MODEM_IMS_RING_STATUS) != QOSA_EVENT_NOTIFY_OK)
    {
        ret = -1;
    }

    if (qosa_event_notify_register(QOSA_EVENT_MODEM_IMS_DISCONNECT_STATUS,
                                   ims_event_cb,
                                   (void *)QOSA_EVENT_MODEM_IMS_DISCONNECT_STATUS) != QOSA_EVENT_NOTIFY_OK)
    {
        ret = -1;
    }

    if (qosa_event_notify_register(QOSA_EVENT_MODEM_IMS_CONN_ID_EVNET,
                                   ims_event_cb,
                                   (void *)QOSA_EVENT_MODEM_IMS_CONN_ID_EVNET) != QOSA_EVENT_NOTIFY_OK)
    {
        ret = -1;
    }

    return ret;
}

/**
 * @brief 发起语音通话。
 *
 * @param[in] phone_number 目标号码。
 * @return int 0 表示请求成功，非 0 表示失败。
 */
int service_start_call(const char *phone_number)
{
    if (phone_number == QOSA_NULL)
    {
        return -1;
    }

    return qosa_start_voice_call(phone_number);
}

/**
 * @brief 停止语音通话。
 *
 * @return int 0 表示请求成功，非 0 表示失败。
 */
int service_stop_call(void)
{
    return qosa_stop_voice_call(ALARM_SIM_ID);
}

/**
 * @brief 对联系人列表执行短信群发。
 *
 * @param[in] contacts 联系人数组。
 * @param[in] count 联系人数。
 * @return void
 * @note 当前实现采用逐联系人异步发送，不阻塞主状态机。
 */
void service_start_sms_fanout(const alarm_contact_t *contacts, qosa_uint8_t count)
{
#if defined(CONFIG_QOSA_SMS_FUNC)
    qosa_uint8_t index = 0;

    for (index = 0; index < count; index++)
    {
        /* 单个联系人失败不会中断后续联系人发送。 */
        (void)send_one_sms(contacts[index].phone_number, index);
    }
#else
    QOSA_UNUSED(contacts);
    QOSA_UNUSED(count);
    QLOGW("[alarm] sms fan-out skipped because CONFIG_QOSA_SMS_FUNC is disabled");
#endif
}
