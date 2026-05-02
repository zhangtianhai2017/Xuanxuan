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

// Blynk 虚拟引脚：手机端按钮/通知控件会和这些编号对应。
#define VPIN_REMOTE_CAPTURE  1
#define VPIN_PLAY_QUARREL    2
#define VPIN_NOTIFY_TITLE    3
#define VPIN_NOTIFY_BODY     4
#define VPIN_TRIGGER_PUSH    5

#endif
