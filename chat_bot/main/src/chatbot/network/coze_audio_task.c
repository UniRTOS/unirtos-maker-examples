#include "qosa_def.h"
#include "qosa_sys.h"
#include "qosa_log.h"
#include "qosa_rtc.h"
#include "qcm_websocket.h"

#include <string.h>

#define QOS_LOG_TAG LOG_TAG_DEMO

#include "coze_internal.h"
#include "coze_network.h"
#include "chatbot_config.h"
#include "audio_pipeline.h"
#include "peripherals.h"
#include "chat_core.h"
#include "vad_lite.h"
#include "chatbot/audio/g711.h"

typedef struct
{
    qosa_uint8_t *data;
    int           len;
} audio_frame_t;

static qosa_task_t          g_uplink_task = QOSA_NULL;
static qosa_task_t          g_downlink_task = QOSA_NULL;
static qosa_msgq_t          g_downlink_queue = QOSA_NULL;
static volatile qosa_bool_t g_audio_active = QOSA_FALSE;
static qosa_bool_t          g_codec_ready = QOSA_FALSE;
static volatile qosa_bool_t g_ptt_pressed = QOSA_FALSE;
static volatile chat_session_mode_e g_session_mode = CHAT_SESSION_MODE_VOICE;
/*!< 本地是否正在播放下行音频：downlink_task 从队列取到帧即置 TRUE,
     队列空闲(1s 无帧已关流)/flush/stop 时置 FALSE。
     用于打断判定：服务端 chat_busy 反映"服务端是否有一轮对话进行中"，但开场白
     (prologue)及"服务端已发 conversation.chat.completed 而本地仍在播队列残留"的
     场景 busy 为 FALSE，此时用户按下 PTT 应仍能立即静音本地，否则要等播完。 */
static volatile qosa_bool_t g_downlink_playing = QOSA_FALSE;
/*!< 上/下行帧计数器：按会话重置（在 coze_audio_set_session_mode() 里清零），
     供节流打印条件 (==1 或 %50==0) 在每次新会话里都能重新命中首帧。 */
static qosa_uint32_t        g_uplink_frame_count = 0;
static qosa_uint32_t        g_downlink_frame_count = 0;

/*!< 打断(flush)后下行静音门控：cancel 上行后服务端确实会停止生成新内容，
     但 cancel 前服务端已下发/本地驱动 ring 里已排队的旧回答残余音频会继续被播完，
     表现为"按住打断后旧回答仍完整播完才播新回答"。定时窗口无法覆盖不定长的残余量。
     改用门控：flush 后置 muted=TRUE，downlink 丢弃一切到达帧且不重开播放流，喇叭立即静音；
     直到服务端开始新一轮回答（protocol 收到 conversation.audio.sentence_start 或
     conversation.chat.created/in_progress 时调用 coze_audio_downlink_resume() 解除）才恢复播放
     ——保证按住期间静音、松开后的新回答正常播出。 */
static volatile qosa_bool_t g_downlink_muted = QOSA_FALSE;

/*!< cancel 卡死兜底计时：muted(打断静音门控) 期间"连续无残余帧到达"的 1s 心跳数。
     downlink 任务以队列 1000ms 超时作为节拍器：每 1s 醒来若 muted 则本计数 +1；
     任一残余帧到达（push 丢弃点或 downlink 消费丢弃点）即清零——说明服务端仍在发
     旧轮音频，不能解除静音。计数达到 CHATBOT_CANCEL_STUCK_RESOLVE_SEC 说明服务端
     已实际停发残余但 canceled 回执丢失/迟到，强制解除静音并复位 cancel/busy 状态，
     避免本会话永久静音、后续对话全部被丢。跨线程共享，仅做整型读改写。 */
static volatile qosa_uint32_t g_muted_quiet_1s_cnt = 0;

/*!< muted 期间累计丢弃的残余帧计数（跨 push/downlink 两丢弃点累加）。
     打断残余期服务端持续下发旧帧，每帧一条 WARN 会刷屏，改为累计计数 +
     首帧/每 100 帧打印一次。 */
static volatile qosa_uint32_t g_muted_drop_cnt = 0;

/*!< 因"下行队列满 / 帧结构体分配失败"而丢弃的帧累计计数（与 muted 丢弃相互独立）。
     非 0 即说明服务端下发速率曾长期超过本地播放速率（突发灌包）——
     参见 chatbot_config.h 的 CHATBOT_COZE_DOWNLINK_LIMIT_*（限流）与
     CHATBOT_DOWNLINK_QUEUE_DEPTH（队列余量）。会话中途不清零，便于跨轮观察。 */
static volatile qosa_uint32_t g_downlink_drop_cnt = 0;

/*!< 上行是否正在说话/按住 PTT（统一判定）：用于采集流 stall 时选择加速自愈周期——
     BUTTON 模式看 g_ptt_pressed，VOICE(VAD) 模式看本地 VAD 判定的语音活跃。
     VOICE 模式在读到语音帧(send_audio==TRUE)时置位，语音结束(SPEECH_END/连续静音)后清除。 */
static volatile qosa_bool_t g_uplink_talking = QOSA_FALSE;

#define CHATBOT_CODEC_INIT_RETRY_MAX      9
#define CHATBOT_CODEC_INIT_RETRY_GAP_MS   500
#define CHATBOT_WS_PENDING_HIGH_WATER      8192
#define CHATBOT_WS_BACKPRESSURE_SLEEP_MS   20
/*!< 某些状态下采集流会卡在持续无数据，重开流可恢复。
     因此：非说话态 1s(100) 重开；PTT 按住/VAD 语音活跃期间 500ms(50) 即重开。 */
#define CHATBOT_CAPTURE_STALL_RECOVER_CYCLES     100
#define CHATBOT_CAPTURE_STALL_RECOVER_SPEECH_CYCLES 50

/*!< 下行预缓冲：蜂窝网络抖动可达数百毫秒，用 4 帧 / 600ms 窗口平滑抖动，
     队列深度（chatbot_config.h 的 CHATBOT_DOWNLINK_QUEUE_DEPTH）需同步容纳
     预缓冲期间堆积的帧。 */
#define CHATBOT_DOWNLINK_PREBUFFER_FRAMES    4
#define CHATBOT_DOWNLINK_PREBUFFER_WAIT_MS   600
/*!< 队列深度配置见 chatbot_config.h 的 CHATBOT_DOWNLINK_QUEUE_DEPTH（与下行限流
     参数同处，便于按内存余量统一调优）。 */

/*!< 打断 cancel 兜底超时：服务端偶发一直不下发 conversation.chat.canceled 回执
     （cancel 已生效、残余已停发，但确认事件丢失/迟到）。若本地静音门控仅靠
     canceled/新一轮事件解除，将永久静音，后续对话全部被丢。兜底：muted 期间若超过
     本秒数没有任何残余帧到达（drop 计数不再增长），视为服务端已实际停发该轮，
     强制解除静音并复位 cancel/busy 状态。muted 期间残余帧仍持续到达时(drop 计数持续增长)
     不解除——那说明服务端仍在发旧轮音频，解除会把旧回答播出来，违背打断意图。
     单位：秒，与 downlink 任务 1s 的队列等待 tick 一一对应。 */
#define CHATBOT_CANCEL_STUCK_RESOLVE_SEC     6

/**
 * @brief 初始化 ES8311 编解码器，并在失败时按配置重试。
 *
 * @return 0 表示编解码器已就绪；返回负值表示重试后仍初始化失败。
 */
static int coze_audio_prepare_codec(void)
{
    if (g_codec_ready == QOSA_TRUE)
    {
        return 0;
    }

    for (qosa_uint32_t attempt = 1; attempt <= CHATBOT_CODEC_INIT_RETRY_MAX; attempt++)
    {
        int ret = codec_es8311_init();
        if (ret == 0)
        {
            g_codec_ready = QOSA_TRUE;
            QLOGI("[chat_coze_audio_task] codec init success, attempt=%d", attempt);
            return 0;
        }

        QLOGW("[chat_coze_audio_task] codec init failed, attempt=%d/%d", attempt, CHATBOT_CODEC_INIT_RETRY_MAX);
        if (attempt < CHATBOT_CODEC_INIT_RETRY_MAX)
        {
            qosa_task_sleep_ms(CHATBOT_CODEC_INIT_RETRY_GAP_MS);
        }
    }

    return -1;
}

/*!< 起声回溯（预滚）缓存。
     VOICE 模式由本地 VAD 判定起声，而 vad_lite 需连续 VAD_LITE_START_FRAMES(3) 次
     判定为语音才返回 SPEECH_START；在此之前的那几帧本已含真实语音，却因上行门控
     （send_audio 仅认 START/CONTINUE）被整帧丢弃——丢掉的恰恰是这句话的开头。
     此处缓存最近几帧"已过 mic 增益"的原始 PCM，在 SPEECH_START 时按时间顺序补发；
     本帧仍由常规路径发送，不重复计入。
     深度取 3：确认窗口内最多丢 START_FRAMES-1 = 2 帧，留 1 帧余量。 */
#define CHATBOT_UPLINK_PREROLL_FRAMES   3

static qosa_uint8_t  s_preroll_pcm[CHATBOT_UPLINK_PREROLL_FRAMES][CHATBOT_AUDIO_FRAME_BYTES];
static qosa_uint32_t s_preroll_len[CHATBOT_UPLINK_PREROLL_FRAMES];
static qosa_uint32_t s_preroll_head  = 0; /*!< 最旧帧所在槽位 */
static qosa_uint32_t s_preroll_count = 0; /*!< 当前有效帧数 */

#if CHATBOT_AUDIO_CODEC_G711A
/*!< G711A 上行编码缓冲区：单帧样本数 = CHATBOT_AUDIO_FRAME_BYTES/2（PCM16升 A-law 1字节/采样）。
     上行任务单线程顺序调用，每次编码后立即组包并发送，下次调用前内容已被消费，共享缓冲区安全。 */
static qosa_uint8_t  s_g711_encode_buf[CHATBOT_AUDIO_FRAME_BYTES / 2];
#endif

/**
 * @brief 根据 CHATBOT_AUDIO_CODEC_G711A 宏开关将线性 PCM16 转换为上行线上数据。
 *
 * @param[in]  pcm      线性 PCM16 数据（已过 mic 增益）。
 * @param[in]  pcm_len  PCM 数据长度，单位字节。
 * @param[out] out_ptr  输出：实际要发送的数据指针（G711A 时指向内部编码缓冲区，否则与 pcm 相同）。
 * @return 实际要发送的字节长度。
 */
static int uplink_encode_for_wire(const qosa_uint8_t *pcm, int pcm_len, const qosa_uint8_t **out_ptr)
{
#if CHATBOT_AUDIO_CODEC_G711A
    int sample_cnt = pcm_len / (int)sizeof(qosa_int16_t);
    if ((pcm == QOSA_NULL) || (sample_cnt <= 0) || ((qosa_uint32_t)sample_cnt > sizeof(s_g711_encode_buf)))
    {
        *out_ptr = pcm;
        return pcm_len;
    }
    g711_alaw_encode_block((const qosa_int16_t *)pcm, s_g711_encode_buf, (qosa_uint32_t)sample_cnt);
    *out_ptr = s_g711_encode_buf;
    return sample_cnt;
#else
    *out_ptr = pcm;
    return pcm_len;
#endif
}

/**
 * @brief 清空 VAD 起声回溯缓存，不清除缓存存储区内容。
 */
static void uplink_preroll_clear(void)
{
    s_preroll_head  = 0;
    s_preroll_count = 0;
}

/**
 * @brief 将一帧 PCM 数据压入起声回溯环形缓存。
 *
 * @param[in] pcm 待缓存的 PCM 数据。
 * @param[in] len PCM 数据长度，单位为字节。
 */
/*!< 压入一帧（环形覆盖最旧帧）。仅"未起声"的帧需要缓存。 */
static void uplink_preroll_push(const qosa_uint8_t *pcm, qosa_uint32_t len)
{
    if ((pcm == QOSA_NULL) || (len == 0) || (len > CHATBOT_AUDIO_FRAME_BYTES))
    {
        return;
    }

    qosa_uint32_t slot;
    if (s_preroll_count == CHATBOT_UPLINK_PREROLL_FRAMES)
    {
        slot = s_preroll_head;
        s_preroll_head = (s_preroll_head + 1) % CHATBOT_UPLINK_PREROLL_FRAMES;
    }
    else
    {
        slot = (s_preroll_head + s_preroll_count) % CHATBOT_UPLINK_PREROLL_FRAMES;
        s_preroll_count++;
    }

    memcpy(s_preroll_pcm[slot], pcm, len);
    s_preroll_len[slot] = len;
}

/**
 * @brief 按时间顺序发送起声前缓存的 PCM 帧并清空缓存。
 *
 * 补发失败则丢弃剩余缓存（宁可少补，也不能让旧帧乱序落在这轮语音之后）。
 *
 * @param[out] json_buf     用于构造上行事件的 JSON 缓冲区。
 * @param[in]  json_buf_len JSON 缓冲区容量，单位为字节。
 */
static void uplink_preroll_flush(char *json_buf, qosa_uint32_t json_buf_len)
{
    if (s_preroll_count == 0)
    {
        return;
    }

    QLOGI("[chat_coze_audio_task] vad speech start, replay %u preroll frame(s)",
          (unsigned)s_preroll_count);

    for (qosa_uint32_t i = 0; i < s_preroll_count; i++)
    {
        qosa_uint32_t slot = (s_preroll_head + i) % CHATBOT_UPLINK_PREROLL_FRAMES;
        const qosa_uint8_t *wire_ptr = s_preroll_pcm[slot];
        int wire_len = uplink_encode_for_wire(s_preroll_pcm[slot], (int)s_preroll_len[slot], &wire_ptr);
        int json_len = coze_protocol_build_audio_append_event(json_buf, json_buf_len, wire_ptr, wire_len);
        if (json_len <= 0)
        {
            break;
        }

        int send_ret = coze_client_send_raw(json_buf, json_len, QCM_WEB_OPCODE_TEXT);
        g_uplink_frame_count++;
        if (send_ret != 0)
        {
            QLOGW("[chat_coze_audio_task] preroll frame send failed, drop tail=%u",
                  (unsigned)(s_preroll_count - i));
            break;
        }
        chat_core_ping_activity();
    }

    uplink_preroll_clear();
}

/**
 * @brief 设置当前会话的输入模式并复位音频任务状态。
 *
 * @param[in] mode 当前会话模式，语音模式或按键 PTT 模式。
 */
void coze_audio_set_session_mode(chat_session_mode_e mode)
{
    g_session_mode = mode;
    g_ptt_pressed = QOSA_FALSE;
    g_uplink_talking = QOSA_FALSE;
    /* 会话级重置：保证节流打印条件(==1/%50==0)在每次新会话里都能重新命中首帧 */
    g_uplink_frame_count = 0;
    g_downlink_frame_count = 0;
    vad_lite_reset();
    uplink_preroll_clear();
}

/**
 * @brief 更新 PTT 按键状态，并在必要时执行本地打断。
 *
 * @param[in] pressed QOSA_TRUE 表示按下；QOSA_FALSE 表示释放。
 */
void coze_audio_set_ptt_pressed(qosa_bool_t pressed)
{
    if ((pressed == QOSA_TRUE) && (g_ptt_pressed == QOSA_FALSE))
    {
        /* 打断(barge-in)条件 = 服务端有对话进行中 OR 本地仍在播放下行音频。
           AI 空闲(开场白播完且本地队列/播放流已停)时按下 PTT 只是开启新一轮语音输入,
           此时不 flush 也不发 cancel——空闲发 cancel 会令服务端终止会话模型,
           后续新 chat 报 "model has been terminated"。 */
        qosa_bool_t busy = coze_protocol_is_chat_busy();
        if ((busy == QOSA_TRUE) || (g_downlink_playing == QOSA_TRUE))
        {
            /* 本地立即静音：清空待播队列并关闭播放流，不等服务端确认。 */
            coze_audio_flush_downlink();
            if (busy == QOSA_TRUE)
            {
                /* 仅当服务端确实还有一轮对话在生成时才发 cancel 打断,
                   避免对已结束的轮次发 cancel 触发会话终止。 */
                (void)coze_session_send_cancel();
            }
        }
    }
    g_ptt_pressed = pressed;
}

/*!< 对 PCM16 采样原地放大（整数增益 + 饱和防削波）。仅 VOICE 模式上行使用：
     mic 原始能量低导致本地 VAD 与云端 ASR 都难触发/识别，放大后再做 VAD 与上传。 */
static void coze_voice_apply_mic_gain(qosa_uint8_t *pcm, int pcm_len)
{
    int16_t *samples = (int16_t *)pcm;
    int      count = pcm_len / (int)sizeof(int16_t);

    for (int i = 0; i < count; i++)
    {
        int32_t s = (int32_t)samples[i] * CHATBOT_VOICE_MIC_SOFT_GAIN;
        if (s > 32767)
        {
            s = 32767;
        }
        else if (s < -32768)
        {
            s = -32768;
        }
        samples[i] = (int16_t)s;
    }
}

/**
 * @brief 音频上行任务：持续对话态下常驻运行，循环"读采集帧 -> Base64 编码 -> 直接发送"，
 *        不经过事件总线。语音起止由本设备判定（不使用服务端 VAD）：
 *        BUTTON 模式 = 按键 PTT 按下/松开；VOICE 模式 = 本地 vad_lite 检测到起声/静音结束。
 *        打断(下行静音/发 cancel)由本地判定起声时触发，见各模式分支。
 */
static void uplink_task_process(void *ctx)
{
    (void)ctx;
    static qosa_uint8_t pcm_buf[CHATBOT_AUDIO_FRAME_BYTES];
    static char         json_buf[CHATBOT_AUDIO_UPLINK_JSON_MAX];
    qosa_uint32_t       no_data_cycle = 0;
    qosa_uint32_t       no_data_recover_cycle = 0;
    qosa_uint32_t       backpressure_log_cycle = 0;

    while (1)
    {
        if (g_audio_active == QOSA_FALSE)
        {
            qosa_task_sleep_ms(50);
            continue;
        }

        /* 兜底轮询：SDK 内部收包通知在消息队列拥塞时可能被静默丢弃（err msg_id=13），
           导致下行数据卡在水位线里永远收不到通知；借上行任务已有的高频循环顺带主动排空，
           无数据时 qcm_ws_read_proc 立即返回，开销可忽略。 */
        coze_client_poll_recv();

        {
            int tx_total_size = 0;
            int send_size = 0;
            if (qcm_ws_client_get_send_size(CHATBOT_COZE_CLIENT_ID, &tx_total_size, &send_size) == QCM_WEB_ERR_OK)
            {
                int pending = tx_total_size - send_size;
                if (pending < 0)
                {
                    pending = 0;
                }

                if (pending >= CHATBOT_WS_PENDING_HIGH_WATER)
                {
                    backpressure_log_cycle++;
                    if ((backpressure_log_cycle % 50) == 1)
                    {
                        QLOGD("[chat_coze_audio_task] ws pending high, pause uplink pending=%d", pending);
                    }
                    /* 网络拥塞暂停上行，但先排空采集 ring 一帧：发送等待期间若长时间
                       不 read，驱动 48KB record ring 会被 DMA 写满(≈1.5s)触发 overflow 停 DMA。
                       这里读掉一帧丢弃，让 ring 始终保持低位。 */
                    (void)audio_capture_read_frame(pcm_buf, sizeof(pcm_buf));
                    qosa_task_sleep_ms(CHATBOT_WS_BACKPRESSURE_SLEEP_MS);
                    continue;
                }
            }
        }

        int read_len = audio_capture_read_frame(pcm_buf, sizeof(pcm_buf));
        if (read_len <= 0)
        {
            no_data_cycle++;
            no_data_recover_cycle++;
            if ((read_len < 0) || (no_data_cycle >= 500))
            {
                QLOGW("[chat_coze_audio_task] capture no data, ret=%d active=%d", read_len, g_audio_active);
                no_data_cycle = 0;
            }
            /* 说话期间卡死会直接吞掉整句语音，必须比空闲更快自愈重开。
               统一判定"正在上行说话"：BUTTON=按住 PTT，VOICE=本地 VAD 语音活跃,
               两种模式说话中都用短周期(500ms)重开，避免吞话。 */
            qosa_uint32_t recover_cycles = CHATBOT_CAPTURE_STALL_RECOVER_CYCLES;
            if (g_uplink_talking == QOSA_TRUE)
            {
                recover_cycles = CHATBOT_CAPTURE_STALL_RECOVER_SPEECH_CYCLES;
            }
            if (no_data_recover_cycle >= recover_cycles)
            {
                QLOGW("[chat_coze_audio_task] capture stalled, reopening stream");
                (void)audio_capture_stop();
                qosa_task_sleep_ms(20);
                (void)audio_capture_start();
                no_data_recover_cycle = 0;
            }
            qosa_task_sleep_ms(10);
            continue;
        }
        no_data_cycle = 0;
        no_data_recover_cycle = 0;

        qosa_bool_t send_audio = QOSA_FALSE;
        qosa_bool_t complete_audio = QOSA_FALSE;
        qosa_bool_t speech_start = QOSA_FALSE;
        if (g_session_mode == CHAT_SESSION_MODE_BUTTON)
        {
            send_audio = g_ptt_pressed;
            g_uplink_talking = g_ptt_pressed;
        }
        else
        {
            /* VOICE 模式：mic 原始能量偏低，先软件增益放大，
               VAD 与上传都用放大后数据（放大前后顺序：读帧→增益→VAD→发送）。 */
            coze_voice_apply_mic_gain(pcm_buf, read_len);
            vad_lite_state_e vad_state = vad_lite_process(pcm_buf, (qosa_uint32_t)read_len);
            /* VAD 仅保留起声(start)与结束(end)两个事件级打印，周期性能量打印已省略。 */
            if (vad_state == VAD_LITE_SPEECH_START)
            {
                QLOGI("[chat_coze_audio_task] vad speech start, rms=%u busy=%d playing=%d read_len=%d",
                      (unsigned)vad_lite_get_last_max_rms(), (int)coze_protocol_is_chat_busy(),
                      (int)g_downlink_playing, read_len);
                /* 同 PTT：服务端有对话进行中 OR 本地仍在播放下行时立即静音打断；
                   空闲时用户起声是新一轮输入，无需 cancel，否则会终止会话模型。 */
                qosa_bool_t busy = coze_protocol_is_chat_busy();
                if ((busy == QOSA_TRUE) || (g_downlink_playing == QOSA_TRUE))
                {
                    coze_audio_flush_downlink();
                    if (busy == QOSA_TRUE)
                    {
                        (void)coze_session_send_cancel();
                    }
                }
            }
            send_audio = (vad_state == VAD_LITE_SPEECH_START) || (vad_state == VAD_LITE_SPEECH_CONTINUE);
            complete_audio = (vad_state == VAD_LITE_SPEECH_END);
            speech_start = (vad_state == VAD_LITE_SPEECH_START);
            g_uplink_talking = send_audio;
        }

        if (complete_audio == QOSA_TRUE)
        {
            (void)coze_session_send_audio_complete();
            QLOGI("[chat_coze_audio_task] vad speech end, submit audio complete");
            /* 一句结束，清空回溯缓存，避免上一句的缓存帧被补发到下一句开头 */
            uplink_preroll_clear();
        }
        if (send_audio == QOSA_FALSE)
        {
            /* VOICE 模式：未起声的帧先压入回溯缓存，供起声时补发（否则句子开头被削）；
               BUTTON 模式不看 VAD，按键前本就不该上送，不缓存。 */
            if (g_session_mode == CHAT_SESSION_MODE_VOICE)
            {
                uplink_preroll_push(pcm_buf, (qosa_uint32_t)read_len);
            }
            /* 非说话帧不发送，但要保持采集 ring 的高频消费：
               若间隔 100ms 才读一次，驱动 48KB ring 在 DMA 持续产数时长期维持低位，
               一旦上行发送因蜂窝调度劣化被拖住，读数间隔被拉长到 ring 写满(≈1.5s)
               就会触发 record overflow 停 DMA。因此空闲也按 10ms 高频轮询读，只丢弃帧不发送。 */
            qosa_task_sleep_ms(10);
            continue;
        }

        /* 起声回溯：先补发确认窗口内被丢弃的开头帧（时间上早于本帧）,
           本帧随后由下方常规路径发送，二者不重复。 */
        if (speech_start == QOSA_TRUE)
        {
            uplink_preroll_flush(json_buf, sizeof(json_buf));
        }

        const qosa_uint8_t *wire_ptr = pcm_buf;
        int wire_len = uplink_encode_for_wire(pcm_buf, read_len, &wire_ptr);
        int json_len = coze_protocol_build_audio_append_event(json_buf, sizeof(json_buf), wire_ptr, wire_len);
        if (json_len > 0)
        {
            /* PTT 释放与上行发送分属两个任务：release 事件由 chat_core 处理，
               置 g_ptt_pressed=FALSE 后立即发 input_audio_buffer.complete；而 uplink
               任务在读帧时 PTT 可能仍为按下状态，随后才把该帧发出——会出现
               "complete 之后再发一帧 append"，服务端会将其视为新一轮语音并等待
               后续音频包直至超时报错。发送前二次确认：BUTTON 模式若 PTT 已释放
               则丢弃本帧，保证 complete 之后绝无 append。 */
            if ((g_session_mode == CHAT_SESSION_MODE_BUTTON) && (g_ptt_pressed == QOSA_FALSE))
            {
                QLOGI("[chat_coze_audio_task] drop trailing uplink frame after PTT release, len=%d", read_len);
                continue;
            }

            int send_ret = coze_client_send_raw(json_buf, json_len, QCM_WEB_OPCODE_TEXT);
            qosa_uint32_t bytes_per_second = CHATBOT_AUDIO_SAMPLE_RATE * CHATBOT_AUDIO_CHANNELS * (CHATBOT_AUDIO_BIT_DEPTH / 8);
            qosa_uint32_t frame_duration_ms = ((qosa_uint32_t)read_len * 1000U) / bytes_per_second;
            g_uplink_frame_count++;
            if (send_ret == 0)
            {
                /* 上行发送成功 → 数据面活跃，刷新会话空闲计时（持续说话/长句上传不被误断） */
                chat_core_ping_activity();
            }
            if (send_ret != 0)
            {
                qosa_task_sleep_ms(100);
                continue;
            }
            if (frame_duration_ms > 0)
            {
                qosa_task_sleep_ms(frame_duration_ms);
            }
        }
    }
}

/**
 * @brief 播放单帧并记录/释放，供下行任务在正常路径和预缓冲路径共用。
 *
 * @note 播放节奏与 uniclaw 参考实现保持一致：不按帧长做固定 sleep pacing,
 *       而是"有多少数据就尽量灌入驱动"——audio_player_write_frame() 内部在底层
 *       FIFO 满(返回 0)时只 sleep 5ms 后继续补写，由驱动可用空间自然节流。
 *       EC718 播放驱动是 48KB ring + DMA 中断续传，启动/续传要求 ring 内数据
 *       >= fram_size*2；若应用写一帧就固定歇一帧时长(100ms)，ring 易在 DMA 抽空后
 *       跌破重启阈值，表现为"写入成功但只出声开头一小段"。连续灌入可让 ring 保持
 *       充足余量，DMA 连续播放。
 */
static void downlink_play_one_frame(audio_frame_t *frame, qosa_uint32_t *played_frame_count)
{
    if (g_audio_active == QOSA_TRUE)
    {
        int start_ret = audio_player_start();
        int write_ret = -1;
        if (start_ret == 0)
        {
            write_ret = audio_player_write_frame(frame->data, (qosa_uint32_t)frame->len);
        }

        (*played_frame_count)++;
        if ((start_ret != 0) || (write_ret < 0))
        {
            QLOGW("[chat_coze_audio_task] downlink play failed, start_ret=%d write_ret=%d len=%d frame=%u",
                  start_ret, write_ret, frame->len, *played_frame_count);
        }
        else
        {
            /* 播放帧写入成功 → 数据面活跃，刷新会话空闲计时：
               长回答播放期间不产生 chat_core 协议事件，若不心跳会被 idle 超时误断。 */
            chat_core_ping_activity();
            if ((*played_frame_count <= 5) || ((*played_frame_count % 50) == 0))
            {
                /* 前 5 帧强制不节流打印，避免日志量过大时首包被日志过滤/截断而无法确认播放链路存活 */
                QLOGI("[chat_coze_audio_task] downlink played frame=%u len=%d write_ret=%d",
                      *played_frame_count, frame->len, write_ret);
            }
        }
    }
    else
    {
        QLOGW("[chat_coze_audio_task] drop downlink frame while inactive, len=%d", frame->len);
    }

    qosa_free(frame->data);
    qosa_free(frame);
}

/**
 * @brief 音频下行任务：从仅承载音频帧的轻量队列取帧，立即解码写入播放，
 *        不经过事件总线；收到打断通知时由 coze_protocol 直接调用 audio_player_stop_and_flush()。
 */
static void downlink_task_process(void *ctx)
{
    (void)ctx;
    qosa_bool_t   idle_before = QOSA_TRUE;

    while (1)
    {
        audio_frame_t *frame = QOSA_NULL;
        int            ret = qosa_msgq_wait(g_downlink_queue, (qosa_uint8_t *)&frame, sizeof(audio_frame_t *), 1000);
        if ((ret != 0) || (frame == QOSA_NULL))
        {
            /* cancel 卡死兜底（以本 1s 超时为心跳）：
               muted 期间残余帧到达会把 g_muted_quiet_1s_cnt 清零（push/downlink 丢弃点）,
               故本计数自增到 CHATBOT_CANCEL_STUCK_RESOLVE_SEC(6) 意味着连续约 6s 没有
               任何残余帧到达 —— 服务端已实际停发旧轮音频，只是 conversation.chat.canceled
               回执丢失/迟到。此时强制解除静音并复位 cancel/busy，避免本会话永久静音。 */
            if (g_downlink_muted == QOSA_TRUE)
            {
                g_muted_quiet_1s_cnt++;
                if (g_muted_quiet_1s_cnt >= CHATBOT_CANCEL_STUCK_RESOLVE_SEC)
                {
                    QLOGW("[chat_coze_audio_task] cancel stuck: no residual for %us, force resolve mute",
                          (unsigned)g_muted_quiet_1s_cnt);
                    coze_protocol_cancel_stuck_resolved();
                    coze_audio_downlink_resume();
                    g_muted_quiet_1s_cnt = 0;
                }
            }

            /* 参考 uniclaw 已验证用法：每轮回复播完即关闭播放流，释放音频通道，
               避免播放 DMA 长期空转占用底层 I2S 通路干扰录音（mic）路径，
               表现为录音流周期性无数据、需要频繁 reopen。下一轮下行音频到达时
               downlink_play_one_frame() 会重新 audio_player_start()。 */
            if (idle_before == QOSA_FALSE)
            {
                QLOGI("[chat_coze_audio_task] downlink idle 1s, close player to release mic path");
                g_downlink_playing = QOSA_FALSE;
                (void)audio_player_stop_and_flush();
                idle_before = QOSA_TRUE;
            }
            continue;
        }

        if (g_audio_active == QOSA_FALSE)
        {
            qosa_uint32_t inactive_wait_ms = 0;
            while ((g_audio_active == QOSA_FALSE) && (inactive_wait_ms < 5000))
            {
                qosa_task_sleep_ms(10);
                inactive_wait_ms += 10;
            }
        }

        /* 打断静音门控消费：flush(打断) 后置 muted，cancel 生效前服务端已下发/在途的
           残余旧回答帧会陆续到达；若不在此丢弃，downlink 会重开播放流把残余播完。
           直到 protocol 收到新一轮 sentence_start / chat.updated 调
           coze_audio_downlink_resume() 解除后才恢复播放。此处置于 prebuffer 之前，
           确保打断后无论是走预缓冲还是正常路径的残余帧都被丢弃。 */
        if (g_downlink_muted == QOSA_TRUE)
        {
            QLOGW("[chat_coze_audio_task] drop downlink frame while muted, len=%d", frame->len);
            /* 残余帧到达即清零兜底心跳：服务端仍在发旧轮音频，不应解除静音 */
            g_muted_quiet_1s_cnt = 0;
            /* 日志节流：muted 丢弃累计计数，首帧与每 100 帧打印一次 */
            g_muted_drop_cnt++;
            if ((g_muted_drop_cnt == 1) || ((g_muted_drop_cnt % 100) == 0))
            {
                QLOGW("[chat_coze_audio_task] drop downlink frame while muted, total_dropped=%u", (unsigned)g_muted_drop_cnt);
            }
            qosa_free(frame->data);
            qosa_free(frame);
            continue;
        }

        if (idle_before == QOSA_TRUE)
        {
            /* 每次从空闲恢复播放前做一次短暂预缓冲，缓解服务端下发抖动引起的播放卡顿 */
            audio_frame_t *held[CHATBOT_DOWNLINK_PREBUFFER_FRAMES] = {frame};
            int            held_count = 1;
            qosa_uint32_t  waited_ms = 0;

            while ((held_count < CHATBOT_DOWNLINK_PREBUFFER_FRAMES) && (waited_ms < CHATBOT_DOWNLINK_PREBUFFER_WAIT_MS))
            {
                audio_frame_t *extra = QOSA_NULL;
                if (qosa_msgq_wait(g_downlink_queue, (qosa_uint8_t *)&extra, sizeof(audio_frame_t *), 10) == 0 && extra != QOSA_NULL)
                {
                    held[held_count++] = extra;
                }
                else
                {
                    waited_ms += 10;
                }
            }

            g_downlink_playing = QOSA_TRUE;
            idle_before = QOSA_FALSE;
            for (int i = 0; i < held_count; i++)
            {
                downlink_play_one_frame(held[i], &g_downlink_frame_count);
            }
            continue;
        }

        g_downlink_playing = QOSA_TRUE;
        downlink_play_one_frame(frame, &g_downlink_frame_count);
    }
}

/**
 * @brief 启动音频上/下行任务：创建下行队列与两个音频任务，初始化编解码器并打开采集/播放流。
 *
 * @return 0 表示成功；返回负值表示资源创建或音频流打开失败。
 */
int coze_audio_tasks_start(void)
{
    if (g_downlink_queue == QOSA_NULL)
    {
        if (qosa_msgq_create(&g_downlink_queue, sizeof(audio_frame_t *), CHATBOT_DOWNLINK_QUEUE_DEPTH) != QOSA_ERROR_OK)
        {
            QLOGE("[chat_coze_audio_task] downlink queue create failed");
            return -1;
        }
    }

    if (g_uplink_task == QOSA_NULL)
    {
        if (qosa_task_create(&g_uplink_task, CHATBOT_UPLINK_TASK_STACK_SIZE, CHATBOT_UPLINK_TASK_PRIORITY, "coze_audio_up", uplink_task_process, QOSA_NULL) != 0)
        {
            QLOGE("[chat_coze_audio_task] uplink task create failed");
            return -1;
        }
    }

    if (g_downlink_task == QOSA_NULL)
    {
        if (qosa_task_create(&g_downlink_task, CHATBOT_DOWNLINK_TASK_STACK_SIZE, CHATBOT_DOWNLINK_TASK_PRIORITY, "coze_audio_down", downlink_task_process, QOSA_NULL) != 0)
        {
            QLOGE("[chat_coze_audio_task] downlink task create failed");
            return -1;
        }
    }

    if (coze_audio_prepare_codec() != 0)
    {
        QLOGE("[chat_coze_audio_task] codec prepare failed");
        return -1;
    }

    if (audio_capture_start() != 0)
    {
        QLOGE("[chat_coze_audio_task] capture start failed");
        return -1;
    }

    if (audio_player_start() != 0)
    {
        QLOGE("[chat_coze_audio_task] player start failed");
        (void)audio_capture_stop();
        return -1;
    }
    g_audio_active = QOSA_TRUE;
    QLOGI("[chat_coze_audio_task] audio uplink/downlink active");
    return 0;
}

/**
 * @brief 停止音频上/下行：复位音频状态与 VAD、清空回溯缓存并关闭采集/播放流。
 */
void coze_audio_tasks_stop(void)
{
    g_audio_active = QOSA_FALSE;
    g_ptt_pressed = QOSA_FALSE;
    g_downlink_playing = QOSA_FALSE;
    g_downlink_muted = QOSA_FALSE;
    g_muted_quiet_1s_cnt = 0;
    vad_lite_reset();
    uplink_preroll_clear();
    (void)audio_capture_stop();
    (void)audio_player_stop_and_flush();
}

/**
 * @brief 将一帧下行音频数据入队待播（入队后缓冲区所有权转移给下行任务）。
 *
 * 打断静音门控期间到达的帧一律视为旧轮残余，直接丢弃并计入丢帧统计。
 *
 * @param[in] pcm 下行音频数据（调用方分配的堆缓冲区）。
 * @param[in] len 数据长度，单位为字节。
 */
void coze_audio_downlink_push(qosa_uint8_t *pcm, int len)
{
    if ((pcm == QOSA_NULL) || (len <= 0))
    {
        if (pcm != QOSA_NULL)
        {
            qosa_free(pcm);
        }
        return;
    }

    /* 打断静音门控期间到达的帧一律视为旧回答残余，源头丢弃、不入队：
       否则残余帧会在队列里积压，等新轮 resume 解除 muted 后被 downlink 当作新音频播完,
       表现为"VAD 说完话后先把上一轮回答播完，才播本次回答"。新轮音频的 push
       只会发生在 sentence_start(resume) 之后，此时 muted 已解除，不会误丢新帧。 */
    if (g_downlink_muted == QOSA_TRUE)
    {
        QLOGW("[chat_coze_audio_task] drop downlink push while muted, len=%d", len);
        /* 残余帧到达即清零兜底心跳：服务端仍在发旧轮音频，不应解除静音 */
        g_muted_quiet_1s_cnt = 0;
        /* 日志节流：muted 丢弃累计计数，首帧与每 100 帧打印一次。 */
        g_muted_drop_cnt++;
        if ((g_muted_drop_cnt == 1) || ((g_muted_drop_cnt % 100) == 0))
        {
            QLOGW("[chat_coze_audio_task] drop downlink push while muted, total_dropped=%u", (unsigned)g_muted_drop_cnt);
        }
        qosa_free(pcm);
        return;
    }

    audio_frame_t *frame = qosa_malloc(sizeof(audio_frame_t));
    if (frame == QOSA_NULL)
    {
        /* 分配失败即丢弃本帧，与队列满同样计入丢帧统计。 */
        g_downlink_drop_cnt++;
        QLOGW("[chat_coze_audio_task] downlink push alloc failed (frame), len=%d, total_dropped=%u",
              len, (unsigned)g_downlink_drop_cnt);
        qosa_free(pcm);
        return;
    }
    frame->data = pcm;
    frame->len = len;

    if ((g_downlink_queue == QOSA_NULL) ||
        (qosa_msgq_release(g_downlink_queue, sizeof(audio_frame_t *), (qosa_uint8_t *)&frame, QOSA_NO_WAIT) != QOSA_ERROR_OK))
    {
        /* 队列满 = 服务端下发速率超过本地播放速率（突发灌包）。累计计数便于量化"限流是否生效"：
           若已下发 limit_config（chatbot_config.h 的 CHATBOT_COZE_DOWNLINK_LIMIT_*）后此计数
           仍持续增长，说明服务端未按限流下发或队列深度（CHATBOT_DOWNLINK_QUEUE_DEPTH）仍偏小。 */
        g_downlink_drop_cnt++;
        QLOGW("[chat_coze_audio_task] downlink push failed, queue unavailable/full, len=%d, total_dropped=%u",
              len, (unsigned)g_downlink_drop_cnt);
        qosa_free(pcm);
        qosa_free(frame);
    }
}

/*!< 排空下行队列中的待播帧（不改变 muted 状态）。供打断 flush 与新轮 resume 共用。 */
static qosa_uint32_t coze_audio_drain_downlink_queue(void)
{
    qosa_uint32_t drained = 0;
    if (g_downlink_queue == QOSA_NULL)
    {
        return 0;
    }

    audio_frame_t *frame = QOSA_NULL;
    while (qosa_msgq_wait(g_downlink_queue, (qosa_uint8_t *)&frame, sizeof(audio_frame_t *), QOSA_NO_WAIT) == 0)
    {
        if (frame != QOSA_NULL)
        {
            qosa_free(frame->data);
            qosa_free(frame);
            frame = QOSA_NULL;
        }
        drained++;
    }
    return drained;
}

/**
 * @brief 打断时调用（本地 VAD SPEECH_START / 按键 PTT 按下 / 服务端 speech_started 兜底）：
 *        清空本地待播队列并停止播放流，同时置下行静音门控，防止打断前已下发的
 *        旧回答残余继续被播完。
 */
void coze_audio_flush_downlink(void)
{
    qosa_uint32_t drained = coze_audio_drain_downlink_queue();

    if (drained > 0)
    {
        QLOGI("[chat_coze_audio_task] flush downlink, drained=%u", drained);
    }

    /* 打断静音门控置位：取消下行播放许可，残余帧到达即被 downlink/push 丢弃、不重开播放流。
       直到服务端开始新一轮回答（protocol 调 coze_audio_downlink_resume）才恢复。 */
    g_downlink_muted = QOSA_TRUE;
    g_downlink_playing = QOSA_FALSE;
    g_muted_quiet_1s_cnt = 0;
    (void)audio_player_stop_and_flush();
}

/*!< 服务端开始新一轮回答（sentence_start / 新一轮 chat.created/in_progress）时调用：
     解除打断后的下行静音门控，允许新一轮下行音频播放。 */
void coze_audio_downlink_resume(void)
{
    if (g_downlink_muted == QOSA_TRUE)
    {
        /* 先排空队列再解除 muted：push 的 muted 检查与 resume 存在跨线程竞态窗口,
           个别旧残余帧可能在 resume 前置入队列；不排空的话它们会被当成新一轮音频播放,
           表现为"说完话后先播上一轮回答再播本次"。sentence_start 与后续 delta 在
           protocol worker 内串行处理，此处排空发生在该轮 delta push 之前，不会误清新帧。 */
        qosa_uint32_t drained = coze_audio_drain_downlink_queue();
        QLOGI("[chat_coze_audio_task] downlink resume (new reply turn started), drained=%u", drained);
        g_downlink_muted = QOSA_FALSE;
        g_downlink_playing = QOSA_FALSE;
        g_muted_quiet_1s_cnt = 0;
        (void)audio_player_stop_and_flush();
    }
}
