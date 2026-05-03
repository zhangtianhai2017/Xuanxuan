/*
  reSpeaker XVF3800 音频播放模块

  本文件响应两条声音需求：
    1. 门铃按下时播放门铃提示音。
    2. PIR 逗留 30 秒后随机顺序播放 3 首吵架声音。

  硬件背景：
    XIAO ESP32C6 通过 I2S 把音频数据送到 reSpeaker。
    I2S 不是普通串口，它有 BCLK/LRCK/DATA/MCLK 多根线，适合连续音频流。
*/

#include "audio_task.h"
#include "config.h"
#include "debug_log.h"
#include "AudioTools.h"
#include "AudioTools/Communication/AudioHttp.h"
#include "AudioTools/AudioCodecs/CodecMP3Helix.h"
#include "AudioTools/AudioCodecs/CodecWAV.h"

const int I2S_BCLK = 0;
const int I2S_LRCK = 1;
const int I2S_DOUT = 2;
const int I2S_MCLK = 19;

// AudioTools 的典型链路：
// URLStream 从网络读取 MP3 -> MP3DecoderHelix 解码 -> I2SStream 输出到硬件。
URLStream urlStream;
I2SStream i2sStream;
MP3DecoderHelix mp3Decoder;
WAVDecoder wavDecoder;
EncodedAudioStream decoder(&i2sStream, &mp3Decoder);
EncodedAudioStream wavDecoderStream(&i2sStream, &wavDecoder);
StreamCopy copier(decoder, urlStream);
StreamCopy wavCopier(wavDecoderStream, urlStream);

enum AudioCodecKind {
    AUDIO_CODEC_MP3,
    AUDIO_CODEC_WAV
};

bool isPlayingQuarrel = false;
bool isPlayingDoorbell = false;
// 下面几个变量组成一个很小的音频状态机。
// 它让“连续播放 3 首吵架音”不会阻塞主循环，主控仍能继续处理按键/PIR/WiFi。
static bool audioActive = false;
static bool audioSawData = false;
static unsigned long lastAudioDataTime = 0;
static int pendingQuarrelTracks = 0;
static int quarrelSequence[3] = {0, 1, 2};
static int nextQuarrelSequenceIndex = 0;
static const char* pendingAudioUrl = nullptr;
static bool pendingAudioIsQuarrel = false;
static bool pendingAudioIsDoorbell = false;
static AudioCodecKind activeAudioCodec = AUDIO_CODEC_MP3;

static bool startNextQueuedQuarrelTrack();

const char* quarrel_urls[] = {
    "https://raw.githubusercontent.com/toffee33/doorbell-noise-audio/main/noises%20quarrel/20260411_230758.mp3",
    "https://raw.githubusercontent.com/toffee33/doorbell-noise-audio/main/noises%20quarrel/20260411_231653.mp3",
    "https://raw.githubusercontent.com/toffee33/doorbell-noise-audio/main/noises%20quarrel/20260411_231910.mp3"
};
const int quarrel_count = 3;

const char doorbell_url[] = "https://raw.githubusercontent.com/toffee33/doorbell-noise-audio/main/noises%20quarrel/door-bell-sound.mp3";
const char button_test_prompt_url[] = "https://raw.githubusercontent.com/zhangtianhai2017/Xuanxuan/main/test/audio-prompts/please_press_doorbell_button_22k.wav";
// 如果长时间没有复制到音频数据，就认为一首歌结束或启动失败。
// 这是网络流的简化判断，适合教学项目，真实产品可换成更严格的 EOF 回调。
const unsigned long AUDIO_IDLE_ADVANCE_MS = 2500;
const unsigned long AUDIO_START_TIMEOUT_MS = 10000;

static void stopCurrentAudio() {
    // 关闭当前网络流、解码器和 I2S，避免下一首歌复用旧状态。
    urlStream.end();
    decoder.end();
    wavDecoderStream.end();
    i2sStream.end();
    audioActive = false;
    audioSawData = false;
    lastAudioDataTime = 0;
    isPlayingQuarrel = false;
    isPlayingDoorbell = false;
    activeAudioCodec = AUDIO_CODEC_MP3;
}

static bool urlEndsWith(const char* url, const char* suffix) {
    // URLStream 只负责下载字节流，真正的解码器要由我们选择。
    // 现有门铃/吵架声是 MP3，新增加的测试语音是 WAV，所以用后缀区分即可。
    size_t urlLen = strlen(url);
    size_t suffixLen = strlen(suffix);
    if (urlLen < suffixLen) {
        return false;
    }
    return strcasecmp(url + urlLen - suffixLen, suffix) == 0;
}

static bool queueAudioFromUrl(const char* url, bool isQuarrel, bool isDoorbell) {
    // 事件处理函数只负责“提出播放请求”，真正打开网络流放到 audioLoop()。
    // 这样 PIR/门铃可以先继续执行拍照流程，不会被 GitHub/raw 网络连接卡住数秒。
    pendingAudioUrl = url;
    pendingAudioIsQuarrel = isQuarrel;
    pendingAudioIsDoorbell = isDoorbell;
    logInfo("AUDIO", "PLAY_QUEUED", String("url=") + url);
    return true;
}

static bool startAudioFromUrl(const char* url) {
    // 网络 MP3 依赖 WiFi。离线时直接跳过，不让主循环卡在网络请求里。
    if (WiFi.status() != WL_CONNECTED) {
        stopCurrentAudio();
        logWarn("AUDIO", "SKIP_WIFI_OFFLINE");
        return false;
    }

    stopCurrentAudio();
    bool useWavDecoder = urlEndsWith(url, ".wav");
    activeAudioCodec = useWavDecoder ? AUDIO_CODEC_WAV : AUDIO_CODEC_MP3;

    auto cfg = i2sStream.defaultConfig(TX_MODE);
    // I2S 引脚必须和硬件接线一致；这些值来自 config.h 中的接线方案。
    cfg.pin_bck = I2S_BCLK;
    cfg.pin_ws = I2S_LRCK;
    cfg.pin_data = I2S_DOUT;
    cfg.pin_mck = I2S_MCLK;
    cfg.sample_rate = 44100;
    cfg.bits_per_sample = 16;
    cfg.channels = 2;
    i2sStream.begin(cfg);

    if (useWavDecoder) {
        wavDecoderStream.begin();
    } else {
        decoder.begin();
    }
    // URLStream.begin() 内部会发起 TCP/TLS/HTTP 请求。弱 WiFi 下它可能阻塞，
    // 所以先设置短超时，并且不等待首包数据，避免音频网络问题拖住门铃/PIR 主流程。
    urlStream.setTimeout(AUDIO_HTTP_TIMEOUT_MS);
    urlStream.setWaitForData(false);
    bool streamStarted = urlStream.begin(url, useWavDecoder ? "audio/wav" : "audio/mp3");
    if (!streamStarted) {
        logError("AUDIO", "PLAY_BEGIN_FAILED",
                 String("timeout_ms=") + AUDIO_HTTP_TIMEOUT_MS
                 + " wifi_status=" + WiFi.status()
                 + " rssi=" + WiFi.RSSI()
                 + " url=" + url);
        stopCurrentAudio();
        return false;
    }

    audioActive = true;
    audioSawData = false;
    lastAudioDataTime = millis();
    logInfo("AUDIO", "PLAY_START",
            String("timeout_ms=") + AUDIO_HTTP_TIMEOUT_MS
            + " rssi=" + WiFi.RSSI()
            + " codec=" + (useWavDecoder ? "wav" : "mp3")
            + " url=" + url);
    return true;
}

static void handleQueuedAudioStartFailure() {
    // 如果一首吵架音因为 WiFi/网络失败没有启动，继续尝试队列里的下一首。
    // 这保证 PIR 的“三首随机音”需求在网络抖动时尽量向前推进。
    if (pendingAudioIsQuarrel && pendingQuarrelTracks > 0) {
        pendingQuarrelTracks--;
        logWarn("AUDIO", "QUEUE_ADVANCE_AFTER_START_FAIL", String("pending_before=") + (pendingQuarrelTracks + 1));
        pendingAudioUrl = nullptr;
        if (!startNextQueuedQuarrelTrack()) {
            pendingQuarrelTracks = 0;
            logError("AUDIO", "QUEUE_ADVANCE_FAILED");
        }
        return;
    }

    pendingAudioUrl = nullptr;
    pendingQuarrelTracks = 0;
}

static bool startPendingAudioIfNeeded() {
    if (pendingAudioUrl == nullptr) {
        return false;
    }

    const char* url = pendingAudioUrl;
    bool wasQuarrel = pendingAudioIsQuarrel;
    bool wasDoorbell = pendingAudioIsDoorbell;
    pendingAudioUrl = nullptr;

    if (startAudioFromUrl(url)) {
        isPlayingQuarrel = wasQuarrel;
        isPlayingDoorbell = wasDoorbell;
        return true;
    }

    pendingAudioIsQuarrel = wasQuarrel;
    pendingAudioIsDoorbell = wasDoorbell;
    handleQueuedAudioStartFailure();
    return false;
}

static bool startQuarrelTrackByIndex(int index) {
    // index 是吵架音数组下标。先做边界检查，防止数组越界。
    if (index < 0 || index >= quarrel_count) {
        return false;
    }

    return queueAudioFromUrl(quarrel_urls[index], true, false);
}

static bool startNextRandomQuarrelTrack() {
    // 用于 Blynk 手动测试：随机播一首。
    int index = random(0, quarrel_count);
    return startQuarrelTrackByIndex(index);
}

static bool startNextQueuedQuarrelTrack() {
    // 用于 PIR 异常逗留：按已经洗牌好的顺序播下一首。
    if (nextQuarrelSequenceIndex >= quarrel_count) {
        return false;
    }

    return startQuarrelTrackByIndex(quarrelSequence[nextQuarrelSequenceIndex++]);
}

void audioLoop() {
    // 这个函数必须在主 loop() 中频繁调用。
    // 每次只复制一小块音频数据，避免播放音频时系统无法响应按键/PIR。
    if (!audioActive) {
        startPendingAudioIfNeeded();
        return;
    }

    size_t copied = (activeAudioCodec == AUDIO_CODEC_WAV) ? wavCopier.copy() : copier.copy();
    if (copied > 0) {
        audioSawData = true;
        lastAudioDataTime = millis();
        return;
    }

    unsigned long idleTime = millis() - lastAudioDataTime;
    bool startTimedOut = !audioSawData && idleTime > AUDIO_START_TIMEOUT_MS;
    bool playbackEnded = audioSawData && idleTime > AUDIO_IDLE_ADVANCE_MS;
    if (!startTimedOut && !playbackEnded) {
        return;
    }

    if (pendingQuarrelTracks > 0) {
        // 当前一首结束后，如果队列里还有吵架音，就自动切到下一首。
        logInfo("AUDIO", playbackEnded ? "TRACK_ENDED" : "TRACK_START_TIMEOUT",
                String("idle_ms=") + idleTime + " pending_before=" + pendingQuarrelTracks);
        pendingQuarrelTracks--;
        stopCurrentAudio();
        if (!startNextQueuedQuarrelTrack()) {
            pendingQuarrelTracks = 0;
            logError("AUDIO", "QUEUE_ADVANCE_FAILED");
            stopCurrentAudio();
        }
    } else {
        logInfo("AUDIO", playbackEnded ? "PLAY_DONE" : "PLAY_START_TIMEOUT", String("idle_ms=") + idleTime);
        stopCurrentAudio();
    }
}

void stopAudio() {
    pendingQuarrelTracks = 0;
    pendingAudioUrl = nullptr;
    logInfo("AUDIO", "STOP");
    stopCurrentAudio();
}

void playAudioFromUrl(const char* url) {
    pendingQuarrelTracks = 0;
    stopCurrentAudio();
    queueAudioFromUrl(url, false, false);
}

bool isAudioBusy() {
    // 测试提示音不应该打断门铃声、PIR 威慑音或正在建立的网络音频连接。
    return audioActive || pendingAudioUrl != nullptr || pendingQuarrelTracks > 0;
}

void playRandomQuarrel() {
    pendingQuarrelTracks = 0;
    logInfo("AUDIO", "QUARREL_SINGLE_REQUEST");
    startNextRandomQuarrelTrack();
}

void playRandomQuarrelSequence(int trackCount) {
    // 功能需求：PIR 异常逗留后随机播放 3 首吵架声音。
    // 这里先把 3 首音频下标洗牌，再按洗牌顺序播放，避免三次随机抽到同一首。
    if (trackCount <= 0) {
        logWarn("AUDIO", "QUARREL_SEQUENCE_EMPTY", String("requested=") + trackCount);
        stopAudio();
        return;
    }

    if (trackCount > quarrel_count) {
        trackCount = quarrel_count;
    }

    bool replacingOldAudio = audioActive || pendingAudioUrl != nullptr || pendingQuarrelTracks > 0;
    if (replacingOldAudio) {
        // PIR 可能在上一轮 3 首吵架音还没播完时再次触发。
        // 这里明确重启一轮新的 3 首序列，避免旧队列和新队列混在一起，导致第一首被覆盖或少播。
        logWarn("AUDIO", "QUARREL_SEQUENCE_RESTART",
                String("active=") + (audioActive ? 1 : 0)
                + " pending_url=" + (pendingAudioUrl != nullptr ? 1 : 0)
                + " pending_tracks=" + pendingQuarrelTracks);
        pendingAudioUrl = nullptr;
        pendingQuarrelTracks = 0;
        stopCurrentAudio();
    }

    for (int i = 0; i < quarrel_count; i++) {
        quarrelSequence[i] = i;
    }
    for (int i = quarrel_count - 1; i > 0; i--) {
        int j = random(0, i + 1);
        int tmp = quarrelSequence[i];
        quarrelSequence[i] = quarrelSequence[j];
        quarrelSequence[j] = tmp;
    }

    nextQuarrelSequenceIndex = 1;
    pendingQuarrelTracks = trackCount - 1;
    logInfo("AUDIO", "QUARREL_SEQUENCE_START",
            String("tracks=") + trackCount + " order=" + quarrelSequence[0] + "," + quarrelSequence[1] + "," + quarrelSequence[2]);
    if (!startQuarrelTrackByIndex(quarrelSequence[0])) {
        pendingQuarrelTracks = 0;
        logError("AUDIO", "QUARREL_SEQUENCE_START_FAILED");
    }
}

void playDoorbell() {
    // 功能需求：门铃按下时播放门铃提示音。
    // 播放门铃会取消正在排队的吵架音，避免两种场景的声音混在一起。
    pendingQuarrelTracks = 0;
    logInfo("AUDIO", "DOORBELL_REQUEST");
    stopCurrentAudio();
    queueAudioFromUrl(doorbell_url, false, true);
}

void playButtonTestPrompt() {
    // 远程调试功能：当我们需要现场同学按门铃但日志迟迟没有检测到 GPIO18
    // 变 LOW 时，播放语音提示。这里不调用 stopCurrentAudio()，避免影响
    // 正常门铃/PIR 流程；如果音频链路正忙，就跳过本轮提示。
    if (isAudioBusy()) {
        logWarn("AUDIO", "BUTTON_TEST_PROMPT_SKIP",
                String("active=") + (audioActive ? 1 : 0)
                + " pending_url=" + (pendingAudioUrl != nullptr ? 1 : 0)
                + " quarrel_pending=" + pendingQuarrelTracks);
        return;
    }

    logInfo("AUDIO", "BUTTON_TEST_PROMPT_REQUEST");
    queueAudioFromUrl(button_test_prompt_url, false, false);
}
