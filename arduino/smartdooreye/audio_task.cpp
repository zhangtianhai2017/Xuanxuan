/*
  reSpeaker XVF3800 音频播放模块

  本文件响应两条声音需求：
    1. 门铃按下时播放门铃提示音。
    2. PIR 逗留 30 秒后随机顺序播放 3 首吵架声音。

  硬件背景：
    XIAO ESP32C6 只负责“解码网络 MP3 并输出 I2S 数字音频”。
    reSpeaker XVF3800 接收 I2S_BCLK/I2S_LRCK/I2S_DATA/MCLK 后再把声音送到喇叭侧。

  调试原则：
    这版刻意回到已经验证过能发声的最短 AudioTools 链路：
      URLStream -> MP3DecoderHelix -> I2SStream
    不再使用复杂的提示音队列。现场远程调试时，简单结构更容易判断问题在
    URL、MP3 解码、I2S 写出，还是 reSpeaker/喇叭/供电硬件。
*/

#include "audio_task.h"
#include "debug_log.h"
#include "AudioTools.h"
#include "AudioTools/Communication/AudioHttp.h"
#include "AudioTools/AudioCodecs/CodecMP3Helix.h"

// I2S 引脚来自完整接线表：
//   D0/GPIO0  -> reSpeaker BCLK
//   D1/GPIO1  -> reSpeaker LRCK
//   D2/GPIO2  -> reSpeaker DATA0/音频输入
//   D8/GPIO19 -> reSpeaker MCLK
const int I2S_BCLK = PIN_I2S_BCLK;
const int I2S_LRCK = PIN_I2S_LRCK;
const int I2S_DOUT = PIN_I2S_DOUT;
const int I2S_MCLK = PIN_I2S_MCLK;

// 原来验证过能发声的核心链路。这里不要随意改成多层队列或异步状态机。
URLStream urlStream;
I2SStream i2sStream;
MP3DecoderHelix mp3Decoder;
EncodedAudioStream decoder(&i2sStream, &mp3Decoder);
StreamCopy copier(decoder, urlStream);

bool isPlayingQuarrel = false;
bool isPlayingDoorbell = false;

enum AudioPlayKind {
    AUDIO_KIND_NONE,
    AUDIO_KIND_DIRECT,
    AUDIO_KIND_DOORBELL,
    AUDIO_KIND_QUARREL,
    AUDIO_KIND_FIELD_PROMPT
};

static bool audioActive = false;
static bool audioSawPcm = false;
static AudioPlayKind activeKind = AUDIO_KIND_NONE;
static unsigned long audioStartedAt = 0;
static unsigned long lastPcmAt = 0;
static unsigned long lastProgressLogAt = 0;
static uint32_t audioPlayId = 0;
static size_t activePcmBytes = 0;

static int quarrelSequence[3] = {0, 1, 2};
static int nextQuarrelSequenceIndex = 0;
static int pendingQuarrelTracks = 0;

// 每次开始播放后，给音频循环一点“独占执行时间”。
// 原因很朴素：如果刚 start 就立刻去做 CAM/百度 HTTP，audioLoop() 没机会 copy，
// 现场就可能完全听不到。这个短窗口让“有没有声音”先变成可验证事实。
static const unsigned long AUDIO_PRIME_PLAY_MS = 2600;
static const unsigned long AUDIO_IDLE_DONE_MS = 2500;
static const unsigned long AUDIO_NO_PCM_TIMEOUT_MS = 12000;
static const unsigned long AUDIO_PROGRESS_LOG_INTERVAL_MS = 2500;

const char* quarrel_urls[] = {
    "https://raw.githubusercontent.com/toffee33/doorbell-noise-audio/main/noises%20quarrel/20260411_230758.mp3",
    "https://raw.githubusercontent.com/toffee33/doorbell-noise-audio/main/noises%20quarrel/20260411_231653.mp3",
    "https://raw.githubusercontent.com/toffee33/doorbell-noise-audio/main/noises%20quarrel/20260411_231910.mp3"
};
const int quarrel_count = 3;

// 注意：门铃音在 GitHub 仓库里的目录是 noise quarrel（单数 noise）。
// 之前误写成 noises quarrel 会返回 404，现场当然听不到门铃声。
const char doorbell_url[] = "https://raw.githubusercontent.com/toffee33/doorbell-noise-audio/main/noise%20quarrel/door-bell-sound.mp3";

static const char* const fieldPromptUrls[FIELD_PROMPT_COUNT] = {
    "https://raw.githubusercontent.com/zhangtianhai2017/Xuanxuan/main/test/audio-prompts/field-test-guide/00_test_start.mp3",
    "https://raw.githubusercontent.com/zhangtianhai2017/Xuanxuan/main/test/audio-prompts/field-test-guide/01_speaker_check.mp3",
    "https://raw.githubusercontent.com/zhangtianhai2017/Xuanxuan/main/test/audio-prompts/field-test-guide/02_press_doorbell.mp3",
    "https://raw.githubusercontent.com/zhangtianhai2017/Xuanxuan/main/test/audio-prompts/field-test-guide/03_doorbell_not_detected.mp3",
    "https://raw.githubusercontent.com/zhangtianhai2017/Xuanxuan/main/test/audio-prompts/field-test-guide/04_doorbell_detected.mp3",
    "https://raw.githubusercontent.com/zhangtianhai2017/Xuanxuan/main/test/audio-prompts/field-test-guide/05_pir_prompt.mp3",
    "https://raw.githubusercontent.com/zhangtianhai2017/Xuanxuan/main/test/audio-prompts/field-test-guide/06_pir_not_detected.mp3",
    "https://raw.githubusercontent.com/zhangtianhai2017/Xuanxuan/main/test/audio-prompts/field-test-guide/07_pir_detected.mp3",
    "https://raw.githubusercontent.com/zhangtianhai2017/Xuanxuan/main/test/audio-prompts/field-test-guide/08_pir_hold_passed.mp3",
    "https://raw.githubusercontent.com/zhangtianhai2017/Xuanxuan/main/test/audio-prompts/field-test-guide/09_camera_prompt.mp3",
    "https://raw.githubusercontent.com/zhangtianhai2017/Xuanxuan/main/test/audio-prompts/field-test-guide/10_camera_not_detected.mp3",
    "https://raw.githubusercontent.com/zhangtianhai2017/Xuanxuan/main/test/audio-prompts/field-test-guide/11_camera_detected.mp3",
    "https://raw.githubusercontent.com/zhangtianhai2017/Xuanxuan/main/test/audio-prompts/field-test-guide/12_face_prompt.mp3",
    "https://raw.githubusercontent.com/zhangtianhai2017/Xuanxuan/main/test/audio-prompts/field-test-guide/13_face_ok.mp3",
    "https://raw.githubusercontent.com/zhangtianhai2017/Xuanxuan/main/test/audio-prompts/field-test-guide/14_test_complete.mp3"
};

static const char* const fieldPromptIds[FIELD_PROMPT_COUNT] = {
    "00_test_start",
    "01_speaker_check",
    "02_press_doorbell",
    "03_doorbell_not_detected",
    "04_doorbell_detected",
    "05_pir_prompt",
    "06_pir_not_detected",
    "07_pir_detected",
    "08_pir_hold_passed",
    "09_camera_prompt",
    "10_camera_not_detected",
    "11_camera_detected",
    "12_face_prompt",
    "13_face_ok",
    "14_test_complete"
};

static bool isValidFieldPrompt(FieldTestPrompt prompt) {
    return prompt >= 0 && prompt < FIELD_PROMPT_COUNT;
}

static const char* audioKindName(AudioPlayKind kind) {
    switch (kind) {
        case AUDIO_KIND_DIRECT: return "direct";
        case AUDIO_KIND_DOORBELL: return "doorbell";
        case AUDIO_KIND_QUARREL: return "quarrel";
        case AUDIO_KIND_FIELD_PROMPT: return "field_prompt";
        default: return "none";
    }
}

static void resetAudioStateOnly() {
    audioActive = false;
    audioSawPcm = false;
    activeKind = AUDIO_KIND_NONE;
    audioStartedAt = 0;
    lastPcmAt = 0;
    lastProgressLogAt = 0;
    activePcmBytes = 0;
    isPlayingQuarrel = false;
    isPlayingDoorbell = false;
}

static void closeAudioObjects() {
    // 关闭顺序保持简单：先网络流，再解码器，最后 I2S。
    // 这样下一次播放一定从干净状态开始，方便学生通过日志理解。
    urlStream.end();
    decoder.end();
    i2sStream.end();
}

static void stopCurrentAudioOnly() {
    closeAudioObjects();
    resetAudioStateOnly();
}

static bool beginI2S() {
    auto cfg = i2sStream.defaultConfig(TX_MODE);
    cfg.pin_bck = I2S_BCLK;
    cfg.pin_ws = I2S_LRCK;
    cfg.pin_data = I2S_DOUT;
    cfg.pin_mck = I2S_MCLK;
    cfg.sample_rate = 44100;
    cfg.bits_per_sample = 16;
    cfg.channels = 2;

    bool ok = i2sStream.begin(cfg);
    logInfo("AUDIO", ok ? "I2S_BEGIN_OK" : "I2S_BEGIN_FAILED",
            String("bclk=") + I2S_BCLK
            + " lrck=" + I2S_LRCK
            + " dout=" + I2S_DOUT
            + " mclk=" + I2S_MCLK
            + " rate=44100 bits=16 channels=2");
    return ok;
}

static bool startAudioNow(const char* url, AudioPlayKind kind) {
    if (WiFi.status() != WL_CONNECTED) {
        logWarn("AUDIO", "SKIP_WIFI_OFFLINE", String("kind=") + audioKindName(kind));
        return false;
    }

    stopCurrentAudioOnly();

    if (!beginI2S()) {
        stopCurrentAudioOnly();
        return false;
    }

    if (!decoder.begin()) {
        logError("AUDIO", "DECODER_BEGIN_FAILED", String("kind=") + audioKindName(kind));
        stopCurrentAudioOnly();
        return false;
    }

    // 和原来验证能响的代码保持一致：begin() 等待 HTTP 首包数据。
    // 只把超时从库默认值收敛到配置值，避免坏网络卡住太久。
    urlStream.setTimeout(AUDIO_HTTP_TIMEOUT_MS);
    urlStream.setWaitForData(true);
    bool streamStarted = urlStream.begin(url, "audio/mp3");
    if (!streamStarted) {
        logError("AUDIO", "PLAY_BEGIN_FAILED",
                 String("kind=") + audioKindName(kind)
                 + " timeout_ms=" + AUDIO_HTTP_TIMEOUT_MS
                 + " wifi_status=" + WiFi.status()
                 + " rssi=" + WiFi.RSSI()
                 + " url=" + url);
        stopCurrentAudioOnly();
        return false;
    }

    audioPlayId++;
    audioActive = true;
    audioSawPcm = false;
    activeKind = kind;
    audioStartedAt = millis();
    lastPcmAt = audioStartedAt;
    lastProgressLogAt = audioStartedAt;
    activePcmBytes = 0;
    isPlayingDoorbell = kind == AUDIO_KIND_DOORBELL;
    isPlayingQuarrel = kind == AUDIO_KIND_QUARREL;

    logInfo("AUDIO", "PLAY_START",
            String("id=") + audioPlayId
            + " kind=" + audioKindName(kind)
            + " timeout_ms=" + AUDIO_HTTP_TIMEOUT_MS
            + " rssi=" + WiFi.RSSI()
            + " content_length=" + urlStream.contentLength()
            + " url=" + url);
    return true;
}

static void finishCurrentAudio(const char* eventName) {
    AudioPlayKind finishedKind = activeKind;
    unsigned long playMs = millis() - audioStartedAt;
    logInfo("AUDIO", eventName,
            String("id=") + audioPlayId
            + " kind=" + audioKindName(finishedKind)
            + " pcm_bytes=" + activePcmBytes
            + " saw_pcm=" + (audioSawPcm ? 1 : 0)
            + " play_ms=" + playMs);

    stopCurrentAudioOnly();

    if (finishedKind == AUDIO_KIND_QUARREL && pendingQuarrelTracks > 0) {
        pendingQuarrelTracks--;
        if (nextQuarrelSequenceIndex < quarrel_count) {
            int nextIndex = quarrelSequence[nextQuarrelSequenceIndex++];
            logInfo("AUDIO", "QUARREL_NEXT", String("index=") + nextIndex + " remaining_after=" + pendingQuarrelTracks);
            startAudioNow(quarrel_urls[nextIndex], AUDIO_KIND_QUARREL);
        } else {
            pendingQuarrelTracks = 0;
        }
    }
}

static void pumpAudioFor(unsigned long durationMs) {
    unsigned long startMs = millis();
    while (audioActive && millis() - startMs < durationMs) {
        audioLoop();
        delay(1);
    }
}

void audioLoop() {
    if (!audioActive) {
        return;
    }

    size_t copied = copier.copy();
    unsigned long now = millis();

    if (copied > 0) {
        activePcmBytes += copied;
        lastPcmAt = now;
        if (!audioSawPcm) {
            audioSawPcm = true;
            logInfo("AUDIO", "PCM_FIRST_BYTES",
                    String("id=") + audioPlayId
                    + " kind=" + audioKindName(activeKind)
                    + " bytes=" + copied
                    + " first_ms=" + (now - audioStartedAt));
        } else if (now - lastProgressLogAt >= AUDIO_PROGRESS_LOG_INTERVAL_MS) {
            lastProgressLogAt = now;
            logInfo("AUDIO", "PCM_PROGRESS",
                    String("id=") + audioPlayId
                    + " kind=" + audioKindName(activeKind)
                    + " pcm_bytes=" + activePcmBytes
                    + " play_ms=" + (now - audioStartedAt));
        }
        return;
    }

    if (!audioSawPcm && now - audioStartedAt >= AUDIO_NO_PCM_TIMEOUT_MS) {
        finishCurrentAudio("PLAY_NO_PCM_TIMEOUT");
        return;
    }

    if (audioSawPcm && now - lastPcmAt >= AUDIO_IDLE_DONE_MS) {
        finishCurrentAudio("PLAY_DONE");
    }
}

void stopAudio() {
    pendingQuarrelTracks = 0;
    logInfo("AUDIO", "STOP");
    stopCurrentAudioOnly();
}

void clearFieldTestPrompts() {
    // 这一版没有提示音队列，函数保留是为了让调用方不用改复杂。
}

bool isAudioBusy() {
    return audioActive;
}

void playAudioFromUrl(const char* url) {
    pendingQuarrelTracks = 0;
    startAudioNow(url, AUDIO_KIND_DIRECT);
}

void playRandomQuarrel() {
    int index = random(0, quarrel_count);
    pendingQuarrelTracks = 0;
    logInfo("AUDIO", "QUARREL_SINGLE_REQUEST", String("index=") + index);
    startAudioNow(quarrel_urls[index], AUDIO_KIND_QUARREL);
    pumpAudioFor(AUDIO_PRIME_PLAY_MS);
}

void playRandomQuarrelSequence(int trackCount) {
    // 功能需求：PIR 异常逗留后随机播放 3 首吵架声音。
    // 为了保持代码直观，只做一个很小的顺序表：第一首现在播，后两首等 audioLoop()
    // 判断上一首结束后再播。
    if (trackCount <= 0) {
        logWarn("AUDIO", "QUARREL_SEQUENCE_EMPTY", String("requested=") + trackCount);
        return;
    }

    if (trackCount > quarrel_count) {
        trackCount = quarrel_count;
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

    pendingQuarrelTracks = trackCount - 1;
    nextQuarrelSequenceIndex = 1;
    logInfo("AUDIO", "QUARREL_SEQUENCE_START",
            String("tracks=") + trackCount
            + " order=" + quarrelSequence[0] + "," + quarrelSequence[1] + "," + quarrelSequence[2]);
    startAudioNow(quarrel_urls[quarrelSequence[0]], AUDIO_KIND_QUARREL);
    pumpAudioFor(AUDIO_PRIME_PLAY_MS);
}

void playDoorbell() {
    // 功能需求：门铃按下时应立即听到提示音。
    // 这里会先给音频 2.6 秒执行窗口，再让主流程继续拍照和百度识别。
    pendingQuarrelTracks = 0;
    logInfo("AUDIO", "DOORBELL_REQUEST");
    if (startAudioNow(doorbell_url, AUDIO_KIND_DOORBELL)) {
        pumpAudioFor(AUDIO_PRIME_PLAY_MS);
    }
}

void playFieldTestPrompt(FieldTestPrompt prompt) {
    if (!FIELD_TEST_VOICE_GUIDE_ENABLED) {
        return;
    }

    if (!isValidFieldPrompt(prompt)) {
        logWarn("AUDIO", "FIELD_PROMPT_INVALID", String("prompt=") + (int)prompt);
        return;
    }

    logInfo("AUDIO", "FIELD_PROMPT_REQUEST", String("id=") + fieldPromptIds[prompt]);
    if (startAudioNow(fieldPromptUrls[prompt], AUDIO_KIND_FIELD_PROMPT)) {
        pumpAudioFor(AUDIO_PRIME_PLAY_MS);
    }
}

void playButtonTestPrompt() {
    // 远程调试功能：每隔一段时间提示现场按 GPIO18 门铃按钮。
    playFieldTestPrompt(FIELD_PROMPT_PRESS_DOORBELL);
}
