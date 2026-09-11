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

#define QOSA_SMS_DEMO_SCA_RETRY     3   /*!< SMSC read retry count */
#define QOSA_SMS_DEMO_SCA_RETRY_GAP 1   /*!< SMSC read retry interval (seconds) */

qosa_uint8_t qosa_sms_simid = 0;

#define TARGET_PHONE_NUMBER "13532640348"   //Target phone number to which the SMS will be sent

#define SEND_TEXT_MESSAGE "hello,这是一条测试短信"   //Text content of the SMS message to be sent, including Chinese and English characters

/*
    SMS service center number (SMSC / 短信中心号码).

    Leave it as QOSA_NULL to use the service center stored on the SIM card, which is
    what a normal phone SIM already carries. Set it to your own operator's number if
    the inserted SIM card has no service center number: such a card makes every send
    fail with QOSA_SMS_ERROR(0x800a8001).

    The number is applied with qosa_sms_set_sca() when this macro is not QOSA_NULL.
    Note that this writes the value into the SIM card, so it must be the service
    center of the operator the card belongs to.

    Examples: China Mobile "+8613800100500", China Unicom "+8613010112500".

    example: #define SMS_SERVICE_CENTER_NUMBER "+8613800100500"
*/
#define SMS_SERVICE_CENTER_NUMBER QOSA_NULL

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

        //QOSA_SMS_ERROR(0x800a8001) is what the platform returns when the PDU
        //cannot be completed, and a missing SMSC is the most common cause.
        if ((qosa_uint32_t)cnf->err_code == (qosa_uint32_t)QOSA_SMS_ERROR)
        {
            QLOGI("[SMS DEMO]Check the SMSC with AT+CSCA?, and set it with:");
            QLOGI("[SMS DEMO]  AT+CSCA=\"+86<your operator SMSC>\",145");
        }
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
    Name: unir_sms_demo_apply_sca
    Description:Apply SMS_SERVICE_CENTER_NUMBER to the module when the user configured
                one. The platform reads the service center from the SIM card only, so
                a SIM card without a service center number cannot send any message.
                Setting it here lets the demo work without any AT command.

                When SMS_SERVICE_CENTER_NUMBER is QOSA_NULL this does nothing and the
                value stored on the SIM card is used as is.
    @param None
*/
static void unir_sms_demo_apply_sca(void)
{
    const char             *sca_text = SMS_SERVICE_CENTER_NUMBER;
    char                    sca_buf[QOSA_ADDRESS_MAX_LEN * 4 + 1] = {0};
    qosa_sms_address_info_t sca = {0};

    if (sca_text == QOSA_NULL)
    {
        //Use the service center stored on the SIM card.
        return;
    }

    qosa_strcpy(sca_buf, sca_text);

    //0x91 means the international number format, it also adds a '+' when read back.
    if (QOSA_SMS_SUCCESS != qosa_sms_text_to_address(sca_buf, (qosa_uint16_t)qosa_strlen(sca_buf), 0x91, &sca))
    {
        QLOGI("[SMS DEMO]SMSC \"%s\" is invalid, check SMS_SERVICE_CENTER_NUMBER", sca_buf);
        return;
    }

    //Store it into the module, the value is used to complete every outgoing PDU.
    if (QOSA_SMS_SUCCESS != qosa_sms_set_sca(qosa_sms_simid, &sca))
    {
        QLOGI("[SMS DEMO]set SMSC \"%s\" failed", sca_buf);
        return;
    }

    QLOGI("[SMS DEMO]SMSC set to %s", sca_buf);
}

/*
    Name: unir_sms_demo_check_sca
    Description:Read-only check of the SMS service center address (SMSC / 短信中心号码)
                stored on the SIM card.

                The platform reads the SMSC from the SIM card only (the NVRAM path is
                compiled out), so a card that carries no SMSC makes every outgoing
                message fail with QOSA_SMS_ERROR(0x800a8001) and no further hint.

                This function never writes to the SIM or to NVRAM, it only reports the
                value that was found and the AT command to fix it when it is missing.
    @param None
*/
static void unir_sms_demo_check_sca(void)
{
    qosa_sms_address_info_t sca = {0};
    char                    sca_text[QOSA_ADDRESS_MAX_LEN * 4 + 1] = {0};
    int                     retry = 0;

    //The SMS module reads the SMSC from the SIM asynchronously while it is being
    //initialized, so wait for the network first and retry a few times before
    //concluding that the card really carries no SMSC.
    if (!qosa_datacall_wait_attached(qosa_sms_simid, QOSA_SMS_DEMO_WAIT_ATTACH_TIMEOUT))
    {
        QLOGI("[SMS DEMO]network attach fail, skip the SMSC check");
        return;
    }

    for (retry = 0; retry < QOSA_SMS_DEMO_SCA_RETRY; retry++)
    {
        qosa_memset(&sca, 0, sizeof(sca));

        if ((QOSA_SMS_SUCCESS == qosa_sms_get_sca(qosa_sms_simid, &sca)) && (sca.address_len != 0))
        {
            break;
        }

        qosa_task_sleep_sec(QOSA_SMS_DEMO_SCA_RETRY_GAP);
    }

    if (sca.address_len != 0)
    {
        qosa_sms_address_to_text(&sca, sca_text, sizeof(sca_text));
        QLOGI("[SMS DEMO]SMSC found on this SIM: %s", sca_text);
        return;
    }

    //No SMSC on the card. Report the problem and how to fix it.
    QLOGI("[SMS DEMO]------------ SMSC MISSING ------------");
    QLOGI("[SMS DEMO]No SMS service center number on this SIM card.");
    QLOGI("[SMS DEMO]Sending SMS will fail with error 0x800a8001.");
    QLOGI("[SMS DEMO]Fix it with either of the two ways below:");
    QLOGI("[SMS DEMO]1) set SMS_SERVICE_CENTER_NUMBER in main/src/sms.c");
    QLOGI("[SMS DEMO]   to the service center of your own operator, example:");
    QLOGI("[SMS DEMO]   #define SMS_SERVICE_CENTER_NUMBER \"+8613800100500\"");
    QLOGI("[SMS DEMO]   then rebuild and reflash.");
    QLOGI("[SMS DEMO]2) set it over AT once, then retry:");
    QLOGI("[SMS DEMO]   AT+CSCA=\"+86<your operator SMSC>\",145");
    QLOGI("[SMS DEMO]--------------------------------------");
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

    //Read-only: report the SMSC of the inserted SIM before sending anything.
    unir_sms_demo_apply_sca();
    unir_sms_demo_check_sca();
    QLOGV("[SMS DEMO]Send SMS to " TARGET_PHONE_NUMBER);
    QLOGV("[SMS DEMO] running... count:%d", count);
    ret = unir_sms_demo_send_all_characters_sms(TARGET_PHONE_NUMBER, SEND_TEXT_MESSAGE);
    if(ret != 0)
    {
        QLOGI("[SMS DEMO]Failed to send SMS");
    }
    qosa_task_sleep_sec(60);  
}

/*
    Name: unir_sms_demo_init
    Description: Initialize the SMS Demo, create a task to run the demo.
    @param None
*/
void unir_sms_demo_init(void)
{
    // Log the entry of the SMS Demo initialization
    QLOGV("[SMS DEMO]enter SMS DEMO !!!");

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