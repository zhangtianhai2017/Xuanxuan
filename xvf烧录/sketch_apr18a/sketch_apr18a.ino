/*
  reSpeaker XVF3800 I2S output test.
  Plays a 440 Hz sine wave to verify I2S wiring and firmware mode.

  这是一个独立硬件测试草图，不属于主控完整功能。
  当学生怀疑 reSpeaker、I2S 接线或 XVF3800 固件不正常时，
  可以先烧录这个最小程序：如果能听到 440Hz 声音，说明播放链路基本正常。
*/

#include <Arduino.h>
#include "AudioTools.h"

#define PIN_I2S_BCLK  0
#define PIN_I2S_LRCK  1
#define PIN_I2S_DIN   2

// XVF3800 I2S 固件常用 16kHz 单声道 16-bit 测试音频。
// 如果后续改成立体声或 44.1kHz，要确认 XVF3800 当前固件模式支持。
const int sampleRate = 16000;
const int channels = 1;
const int bitsPerSample = 16;

I2SStream i2s;
SineWaveGenerator<int16_t> sineWave(30000);
GeneratedSoundStream<int16_t> sound(sineWave);
StreamCopy copier(i2s, sound);

static void logTest(const char* level, const char* event, const String& detail = "") {
    Serial.print('[');
    Serial.print(millis());
    Serial.print("][");
    Serial.print(level);
    Serial.print("][XVF_TEST][");
    Serial.print(event);
    Serial.print(']');
    if (detail.length() > 0) {
        Serial.print(' ');
        Serial.print(detail);
    }
    Serial.println();
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    logTest("INFO", "BOOT_START", "board=XIAO_ESP32C6 target=reSpeaker_XVF3800");

    auto config = i2s.defaultConfig(TX_MODE);
    // TX_MODE 表示 XIAO 向 reSpeaker 输出音频。
    // pin_bck/pin_ws/pin_data 必须和实际接线 D0/D1/D2 一致。
    config.sample_rate = sampleRate;
    config.channels = channels;
    config.bits_per_sample = bitsPerSample;
    config.i2s_format = I2S_STD_FORMAT;
    config.pin_bck = PIN_I2S_BCLK;
    config.pin_ws = PIN_I2S_LRCK;
    config.pin_data = PIN_I2S_DIN;

    i2s.begin(config);

    AudioInfo info(sampleRate, channels, bitsPerSample);
    // 440Hz 是标准 A4 音，容易通过耳朵判断是否在持续播放。
    sineWave.begin(info, 440);

    logTest("INFO", "I2S_READY",
            String("bclk=") + PIN_I2S_BCLK + " lrck=" + PIN_I2S_LRCK + " data=" + PIN_I2S_DIN
            + " sample_rate=" + sampleRate + " channels=" + channels + " bits=" + bitsPerSample);
    logTest("INFO", "SINE_PLAYING", "freq_hz=440");
}

void loop() {
    copier.copy();
}
