/*
  智能猫眼系统 - 主控入口

  给初学者的阅读顺序：
    1. 先看 setup()：它说明系统上电后初始化了哪些硬件。
    2. 再看 loop()：它说明系统一直循环处理哪些事件。
    3. 最后看 handleButton()/handlePIR()：它们分别对应两个核心需求。

  本文件运行在 XIAO ESP32C6 上，不运行在 ESP32-CAM 上。
*/
#define USE_AUDIO_LOGGING false
#define ARDUINOJSON_ENABLE_STD_STREAM 0
#define ARDUINOJSON_ENABLE_STD_STRING 0
#define ARDUINOJSON_USE_DOUBLE 0
#define ARDUINOJSON_USE_LONG_LONG 0
#define ARDUINOJSON_ENABLE_COMMENTS 0
#define BLYNK_NO_BUILTIN
#define BLYNK_USE_128_VPINS

#include "config.h"
#include "debug_log.h"
#include "audio_task.h"
#include "camera_comm.h"
#include "face_recognition.h"
#include "blynk_handlers.h"

// WiFi 账号密码。教学项目直接写在这里方便烧录测试；
// 实际项目建议放进不提交的私密配置文件。
const char* ssid = "123";
const char* password = "12345678";
unsigned long lastTokenRefresh = 0;
const unsigned long TOKEN_REFRESH_INTERVAL = 25UL * 24 * 3600 * 1000;
const unsigned long TOKEN_RETRY_INTERVAL = 30000;
const unsigned long WIFI_CONNECT_TIMEOUT = 15000;
const unsigned long WIFI_RECONNECT_INTERVAL = 30000;

void setupWiFi();
void setupI2C();
void handleWiFiReconnect();
void handleTokenRefresh();
void handlePIR();
void handleButton();

void setup() {
    // Serial 是给电脑串口监视器看的调试日志。
    // Serial1 是给 ESP32-CAM 的硬件串口，接线见 config.h。
    Serial.begin(115200);
    Serial1.begin(CAMERA_BAUD_RATE, SERIAL_8N1, PIN_UART_RX, PIN_UART_TX);

    logInfo("BOOT", "START", "board=XIAO_ESP32C6 usb_baud=115200 cam_baud=115200");
    forwardCameraLogsFor(800);
    requestCameraStatus(500);

    // PIR 输出高电平表示检测到人体；门铃按键使用内部上拉，
    // 所以按下时 GPIO 会被拉到 GND，读数变成 LOW。
    pinMode(PIN_PIR, INPUT);
    pinMode(PIN_BUTTON, INPUT_PULLUP);

    setupWiFi();
    setupI2C();
    randomSeed(analogRead(0));

    baidu_access_token = getBaiduAccessToken();
    if (baidu_access_token.length() > 0) {
        lastTokenRefresh = millis();
    }

    setupBlynk();
    loadCounters();

    logInfo("BOOT", "READY");
}

void loop() {
    // loop() 不要写成长时间阻塞函数。主控需要同时照顾：
    // Blynk、音频流、WiFi 重连、按键、PIR 和 Token 刷新。
    pollCameraLogs();
    blynkRun();
    audioLoop();
    handleWiFiReconnect();
    handleButton();
    handlePIR();
    handleTokenRefresh();
    delay(10);
}

void setupWiFi() {
    // 主控联网用于百度云 AI、Blynk 和网络 MP3。
    // 这里设置超时：即使实验室 WiFi 不可用，门铃/PIR/串口拍照也能继续测试。
    logInfo("WIFI", "CONNECT_START", String("ssid=") + ssid + " timeout_ms=" + WIFI_CONNECT_TIMEOUT);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    unsigned long wifiStart = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < WIFI_CONNECT_TIMEOUT) {
        delay(800);
        pollCameraLogs();
    }

    if (WiFi.status() == WL_CONNECTED) {
        logInfo("WIFI", "CONNECTED", String("ip=") + WiFi.localIP().toString());
    } else {
        logWarn("WIFI", "CONNECT_TIMEOUT", "mode=offline");
    }
}

void handleWiFiReconnect() {
    // 如果开机时没连上 WiFi，系统每隔一段时间重试一次。
    // 这比在 setup() 里无限等待更适合教学和调试。
    static unsigned long lastReconnectAttempt = 0;
    static unsigned long reconnectStart = 0;
    static bool reconnectInProgress = false;

    if (WiFi.status() == WL_CONNECTED) {
        if (reconnectInProgress) {
            logInfo("WIFI", "RECONNECT_OK", String("ip=") + WiFi.localIP().toString());
        }
        reconnectInProgress = false;
        return;
    }

    if (reconnectInProgress) {
        if (millis() - reconnectStart < WIFI_CONNECT_TIMEOUT) {
            // WiFi.begin() 是异步的。连接还在进行时不要反复 begin()，
            // 否则 ESP-IDF 会打印 "sta is connecting, cannot set config"。
            return;
        }
        reconnectInProgress = false;
        logWarn("WIFI", "RECONNECT_TIMEOUT", String("status=") + WiFi.status());
    }

    if (millis() - lastReconnectAttempt >= WIFI_RECONNECT_INTERVAL) {
        lastReconnectAttempt = millis();
        reconnectStart = millis();
        reconnectInProgress = true;
        logInfo("WIFI", "RECONNECT_START", String("status=") + WiFi.status());
        WiFi.disconnect();
        delay(50);
        WiFi.begin(ssid, password);
    }
}

void setupI2C() {
    // reSpeaker XVF3800 有 I2C 控制接口，本项目预留初始化。
    // 当前音频播放主要通过 I2S 输出，I2C 可用于后续配置音量/模式。
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    logInfo("I2C", "READY", String("sda=") + PIN_I2C_SDA + " scl=" + PIN_I2C_SCL);
}

void handleTokenRefresh() {
    // 百度 access_token 会过期，所以系统定期刷新。
    // 如果没网或请求失败，不要卡死主循环，只等待下一次重试。
    if (baidu_access_token.length() > 0) {
        if (millis() - lastTokenRefresh > TOKEN_REFRESH_INTERVAL) {
            logInfo("BAIDU", "TOKEN_REFRESH_START");
            String newToken = getBaiduAccessToken();
            if (newToken.length() > 0) {
                baidu_access_token = newToken;
                lastTokenRefresh = millis();
                logInfo("BAIDU", "TOKEN_REFRESH_OK");
            } else {
                logWarn("BAIDU", "TOKEN_REFRESH_FAILED");
            }
        }
    } else {
        static unsigned long lastRetry = 0;
        if (lastRetry == 0 || millis() - lastRetry > TOKEN_RETRY_INTERVAL) {
            lastRetry = millis();
            baidu_access_token = getBaiduAccessToken();
            if (baidu_access_token.length() > 0) {
                lastTokenRefresh = millis();
                logInfo("BAIDU", "TOKEN_RETRY_OK");
            } else {
                logWarn("BAIDU", "TOKEN_RETRY_FAILED");
            }
        }
    }
}

void handlePIR() {
    // 功能需求 2：人体移动侦测。
    // PIR 只要检测到人体就会输出 HIGH，但需求要求“逗留 >= 30 秒”才触发。
    // 因此这里不是检测到 HIGH 立刻报警，而是记录 HIGH 持续时间。
    static unsigned long lastPIRHighTime = 0;
    static bool pirWasHigh = false;
    static bool pirTriggered = false;

    bool pirState = digitalRead(PIN_PIR);
    if (pirState == HIGH) {
        if (!pirWasHigh) {
            pirWasHigh = true;
            lastPIRHighTime = millis();
            logInfo("PIR", "MOTION_START", "pin=20 state=HIGH");
        } else if (!pirTriggered && (millis() - lastPIRHighTime >= PIR_HOLD_TIME)) {
            pirTriggered = true;
            logInfo("PIR", "HOLD_REACHED", String("hold_ms=") + (millis() - lastPIRHighTime) + " capture_count=3");
            // 功能需求：PIR 异常逗留后应尽快发出声音威慑。
            // 因此先启动 3 首吵架音队列，再尝试拍照；即使 CAM 当前故障，音频测试也不被拖住。
            playRandomQuarrelSequence(3);
            // 连续拍 3 张：每一张都会走 takePhotoAndProcess()，
            // 即拍照 -> Base64 -> 百度人脸识别。
            bool registeredNewFaceThisEvent = false;
            for (int i = 0; i < 3; i++) {
                // 同一次 PIR 逗留事件里，后续照片仍然会搜索熟人和统计访问，
                // 但如果前面已经注册过新人，就不再注册第二个新人。
                FaceProcessResult result = takePhotoAndProcess(i, !registeredNewFaceThisEvent);
                if (result == FACE_PROCESS_REGISTERED) {
                    registeredNewFaceThisEvent = true;
                }
                audioLoop();
                delay(500);
            }
        }
    } else {
        if (pirWasHigh) {
            logInfo("PIR", "MOTION_END", String("duration_ms=") + (millis() - lastPIRHighTime));
        }
        pirWasHigh = false;
        pirTriggered = false;
    }
}

void handleButton() {
    // 功能需求 1：门铃触发。
    // 这里用软件消抖，避免机械按键抖动造成一次按下被识别成多次。
    static unsigned long lastDebounceTime = 0;
    static bool lastButtonState = HIGH;
    static bool buttonTriggered = false;

    bool currentState = digitalRead(PIN_BUTTON);
    if (currentState != lastButtonState) {
        lastDebounceTime = millis();
    }

    if ((millis() - lastDebounceTime) > DEBOUNCE_TIME) {
        if (currentState == LOW && !buttonTriggered) {
            buttonTriggered = true;
            logInfo("BUTTON", "DOORBELL_PRESSED", "pin=18 state=LOW");
            // 功能需求：访客按门铃后，提示音应立即响应，让现场能先确认按键和音频链路。
            // 拍照/百度识别随后执行；CAM 故障时会使用模拟图，不会阻塞其它测试太久。
            playDoorbell();
            takePhotoAndProcess(0);

            // Blynk 通知是辅助功能：如果 Blynk 未连接，包装函数会自动跳过。
            blynkVirtualWrite(VPIN_NOTIFY_TITLE, "Doorbell");
            blynkVirtualWrite(VPIN_NOTIFY_BODY, "A visitor pressed the doorbell; photo captured and processed.");
            blynkVirtualWrite(VPIN_TRIGGER_PUSH, 1);
        } else if (currentState == HIGH) {
            buttonTriggered = false;
        }
    }

    lastButtonState = currentState;
}
