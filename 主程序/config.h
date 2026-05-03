/*
  智能猫眼系统 - 主控配置文件

  这个文件集中保存“全项目都会用到”的常量，适合初学者先从这里看硬件连接。
  主控板是 XIAO ESP32C6，它负责：
    1. 通过 UART 命令 ESP32-CAM 拍照。
    2. 通过 I2S 把门铃声/吵架声送到 reSpeaker XVF3800。
    3. 读取 PIR 传感器和门铃按键。
    4. 通过 WiFi 调百度云 AI 和 Blynk。
*/

#ifndef CONFIG_H
#define CONFIG_H

// Blynk 云平台配置。教学项目里直接写在代码中方便演示；
// 实际产品或公开仓库中应改成私密配置，避免 Token 泄露。
#define BLYNK_TEMPLATE_ID   "TMPL6nk5F1EOk"
#define BLYNK_TEMPLATE_NAME "My Smart Doorbell"
#define BLYNK_AUTH_TOKEN    "TNBHcworOy4Ar1f3NmYQXgFxI6Wsdmir"

// Arduino/ESP32 常用库：
// Wire      -> I2C，总线连接 reSpeaker 配置接口。
// WiFi      -> 主控联网。
// HTTPClient/ArduinoJson -> 调百度云 AI HTTP API 并解析 JSON。
#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// Base64 用于把 JPEG 二进制图片转成百度云 API 要求的字符串。
#include <Base64.h>

// 这些变量在 .ino/.cpp 中定义，在这里声明后其他模块就能使用。
extern const char* ssid;
extern const char* password;
extern const char* baidu_api_key;
extern const char* baidu_secret_key;

// XIAO ESP32C6 正面引脚分配。
// 注意：D6/D7 是给 CAM 的 UART；D0-D3/D8 是给 reSpeaker 的 I2S。
#define PIN_UART_TX   16
#define PIN_UART_RX   17
#define PIN_I2C_SDA   22
#define PIN_I2C_SCL   23
#define PIN_I2S_BCLK  0
#define PIN_I2S_LRCK  1
#define PIN_I2S_DOUT  2
#define PIN_I2S_DIN   21
#define PIN_I2S_MCLK  19
#define PIN_PIR       20
#define PIN_BUTTON    18

// 功能需求相关常量。
// PIR_HOLD_TIME = 30 秒，对应“人体逗留 >= 30 秒才触发异常流程”。
// MAX_IMAGE_SIZE 限制主控一次接收的 JPEG 大小，避免小板内存被图片撑爆。
#define PIR_HOLD_TIME       30000
#define DEBOUNCE_TIME       50
#define AUDIO_VOLUME        0.7
#define CAMERA_BAUD_RATE    115200
#define MAX_IMAGE_SIZE      65536
// 网络 MP3 来自 GitHub raw，实验室 WiFi 抖动时 connect 可能卡很久。
// 这里给音频 HTTP 一个短超时，保证它不会拖住 CAM 拍照、PIR 和门铃状态机。
#define AUDIO_HTTP_TIMEOUT_MS 2500

// 百度 AI 请求会上传 Base64 图片，数据量比普通传感器消息大得多。
// 单独设超时，方便远程日志判断是 API 拒绝、网络慢，还是主控卡住。
#define BAIDU_HTTP_TIMEOUT_MS 10000

// 远程按键测试提示音：
// 现场同学有时看不到我们的即时文字指令，所以测试固件会通过 reSpeaker
// 定时播放“请配合测试，请按一下门铃按钮”。这只用于确认 D10/GPIO18
// 的门铃按键接线是否真的能把引脚拉到 GND。
#define BUTTON_TEST_PROMPT_ENABLED        true
#define BUTTON_TEST_PROMPT_START_DELAY_MS 5000UL
#define BUTTON_TEST_PROMPT_INTERVAL_MS    8000UL

// 现场语音引导：每条提示都是一个很短的 MP3 文件，主控只保存 URL 和少量队列编号，
// 不会把整套语音读进内存。下载时由 URLStream 分段读取，MP3DecoderHelix 边解码边通过 I2S 播放。
// 这个功能专门服务异地调试：当门铃、PIR、CAM 等没有测到时，设备直接用语音提醒现场检查哪根线。
#define FIELD_TEST_VOICE_GUIDE_ENABLED    true
#define FIELD_TEST_PROMPT_QUEUE_SIZE      4
// 现场语音提示是调试引导音，不是正式门铃/威慑音。
// 超过这个时间仍未结束时主动释放音频通道，避免网络音频异常导致后续提示一直排队。
#define FIELD_TEST_PROMPT_MAX_PLAY_MS     6000UL
#define FIELD_TEST_NEXT_STEP_DELAY_MS     12000UL
#define FIELD_TEST_PROBLEM_INTERVAL_MS    15000UL
// CAM 失败后会自动用 GitHub 上的小真人图代替真实照片继续测试百度链路。
// 但如果立刻进入 HTTP 人脸识别，提示音可能要等很久才开始跑；这里给提示音一个短暂播放窗口，
// 让现场先听到“没收到摄像头，请检查供电/串口/GPIO0”，随后继续后续识别流程。
#define FIELD_TEST_CAMERA_PROMPT_BEFORE_CONTINUE_MS 4500UL

// CAM 故障隔离参数，主要服务于异地课堂调试。
// 临时飞线、供电接触不良或 CAM 未启动时，每次拍照都可能让主控等待很多秒。
// 进入短暂退避后，PIR、门铃、WiFi、Blynk 和音频还能继续被测试，不会被坏 CAM 拖住。
#define CAMERA_CAPTURE_ENABLED              true
#define CAMERA_MOCK_IMAGE_ON_FAILURE        true
// 远程调试专用：当 ESP32-CAM 还没有修好时，主控可以从本项目 GitHub raw 地址下载小 JPEG。
// 这些图片放在 test/face-test-images/，尺寸和字节数接近 QVGA 拍照结果，用来验证“图片->Base64->百度 API”链路。
// 默认不允许把这些公开测试图注册进百度人脸库，避免污染真实访客库。
#define CAMERA_REMOTE_TEST_IMAGE_ON_FAILURE true
#define CAMERA_REMOTE_TEST_IMAGE_MAX_BYTES  60000
#define CAMERA_REMOTE_TEST_IMAGE_TIMEOUT_MS 8000
#define CAMERA_REMOTE_TEST_IMAGE_ALLOW_REGISTER false
#define CAMERA_FAILURE_BACKOFF_THRESHOLD    1
#define CAMERA_FAILURE_BACKOFF_MS           60000UL
// 远程调试阶段宁可拒绝可疑图片，也不要把坏 JPEG 当成有效访客照片上传。
// SOI/EOI 是 JPEG 文件头尾标记；DONE 是 CAM 串口协议的结束行。
#define CAMERA_REQUIRE_JPEG_MARKERS         true
#define CAMERA_REQUIRE_DONE                 true

// Blynk 虚拟引脚：手机端按钮/通知控件会和这些编号对应。
#define VPIN_REMOTE_CAPTURE  1
#define VPIN_PLAY_QUARREL    2
#define VPIN_NOTIFY_TITLE    3
#define VPIN_NOTIFY_BODY     4
#define VPIN_TRIGGER_PUSH    5

#endif
