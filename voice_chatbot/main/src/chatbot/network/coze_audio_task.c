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
/*!< 播放流当前是否已打开（应用内只有本文件开关播放流），
     用于"无可清内容时打断为 no-op"与"空闲自动关流"两处判断。 */
static volatile qosa_bool_t g_player_open = QOSA_FALSE;
/*!< 上/下行帧计数器：按会话重置（在 coze_audio_set_session_mode() 里清零），
     供节流打印条件 (==1 或 %50==0) 在每次新会话里都能重新命中首帧。 */
static qosa_uint32_t        g_uplink_frame_count = 0;
static qosa_uint32_t        g_downlink_frame_count = 0;

/*!< 进入空闲态的时刻（系统 tick），用于"空闲足够久即关流释放音频通路"。 */
static volatile qosa_uint32_t g_downlink_idle_tick = 0;

/*!< 打断后"丢弃在途旧回复残余帧"的截止时刻（系统 tick，1 tick = 1ms）。
     0 表示无窗口。窗口到期即自然失效——这是防止永久静音死锁的关键：
     若改为只能由服务端"新一轮事件"解除的闩锁，一旦服务端停在"用户正在说话"态
     不再下发新事件，本地就会永久静音且丢弃全部下行音频。 */
static volatile qosa_uint32_t g_drop_window_until_tick = 0;

/*!< 上次清尾时刻，用于打断冷却（抑制回声反复触发造成的回复断续）。 */
static volatile qosa_uint32_t g_last_flush_tick = 0;

/*!< 打断窗口内累计丢弃的残余帧计数（跨 push/downlink 两丢弃点累加），日志节流用。
     打断残余期服务端持续下发旧帧，每帧一条 WARN 会刷屏，改为累计计数 +
     首帧/每 20 帧打印一次。 */
static volatile qosa_uint32_t g_drop_window_frame_count = 0;

/*!< 因"下行队列满 / 帧结构体分配失败"而丢弃的帧累计计数（与 muted 丢弃相互独立）。
     非 0 即说明服务端下发速率曾长期超过本地播放速率（突发灌包）——
     参见 chatbot_config.h 的 CHATBOT_COZE_DOWNLINK_LIMIT_*（限流）与
     CHATBOT_DOWNLINK_QUEUE_DEPTH（队列余量）。会话中途不清零，便于跨轮观察。 */
static volatile qosa_uint32_t g_downlink_drop_cnt = 0;

/*!< 上行是否正在活跃上传（统一判定）：用于采集流 stall 时选择加速自愈周期——
     BUTTON 模式看 g_ptt_pressed；VOICE（server_vad）模式持续上传音频，恒为 TRUE。 */
static volatile qosa_bool_t g_uplink_talking = QOSA_FALSE;

#define CHATBOT_CODEC_INIT_RETRY_MAX      9
#define CHATBOT_CODEC_INIT_RETRY_GAP_MS   500
#define CHATBOT_WS_PENDING_HIGH_WATER      8192
#define CHATBOT_WS_BACKPRESSURE_SLEEP_MS   20
/*!< 某些状态下采集流会卡在持续无数据，重开流可恢复。
     因此：非说话态 1s(100) 重开；PTT 按住 / server_vad 持续上传期间 500ms(50) 即重开。 */
#define CHATBOT_CAPTURE_STALL_RECOVER_CYCLES     100
#define CHATBOT_CAPTURE_STALL_RECOVER_SPEECH_CYCLES 50

/*!< 下行预缓冲：蜂窝网络抖动可达数百毫秒，用 4 帧 / 600ms 窗口平滑抖动，
     队列深度（chatbot_config.h 的 CHATBOT_DOWNLINK_QUEUE_DEPTH）需同步容纳
     预缓冲期间堆积的帧。 */
#define CHATBOT_DOWNLINK_PREBUFFER_FRAMES    4
#define CHATBOT_DOWNLINK_PREBUFFER_WAIT_MS   600
/*!< 队列深度配置见 chatbot_config.h 的 CHATBOT_DOWNLINK_QUEUE_DEPTH（与下行限流
     参数同处，便于按内存余量统一调优）。 */

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

/**
 * @brief 判断打断丢弃窗口当前是否生效（到点自动失效，无需任何对端事件解除）。
 */
static qosa_bool_t coze_audio_drop_window_active(void)
{
    qosa_uint32_t until = g_drop_window_until_tick;
    if (until == 0)
    {
        return QOSA_FALSE;
    }
    /* 有符号差值比较，天然处理 tick 回绕 */
    return ((qosa_int32_t)(qosa_get_system_tick_cnt() - until) < 0) ? QOSA_TRUE : QOSA_FALSE;
}

/*!< 对 PCM16 采样原地放大（整数增益 + 饱和防削波）。仅 VOICE 模式上行使用：
     mic 原始能量低，不加增益时服务端 server_vad 可能达不到起声门限，
     导致完全收不到 speech_started；放大后再上传。 */
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
 *        不经过事件总线。语音起止判定按会话模式：
 *        - VOICE（server_vad）模式：零门控、零本地检测，持续恒定帧率上传音频，
 *          语音起止完全由服务端判定（不做任何本地 VAD / 静音替换 / 上行速率门控）；
 *        - BUTTON（client_interrupt）模式：按下上传，松开由 chat_core 发 complete。
 */
static void uplink_task_process(void *ctx)
{
    (void)ctx;
    static qosa_uint8_t pcm_buf[CHATBOT_AUDIO_FRAME_BYTES];
    static char         json_buf[CHATBOT_AUDIO_UPLINK_JSON_MAX];
    qosa_uint32_t       no_data_cycle = 0;
    qosa_uint32_t       no_data_recover_cycle = 0;
    qosa_uint32_t       backpressure_log_cycle = 0;
    /*!< 当前已累积的 PCM 字节数。采集流是**异步模式**，单次 read 只返回"此刻可用"
         的 20~40ms 数据；若"读多少发多少"，消息数会被放大到 25~50 msg/s（预期 10 msg/s），
         把 SDK 内部消息队列（容量仅 10）与发送水位推爆，表现为下行通知丢失/上行积压。
         故必须先凑满一整帧再发送（与参考实现一致）。 */
    qosa_uint32_t       pcm_acc = 0;
    /*!< 下一帧的发送**截止时刻**（系统 tick）。每帧只做"截止时刻 += 固定帧长"，
         而不是"本帧起点 + 帧长"——凑满一帧的过程本身已耗尽约一帧时长，
         此处只补剩余等待即可保持真实速率；反之若固定睡满一帧，速率会掉到半速。 */
    qosa_uint32_t       next_deadline_tick = 0;

    while (1)
    {
        if (g_audio_active == QOSA_FALSE)
        {
            pcm_acc = 0;
            next_deadline_tick = 0;
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
                    pcm_acc = 0;
                    next_deadline_tick = 0;
                    qosa_task_sleep_ms(CHATBOT_WS_BACKPRESSURE_SLEEP_MS);
                    continue;
                }
            }
        }

        int read_len = audio_capture_read_frame(pcm_buf + pcm_acc,
                                               (qosa_uint32_t)(CHATBOT_AUDIO_FRAME_BYTES - pcm_acc));
        if (read_len <= 0)
        {
            no_data_cycle++;
            no_data_recover_cycle++;
            if ((read_len < 0) || (no_data_cycle >= 500))
            {
                QLOGW("[chat_coze_audio_task] capture no data, ret=%d active=%d", read_len, g_audio_active);
                no_data_cycle = 0;
            }
            /* 采集断流会直接吞掉整段语音，必须比空闲更快自愈重开。
               统一判定"上行处于活跃上传态"：BUTTON=按住 PTT，
               VOICE（server_vad）=持续上传态；两种情形都用短周期(500ms)重开。 */
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
                /* 重开采集流后半帧已残缺，丢弃避免与后续音频拼接 */
                pcm_acc = 0;
            }
            qosa_task_sleep_ms(10);
            continue;
        }
        no_data_cycle = 0;
        no_data_recover_cycle = 0;

        pcm_acc += (qosa_uint32_t)read_len;
        if (pcm_acc < CHATBOT_AUDIO_FRAME_BYTES)
        {
            /* 未满一整帧：立刻继续读，不 sleep、不发送，保证每次上行都是恒定
               CHATBOT_AUDIO_FRAME_MS 的整帧（服务端按 frame_size_ms 预期音频速率）。 */
            continue;
        }
        pcm_acc = 0;

        qosa_bool_t send_audio = QOSA_FALSE;
        if (g_session_mode == CHAT_SESSION_MODE_BUTTON)
        {
            /* 按键 PTT 模式（client_interrupt）：按下才上传，松开由 chat_core 发
               input_audio_buffer.complete 提交本段语音。 */
            send_audio = g_ptt_pressed;
            g_uplink_talking = g_ptt_pressed;
        }
        else
        {
            /* VOICE 模式（server_vad）：设备侧不做任何本地语音活动检测，也不做静音门控，
               先把 mic 增益后的整帧音频持续上传，语音起止完全交给服务端判定
               （服务端下发 speech_started / speech_stopped）。打断同样由服务端在检测到
               用户起声时自动完成，设备侧只在收到 speech_started 时清尾，此处不发
               conversation.chat.cancel、也不发 input_audio_buffer.complete。 */
            coze_voice_apply_mic_gain(pcm_buf, (int)CHATBOT_AUDIO_FRAME_BYTES);
            send_audio = QOSA_TRUE;
            g_uplink_talking = QOSA_TRUE;
        }

        if (send_audio == QOSA_FALSE)
        {
            /* 非上传帧（按键模式下未按 PTT）不发送，但仍必须持续消费采集 ring：
               驱动 48KB record ring 若不被消费，会被 DMA 写满(≈1.5s)并触发 record
               overflow 停 DMA，导致后续句首丢失。凑满一帧的过程本身已耗时约一帧
               时长，故此处不额外 sleep，保持与生产同速消费。 */
            continue;
        }

        const qosa_uint8_t *wire_ptr = pcm_buf;
        int wire_len = uplink_encode_for_wire(pcm_buf, (int)CHATBOT_AUDIO_FRAME_BYTES, &wire_ptr);
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
                QLOGI("[chat_coze_audio_task] drop trailing uplink frame after PTT release, len=%d",
                      (int)CHATBOT_AUDIO_FRAME_BYTES);
                continue;
            }

            int send_ret = coze_client_send_raw(json_buf, json_len, QCM_WEB_OPCODE_TEXT);
            g_uplink_frame_count++;
            if (send_ret == 0)
            {
                /* 上行发送成功 → 数据面活跃，刷新会话空闲计时（持续说话/长句上传不被误断） */
                chat_core_ping_activity();
            }
            if (send_ret != 0)
            {
                /* 发送失败（如 SDK 发送水位闩锁返回 NOMEM / 链路瞬断）：
                   停止写入本身就是释放机制，退避后重试即可，绝不因此重连。
                   同时重同步截止时刻，避免退避结束后突发补发把发送水位再次推爆。 */
                next_deadline_tick = 0;
                qosa_task_sleep_ms(100);
                continue;
            }

            /* 按真实时间节流（截止时刻制）：只把截止时刻推进固定帧长，
               本帧处理慢了后面的帧自动少睡补回来，不产生长期速率亏损。 */
            if (next_deadline_tick == 0)
            {
                next_deadline_tick = qosa_get_system_tick_cnt();
            }
            next_deadline_tick += CHATBOT_AUDIO_FRAME_MS;
            {
                qosa_int32_t wait_ms = (qosa_int32_t)(next_deadline_tick - qosa_get_system_tick_cnt());
                if (wait_ms > 0)
                {
                    qosa_task_sleep_ms((qosa_uint32_t)wait_ms);
                }
                else if (wait_ms < -1000)
                {
                    /* 落后过多（链路长时间退避）：重同步，避免突发补发 */
                    QLOGW("[chat_coze_audio_task] uplink lag %dms, resync pacing deadline", -wait_ms);
                    next_deadline_tick = qosa_get_system_tick_cnt();
                }
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
            g_player_open = QOSA_TRUE;
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
            /* 参考 uniclaw 已验证用法：每轮回复播完即关闭播放流，释放音频通道，
               避免播放 DMA 长期空转占用底层 I2S 通路干扰录音（mic）路径，
               表现为录音流周期性无数据、需要频繁 reopen。下一轮下行音频到达时
               downlink_play_one_frame() 会重新 audio_player_start()。 */
            if (idle_before == QOSA_FALSE)
            {
                QLOGI("[chat_coze_audio_task] downlink idle 1s, close player to release mic path");
                g_downlink_playing = QOSA_FALSE;
                g_downlink_idle_tick = qosa_get_system_tick_cnt();
                (void)audio_player_stop_and_flush();
                g_player_open = QOSA_FALSE;
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

        /* 打断丢弃窗口消费：flush(打断) 后窗口内置真，服务端在途的旧回答残余帧会陆续到达；
           若不在此丢弃，downlink 会重开播放流把残余播完。此处置于 prebuffer 之前，
           确保打断后无论是走预缓冲还是正常路径的残余帧都被丢弃。
           窗口由 tick 自动过期，不依赖任何对端事件，故不会永久丢弃下行。 */
        if (coze_audio_drop_window_active() == QOSA_TRUE)
        {
            g_drop_window_frame_count++;
            if ((g_drop_window_frame_count == 1) || ((g_drop_window_frame_count % 20) == 0))
            {
                QLOGW("[chat_coze_audio_task] drop downlink frame in barge-in window, len=%d total=%u",
                      frame->len, (unsigned)g_drop_window_frame_count);
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
    g_player_open = QOSA_TRUE;
    g_downlink_idle_tick = qosa_get_system_tick_cnt();
    QLOGI("[chat_coze_audio_task] audio uplink/downlink active");
    return 0;
}

/**
 * @brief 停止音频上/下行：复位音频状态与打断窗口并关闭采集/播放流。
 */
void coze_audio_tasks_stop(void)
{
    g_audio_active = QOSA_FALSE;
    g_ptt_pressed = QOSA_FALSE;
    g_uplink_talking = QOSA_FALSE;
    g_downlink_playing = QOSA_FALSE;
    g_player_open = QOSA_FALSE;
    g_drop_window_until_tick = 0;
    g_last_flush_tick = 0;
    (void)audio_capture_stop();
    (void)audio_player_stop_and_flush();
}

/**
 * @brief 将一帧下行音频数据入队待播（入队后缓冲区所有权转移给下行任务）。
 *
 * 打断丢弃窗口内到达的帧一律视为旧轮残余，直接丢弃并计入丢帧统计。
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

    /* 打断丢弃窗口内到达的帧一律视为旧回答残余，源头丢弃、不入队：
       否则残余帧会在队列里积压，等窗口过期后被 downlink 当作新音频播完，
       表现为"打断后先把上一轮回答播完，才播本次回答"。窗口按 tick 自动过期，
       故新轮音频（通常发生在窗口过期之后）不会被误丢。 */
    if (coze_audio_drop_window_active() == QOSA_TRUE)
    {
        g_drop_window_frame_count++;
        if ((g_drop_window_frame_count == 1) || ((g_drop_window_frame_count % 20) == 0))
        {
            QLOGW("[chat_coze_audio_task] drop downlink push in barge-in window, len=%d total=%u",
                  len, (unsigned)g_drop_window_frame_count);
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

/*!< 排空下行队列中的待播帧并释放内存。返回被排空的帧数。 */
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
 * @brief 用户打断：立即关闭本地播放并清除待播尾音。
 *
 * 触发时机（由通信层协议分发 / 按键处理调用）：
 *   - VOICE（server_vad）模式：input_audio_buffer.speech_started
 *     —— 服务端判定用户起声，即"打断发生"的那一刻；
 *   - BUTTON（PTT）模式：按下按键准备说话时。
 *
 * 三条设计约束（缺一不可）：
 *   1. 时间有界：随后的丢弃窗口由 tick 自动过期，本地不存在任何"等对端事件
 *      才能解除"的状态，避免陷入永久静音死锁；
 *   2. 无可清内容时为 no-op：队列空且播放流未打开时直接返回，不置窗口、不关流，
 *      避免"回声反复触发 → 反复关流重开"把正常回复打成断续；
 *   3. 冷却：两次清尾间隔不小于 CHATBOT_BARGEIN_FLUSH_COOLDOWN_MS。
 */
void coze_audio_flush_downlink(void)
{
    qosa_uint32_t now = qosa_get_system_tick_cnt();

    /* 约束 3：冷却 */
    if ((g_last_flush_tick != 0) &&
        ((now - g_last_flush_tick) < (qosa_uint32_t)CHATBOT_BARGEIN_FLUSH_COOLDOWN_MS))
    {
        return;
    }

    qosa_uint32_t drained = coze_audio_drain_downlink_queue();

    /* 约束 2：没有待播内容可清 —— 打断已由上一次动作完成，此处不做任何事 */
    if ((drained == 0) && (g_player_open == QOSA_FALSE))
    {
        return;
    }

    if (g_player_open == QOSA_TRUE)
    {
        (void)audio_player_stop_and_flush();
        g_player_open = QOSA_FALSE;
    }

    g_downlink_playing = QOSA_FALSE;
    g_downlink_idle_tick = now;
    /* 约束 1：窗口到点自动过期，绝不依赖任何下行事件解除 */
    g_drop_window_until_tick = now + (qosa_uint32_t)CHATBOT_BARGEIN_DROP_WINDOW_MS;
    g_last_flush_tick = now;

    QLOGI("[chat_coze_audio_task] barge-in flush: drained=%u drop_window=%ums",
          drained, CHATBOT_BARGEIN_DROP_WINDOW_MS);
}
