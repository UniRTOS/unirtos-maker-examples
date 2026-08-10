/*****************************************************************/ /**
* @file sms.c
* @brief
* A demo of the SMS sending function shows how to use the QOSA SMS interface to send SMS messages containing both Chinese and English characters.
* @author lysander.li@quectel.com
* @date 2026-03-26
*
**********************************************************************/

#include "qosa_sys.h"
#include "qosa_def.h"
#include "qosa_log.h"
#include "qosa_datacall.h"          // DataCall functions
#include "qosa_sms.h"               // SMS functions
#include "qosa_system_utils.h"
#include "qosa_at_config.h"
#include "unirtos_app_init_registry.h"
#include "sms.h"

#define QOS_LOG_TAG   LOG_TAG_DEMO

#define UniRTOS_TEST_DEMO_TASK_STACK_SIZE 4096  // Task stack size 4KB

#define UniRTOS_TEST_DEMO_TASK_PRIO QOSA_PRIORITY_NORMAL // Normal priority

static qosa_task_t sms_demo_task = QOSA_NULL;

#define QOSA_SMS_DEMO_WAIT_ATTACH_TIMEOUT       300     /*!< Network registration timeout (seconds) */

qosa_uint8_t qosa_sms_simid = 0;

#define TARGET_PHONE_NUMBER "13532640348"   //Target phone number to which the SMS will be sent

#define SEND_TEXT_MESSAGE "hello,这是一条测试短信"   //Text content of the SMS message to be sent, including Chinese and English characters

/*
    Name: unir_sms_demo_send_msg_rsp
    Description:SMS function callback for sending message, this callback will be called when the send message result is received.
    @param ctx: The context of the callback function.
    @param argv: The arguments for the callback function.
*/
static void unir_sms_demo_send_msg_rsp(void *ctx, void *argv)
{
    qosa_sms_send_pdu_cnf_t *cnf = argv;

    QLOGI("[SMS DEMO]result sim:%d err:0x%x", qosa_sms_simid, cnf->err_code);

    if (QOSA_SMS_SUCCESS != cnf->err_code)
    {
        QLOGI("[SMS DEMO]Send SMS failed with error:%d", cnf->err_code);   // Log failure
    }
    else
    {
        QLOGI("[SMS DEMO]Send SMS success, MR:%u", cnf->mr);   // Log success with message reference
    }
}

/*
    Name: unir_sms_demo_send_all_characters_sms
    Description: Send a SMS message with all characters, including Chinese and English characters.
    @param phone_number: The destination phone number to which the SMS will be sent.
    @param message_txt: The text content of the SMS message to be sent.
    @return: 0 on success, -1 on failure.

    example: unir_sms_demo_send_all_characters_sms("10086", "你好hello")
*/
static int unir_sms_demo_send_all_characters_sms(const char *phone_number, const char *message_txt)
{
    int                     qosa_err = QOSA_SMS_SUCCESS;
    qosa_uint8_t            qosa_sms_simid = 0;
    qosa_bool_t             is_attached = 0;
    qosa_uint16_t           message_len = 0;
    qosa_sms_msg_t          message = {0};
    qosa_sms_send_param_t   send_param = {0};
    qosa_sms_cfg_t          sms_conf = {0};
    qosa_uint8_t           *pdu_with_sca = QOSA_NULL;
    qosa_uint8_t            pdu_with_sca_len = 0;
    qosa_sms_record_t       record = {0};
    qosa_size_t             max_hex_size = 0;
    char                   *message_ucs2_string = QOSA_NULL;
    if( (message_txt == QOSA_NULL) || (phone_number == QOSA_NULL) )
    {
        return -1;
    }
    is_attached = qosa_datacall_wait_attached(qosa_sms_simid, QOSA_SMS_DEMO_WAIT_ATTACH_TIMEOUT);
    if (!is_attached)
    {
        QLOGI("[SMS DEMO]attach fail");
        return -1;
    }
    // required max buffer size for UCS-2 conversion
    max_hex_size = (qosa_strlen(message_txt) * 4) + 1;
    message_ucs2_string = (char*)qosa_malloc(max_hex_size);
    if(!message_ucs2_string)
    {
       QLOGI("[SMS DEMO]malloc fail");
       return -1; 
    }
    // Convert UTF-8 string to UCS-2 encoding
    if(qosa_sms_utf8_to_ucs2(message_txt, message_ucs2_string, max_hex_size) != QOSA_SMS_SUCCESS)
    {
       QLOGI("[SMS DEMO]qosa_sms_utf8_to_ucs2 fail");
       qosa_free(message_ucs2_string);
       return -1;
    }
    message_len = qosa_strlen(message_ucs2_string);
    if (message_len == 0)
    {
        return -1;
    }
    //DA support QOSA_CS_GSM charset only.
    // qosa_sms_set_charset(QOSA_CS_GSM);
    // Fill in basic information for text messages.
    message.msg_type = QOSA_SMS_SUBMIT;
    message.text.send.status = 0xff;
    qosa_strcpy(message.text.send.da, (const char *)phone_number);
    message.text.send.toda = 129;
    qosa_strcpy((char *)message.text.send.data, (const char *)message_ucs2_string);
    message.text.send.data_len = message_len;
    // DATA support QOSA_CS_UCS2 charset
    message.text.send.data_chset = QOSA_CS_UCS2;
    
    // Copy the text message configuration information.
    qosa_sms_get_config(qosa_sms_simid, &sms_conf);
    message.text.send.fo = sms_conf.text_fo;
    message.text.send.pid = sms_conf.text_pid;
    message.text.send.vp = sms_conf.text_vp;
    // If the messages is UCS2 encoding. the PDU is configured as 16bit, the dcs set 0x08
    message.text.send.dcs = 0x08;
    // Copy concatenated SMS information.
    message.is_concatenated = QOSA_FALSE;
    message.concat.msg_ref_number = 0;
    message.concat.msg_seg = 0;
    message.concat.msg_total = 0;
    do {
        // Convert text message to pdu message
        qosa_err = qosa_sms_text_to_pdu(&message, &record);
        if (qosa_err != QOSA_SMS_SUCCESS)
        {
            break;
        }
        pdu_with_sca = record.pdu.data;
        pdu_with_sca_len = record.pdu.data_len; 
    
        send_param.pdu.data_len = pdu_with_sca_len;
        qosa_memcpy(send_param.pdu.data, pdu_with_sca, pdu_with_sca_len);
        qosa_err = qosa_sms_send_pdu_async(qosa_sms_simid, &send_param, unir_sms_demo_send_msg_rsp, pdu_with_sca);
    } while (0);
    
    qosa_free(message_ucs2_string);
    
    if(QOSA_SMS_SUCCESS != qosa_err)
    {
        QLOGI("[SMS DEMO]SEND SMS FAILED qosa_err:%d",qosa_err);
        return -1;
    }
    return 0;
}



/*
    Name: unir_sms_demo_process
    Description: The entry function of the SMS Demo task. 1 minute interval to send a SMS message.
    @param ctx: The context of the task.
*/
static void unir_sms_demo_process(void *ctx)
{
    int count = 0;
    int ret = 0;
    while(1)
    {
        count++;
        if(count > 10)
        {
            break;
        }
        QLOGV("[SMS DEMO]Send SMS to " TARGET_PHONE_NUMBER);
        QLOGV("[SMS DEMO] running... count:%d", count);
        ret = unir_sms_demo_send_all_characters_sms(TARGET_PHONE_NUMBER, SEND_TEXT_MESSAGE);
        if(ret != 0)
        {
            QLOGI("[SMS DEMO]Failed to send SMS");
        }
        qosa_task_sleep_sec(60);  
    }
}

/*
    Name: unir_sms_demo_init
    Description: Initialize the SMS Demo, create a task to run the demo.
    @param None
*/
void unir_sms_demo_init(void)
{
    // Log the entry of the SMS Demo initialization
    QLOGV("enter SMS DEMO !!!");

    // Create a task for the SMS Demo using qosa_task_create, with specified stack size, priority, name, and entry function
    if (sms_demo_task == QOSA_NULL) // Check if the SMS Demo task has already been created
    {       
        
        qosa_task_create(
            &sms_demo_task,
            UniRTOS_TEST_DEMO_TASK_STACK_SIZE,     // Task stack size
            UniRTOS_TEST_DEMO_TASK_PRIO,           // Task priority
            "sms_demo",                            // Task name
            unir_sms_demo_process,                // Task entry function
            QOSA_NULL                             // Task context (not used in this case
        );
    }
}

UNIRTOS_APP_EXPORT(700, "unir_sms_demo", unir_sms_demo_init);