/*
  reSpeaker XVF3800 音频播放模块

  这一版刻意回到开发者已经验证“能出声”的最小结构：
    URLStream -> MP3DecoderHelix -> I2SStream

  为什么这样做：
    现场远程调试时间很贵。之前复杂队列、反复 begin/end、额外 codec 初始化和采样率修改
    会让“到底是软件还是硬件没声”很难判断。现在先用最短路径证明声音链路。

  硬件连接：
    XIAO D0 / GPIO0  -> reSpeaker I2S_BCLK
    XIAO D1 / GPIO1  -> reSpeaker I2S_LRCK
    XIAO D2 / GPIO2  -> reSpeaker I2S_DATA0
    XIAO D8 / GPIO19 -> reSpeaker MCLK

  功能需求：
    - 门铃按下后播放门铃声。
    - PIR 逗留 30 秒后按随机顺序播放 3 首吵架声音。
    - 现场测试阶段可以播放“请按门铃”等语音提示。
*/

#include "audio_task.h"
#include "debug_log.h"
#include "AudioTools.h"
#include "AudioTools/Communication/AudioHttp.h"
#include "AudioTools/AudioCodecs/CodecMP3Helix.h"

const int I2S_BCLK = PIN_I2S_BCLK;
const int I2S_LRCK = PIN_I2S_LRCK;
const int I2S_DOUT = PIN_I2S_DOUT;
const int I2S_MCLK = PIN_I2S_MCLK;

URLStream urlStream;
I2SStream i2sStream;
MP3DecoderHelix mp3Decoder;
EncodedAudioStream decoder(&i2sStream, &mp3Decoder);
StreamCopy copier(decoder, urlStream);

bool isPlayingQuarrel = false;
bool isPlayingDoorbell = false;

static bool audioActive = false;
static bool sawPcm = false;
static uint32_t playId = 0;
static unsigned long playStartedAt = 0;
static unsigned long lastPcmAt = 0;
static size_t pcmBytes = 0;

static int quarrelOrder[3] = {0, 1, 2};
static int nextQuarrel = 0;
static int remainingQuarrel = 0;

static const unsigned long AUDIO_IDLE_DONE_MS = 2500;
static const unsigned long AUDIO_NO_PCM_TIMEOUT_MS = 12000;
static const unsigned long AUDIO_PRIME_MS = 2600;

const char* quarrel_urls[] = {
    "https://raw.githubusercontent.com/toffee33/doorbell-noise-audio/main/noises%20quarrel/20260411_230758.mp3",
    "https://raw.githubusercontent.com/toffee33/doorbell-noise-audio/main/noises%20quarrel/20260411_231653.mp3",
    "https://raw.githubusercontent.com/toffee33/doorbell-noise-audio/main/noises%20quarrel/20260411_231910.mp3"
};
const int quarrel_count = 3;

const char doorbell_url[] =
    "https://raw.githubusercontent.com/toffee33/doorbell-noise-audio/main/noise%20quarrel/door-bell-sound.mp3";

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

static void closeAudio() {
    urlStream.end();
    decoder.end();
    i2sStream.end();
    audioActive = false;
    sawPcm = false;
    pcmBytes = 0;
    isPlayingDoorbell = false;
    isPlayingQuarrel = false;
}

void stopAudio() {
    remainingQuarrel = 0;
    closeAudio();
    logInfo("AUDIO", "STOP");
}

static bool startAudioFromUrl(const char* url, const char* kind) {
    closeAudio();

    auto cfg = i2sStream.defaultConfig(TX_MODE);
    cfg.pin_bck = I2S_BCLK;
    cfg.pin_ws = I2S_LRCK;
    cfg.pin_data = I2S_DOUT;
    cfg.pin_mck = I2S_MCLK;
    cfg.sample_rate = 44100;
    cfg.bits_per_sample = 16;
    cfg.channels = 2;
    i2sStream.begin(cfg);

    decoder.begin();
    urlStream.begin(url, "audio/mp3");

    playId++;
    audioActive = true;
    sawPcm = false;
    playStartedAt = millis();
    lastPcmAt = playStartedAt;
    pcmBytes = 0;
    isPlayingDoorbell = strcmp(kind, "doorbell") == 0;
    isPlayingQuarrel = strcmp(kind, "quarrel") == 0;

    logInfo("AUDIO", "PLAY_START",
            String("id=") + playId
            + " kind=" + kind
            + " bclk=" + I2S_BCLK
            + " lrck=" + I2S_LRCK
            + " dout=" + I2S_DOUT
            + " mclk=" + I2S_MCLK
            + " rate=44100 bits=16 channels=2"
            + " url=" + url);
    return true;
}

static void pumpAudioFor(unsigned long ms) {
    unsigned long start = millis();
    while (audioActive && millis() - start < ms) {
        audioLoop();
        delay(1);
    }
}

static void maybeStartNextQuarrel() {
    if (remainingQuarrel <= 0 || nextQuarrel >= quarrel_count) {
        remainingQuarrel = 0;
        return;
    }

    int index = quarrelOrder[nextQuarrel++];
    remainingQuarrel--;
    logInfo("AUDIO", "QUARREL_NEXT", String("index=") + index + " remaining=" + remainingQuarrel);
    startAudioFromUrl(quarrel_urls[index], "quarrel");
}

void audioLoop() {
    if (!audioActive) {
        return;
    }

    size_t copied = copier.copy();
    unsigned long now = millis();

    if (copied > 0) {
        pcmBytes += copied;
        lastPcmAt = now;
        if (!sawPcm) {
            sawPcm = true;
            logInfo("AUDIO", "PCM_FIRST_BYTES",
                    String("id=") + playId
                    + " bytes=" + copied
                    + " first_ms=" + (now - playStartedAt));
        }
        return;
    }

    if (!sawPcm && now - playStartedAt > AUDIO_NO_PCM_TIMEOUT_MS) {
        logWarn("AUDIO", "PLAY_NO_PCM_TIMEOUT", String("id=") + playId);
        closeAudio();
        maybeStartNextQuarrel();
        return;
    }

    if (sawPcm && now - lastPcmAt > AUDIO_IDLE_DONE_MS) {
        logInfo("AUDIO", "PLAY_DONE",
                String("id=") + playId
                + " pcm_bytes=" + pcmBytes
                + " play_ms=" + (now - playStartedAt));
        closeAudio();
        maybeStartNextQuarrel();
    }
}

void playAudioFromUrl(const char* url) {
    remainingQuarrel = 0;
    startAudioFromUrl(url, "direct");
}

void playRandomQuarrel() {
    int index = random(0, quarrel_count);
    remainingQuarrel = 0;
    startAudioFromUrl(quarrel_urls[index], "quarrel");
    pumpAudioFor(AUDIO_PRIME_MS);
}

void playRandomQuarrelSequence(int trackCount) {
    if (trackCount <= 0) {
        return;
    }
    if (trackCount > quarrel_count) {
        trackCount = quarrel_count;
    }

    for (int i = 0; i < quarrel_count; i++) {
        quarrelOrder[i] = i;
    }
    for (int i = quarrel_count - 1; i > 0; i--) {
        int j = random(0, i + 1);
        int tmp = quarrelOrder[i];
        quarrelOrder[i] = quarrelOrder[j];
        quarrelOrder[j] = tmp;
    }

    nextQuarrel = 1;
    remainingQuarrel = trackCount - 1;
    logInfo("AUDIO", "QUARREL_SEQUENCE_START",
            String("tracks=") + trackCount
            + " order=" + quarrelOrder[0] + "," + quarrelOrder[1] + "," + quarrelOrder[2]);
    startAudioFromUrl(quarrel_urls[quarrelOrder[0]], "quarrel");
    pumpAudioFor(AUDIO_PRIME_MS);
}

void playDoorbell() {
    remainingQuarrel = 0;
    logInfo("AUDIO", "DOORBELL_REQUEST");
    startAudioFromUrl(doorbell_url, "doorbell");
    pumpAudioFor(AUDIO_PRIME_MS);
}

void playFieldTestPrompt(FieldTestPrompt prompt) {
    if (!FIELD_TEST_VOICE_GUIDE_ENABLED) {
        return;
    }
    if (prompt < 0 || prompt >= FIELD_PROMPT_COUNT) {
        logWarn("AUDIO", "FIELD_PROMPT_INVALID", String("prompt=") + (int)prompt);
        return;
    }

    remainingQuarrel = 0;
    logInfo("AUDIO", "FIELD_PROMPT_REQUEST", String("id=") + fieldPromptIds[prompt]);
    startAudioFromUrl(fieldPromptUrls[prompt], "field_prompt");
    pumpAudioFor(AUDIO_PRIME_MS);
}

void playButtonTestPrompt() {
    playFieldTestPrompt(FIELD_PROMPT_PRESS_DOORBELL);
}

void clearFieldTestPrompts() {
}

bool isAudioBusy() {
    return audioActive;
}
