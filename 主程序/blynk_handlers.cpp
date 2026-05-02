/*
  Blynk 远程控制实现

  Blynk 是给学生演示“手机 App 控制硬件”的入口。
  这里没有把 Blynk 逻辑散落在主程序里，而是集中在本文件，方便理解：
    - VPIN_REMOTE_CAPTURE：远程触发拍照和识别。
    - VPIN_PLAY_QUARREL：远程测试吵架音播放。
*/

#include "blynk_handlers.h"
#include "debug_log.h"
#include <BlynkSimpleEsp32.h>

void blynkRun() {
    // Blynk.run() 需要频繁调用，但前提是 WiFi 已连接。
    // 如果离线还反复调用，可能造成无意义的等待。
    static unsigned long lastBlynkConnectAttempt = 0;
    if (WiFi.status() != WL_CONNECTED) {
        return;
    }

    if (!Blynk.connected() && millis() - lastBlynkConnectAttempt > 30000) {
        // WiFi 恢复后每 30 秒尝试重连 Blynk，避免主循环被长时间阻塞。
        lastBlynkConnectAttempt = millis();
        logInfo("BLYNK", "CONNECT_START", "timeout_ms=1000");
        Blynk.connect(1000);
    }

    if (Blynk.connected()) {
        Blynk.run();
    }
}

void blynkVirtualWrite(int pin, const String& value) {
    // 包装 virtualWrite 的好处：主程序不用每次都判断 Blynk 是否在线。
    if (WiFi.status() == WL_CONNECTED && Blynk.connected()) {
        Blynk.virtualWrite(pin, value);
    }
}

void blynkVirtualWrite(int pin, int value) {
    if (WiFi.status() == WL_CONNECTED && Blynk.connected()) {
        Blynk.virtualWrite(pin, value);
    }
}

void setupBlynk() {
    // Blynk.config() 只配置 Token，不会像 Blynk.begin() 那样长时间阻塞 WiFi。
    Blynk.config(BLYNK_AUTH_TOKEN);
    if (WiFi.status() == WL_CONNECTED) {
        Blynk.connect(5000);
    }
    logInfo("BLYNK", "CONFIGURED", WiFi.status() == WL_CONNECTED ? "wifi=connected" : "wifi=offline");
}

BLYNK_WRITE(VPIN_REMOTE_CAPTURE) {
    // 手机端按下“远程拍照”按钮后，会执行和门铃拍照相同的流程。
    if (param.asInt() == 1) {
        logInfo("BLYNK", "REMOTE_CAPTURE_REQUEST");
        extern void takePhotoAndProcess(int);
        takePhotoAndProcess(0);
    }
}

BLYNK_WRITE(VPIN_PLAY_QUARREL) {
    // 用于不触发 PIR 的情况下单独验证音频链路是否正常。
    if (param.asInt() == 1) {
        logInfo("BLYNK", "PLAY_QUARREL_REQUEST");
        extern void playRandomQuarrel();
        playRandomQuarrel();
    }
}
