/*
  ESP32-CAM 纯串口拍照固件

  这个文件烧录到 ESP32-CAM，不烧录到 XIAO ESP32C6。
  它的职责非常单一：等待主控通过 UART 发来 CAPTURE 命令，然后回传 JPEG。

  硬件接线：
    XIAO TX(GPIO16) -> ESP32-CAM U0RXD(GPIO3)
    XIAO RX(GPIO17) <- ESP32-CAM U0TXD(GPIO1)
    XIAO GND        -- ESP32-CAM GND

  烧录提醒：
    ESP32-CAM 烧录时 GPIO0 通常要接 GND；
    烧录完成运行摄像头时 GPIO0 必须断开，否则会影响 XCLK/启动状态。
*/

#include <Arduino.h>
#include "esp_camera.h"

// ===========================
// Select camera model in board_config.h
// ===========================
#include "board_config.h"

// ===========================
// Serial command protocol (match with main controller)
// ===========================
#define CMD_CAPTURE "CAPTURE"
#define CMD_STATUS  "STATUS"
#define RESP_OK     "OK"
#define RESP_ERROR  "ERROR"
#define RESP_DONE   "DONE"

// Forward declaration
void sendImageToSerial(camera_fb_t *fb);
void sendStatusToSerial();

bool cameraReady = false;
uint32_t cameraInitError = 0;
uint32_t captureCount = 0;
uint32_t fbFailCount = 0;
uint32_t lastFbLen = 0;
uint32_t lastCaptureMs = 0;
uint32_t lastSendMs = 0;
uint32_t lastJpegWriteBytes = 0;

static void camLog(const char* level, const char* event, const String& detail = "") {
  // CAM 的日志只在单独接 USB-TTL 调试时给人看。
  // CAPTURE 响应里的 OK/ERROR/JPEG/DONE 不能调用这个函数，否则会污染主控协议。
  Serial.print('[');
  Serial.print(millis());
  Serial.print("][");
  Serial.print(level);
  Serial.print("][CAM][");
  Serial.print(event);
  Serial.print(']');
  if (detail.length() > 0) {
    Serial.print(' ');
    Serial.print(detail);
  }
  Serial.println();
}

void setup() {
  // 这里的 Serial 既接 USB-TTL 烧录器，也接主控 UART。
  // 因为后面要发送 JPEG 二进制，绝不能打开底层调试输出。
  Serial.begin(115200);
  Serial.setTimeout(100);
  Serial.setDebugOutput(false);
  camLog("INFO", "BOOT_START", "fw=serial_capture board=ESP32_CAM baud=115200");

#if PWDN_GPIO_NUM >= 0
  // AI Thinker ESP32-CAM 的 PWDN(GPIO32) 控制摄像头电源。
  // 拉低表示摄像头上电工作。
  pinMode(PWDN_GPIO_NUM, OUTPUT);
  digitalWrite(PWDN_GPIO_NUM, LOW);
  delay(100);
#endif

  camera_config_t config;
  // 以下 pin_xxx 全部来自 camera_pins.h，必须和实际摄像头板型一致。
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  // QVGA 图片较小，适合通过 115200 串口传输，也不容易超过主控 64KB 限制。
  config.frame_size = FRAMESIZE_QVGA;
  config.pixel_format = PIXFORMAT_JPEG;  // for streaming
  //config.pixel_format = PIXFORMAT_RGB565; // for face detection/recognition
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
  config.jpeg_quality = 15;
  config.fb_count = 1;

#if defined(CAMERA_MODEL_ESP_EYE)
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif

  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    // 初始化失败多半是供电、排线、板型宏或 PWDN/RESET 引脚问题。
    cameraReady = false;
    cameraInitError = (uint32_t)err;
    camLog("ERROR", "INIT_FAILED", String("err=0x") + String((uint32_t)err, HEX));
  } else {
    cameraReady = true;
    cameraInitError = 0;
    camLog("INFO", "READY", "sensor=OV2640 frame=QVGA jpeg_quality=15");
  }

  if (!cameraReady) {
    return;
  }

  sensor_t *s = esp_camera_sensor_get();
  // initial sensors are flipped vertically and colors are a bit saturated
  if (s->id.PID == OV2640_PID) {
    s->set_vflip(s, 1);        // flip it back
    s->set_brightness(s, 1);   // up the brightness just a bit
    s->set_saturation(s, -2);  // lower the saturation
  }
  // drop down frame size for higher initial frame rate
  if (config.pixel_format == PIXFORMAT_JPEG) {
    s->set_framesize(s, FRAMESIZE_QVGA);
  }

#if defined(CAMERA_MODEL_M5STACK_WIDE) || defined(CAMERA_MODEL_M5STACK_ESP32CAM)
  s->set_vflip(s, 1);
  s->set_hmirror(s, 1);
#endif

#if defined(CAMERA_MODEL_ESP32S3_EYE)
  s->set_vflip(s, 1);
#endif

  camLog("INFO", "SERIAL_CAPTURE_ONLY", "cmd=CAPTURE");
}

// ===========================
// Send JPEG image over serial according to protocol:
// 1. "OK\r\n"
// 2. 4-byte little-endian length
// 3. image data
// 4. "DONE\r\n"
// ===========================
void sendImageToSerial(camera_fb_t *fb) {
  // 回传协议必须和主控 camera_comm.cpp 完全一致：
  // OK 行 -> 4 字节长度 -> JPEG 数据 -> DONE 行。
  if (!fb) {
    fbFailCount++;
    Serial.println(RESP_ERROR);
    camLog("ERROR", "FB_GET_FAILED",
           String("fb_fail=") + fbFailCount + " capture_ms=" + lastCaptureMs + " free_heap=" + ESP.getFreeHeap());
    return;
  }
  captureCount++;
  lastFbLen = fb->len;
  bool hasSoi = fb->len >= 2 && fb->buf[0] == 0xFF && fb->buf[1] == 0xD8;
  bool hasEoi = fb->len >= 2 && fb->buf[fb->len - 2] == 0xFF && fb->buf[fb->len - 1] == 0xD9;
  uint32_t sendStart = millis();
  // Send OK
  Serial.println(RESP_OK);
  
  // Send image length (uint32_t, little-endian)
  uint32_t len = fb->len;
  size_t headerBytes = Serial.write((uint8_t*)&len, 4);
  
  // Send image data
  size_t jpegBytes = Serial.write(fb->buf, fb->len);
  
  // Send DONE
  Serial.println(RESP_DONE);
  Serial.flush();
  lastSendMs = millis() - sendStart;
  lastJpegWriteBytes = jpegBytes;
  camLog("INFO", "JPEG_SENT",
         String("bytes=") + len
         + " header_bytes=" + headerBytes
         + " jpeg_write_bytes=" + jpegBytes
         + " send_ms=" + lastSendMs
         + " capture_ms=" + lastCaptureMs
         + " soi=" + (hasSoi ? 1 : 0)
         + " eoi=" + (hasEoi ? 1 : 0));
  
  // Return frame buffer
  esp_camera_fb_return(fb);
}

void sendStatusToSerial() {
  // STATUS is a small text-only diagnostic response.  It is safe for the main
  // controller to forward and does not include any JPEG payload.
  Serial.print("STATUS ready=");
  Serial.print(cameraReady ? 1 : 0);
  Serial.print(" init_err=0x");
  Serial.print(cameraInitError, HEX);
  Serial.print(" captures=");
  Serial.print(captureCount);
  Serial.print(" fb_fail=");
  Serial.print(fbFailCount);
  Serial.print(" last_fb_len=");
  Serial.print(lastFbLen);
  Serial.print(" last_capture_ms=");
  Serial.print(lastCaptureMs);
  Serial.print(" last_send_ms=");
  Serial.print(lastSendMs);
  Serial.print(" last_jpeg_write_bytes=");
  Serial.print(lastJpegWriteBytes);
  Serial.print(" free_heap=");
  Serial.print(ESP.getFreeHeap());
  Serial.print(" psram=");
  Serial.println(psramFound() ? 1 : 0);
}

void loop() {
  // 主控发送 "CAPTURE\r\n"；这里读到换行后 trim()，得到 "CAPTURE"。
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd == CMD_CAPTURE) {
      if (!cameraReady) {
        Serial.println(RESP_ERROR);
        camLog("ERROR", "CAPTURE_REJECTED", "reason=camera_not_ready");
        return;
      }
      uint32_t captureStart = millis();
      camera_fb_t *fb = esp_camera_fb_get();
      lastCaptureMs = millis() - captureStart;
      sendImageToSerial(fb);
    } else if (cmd == CMD_STATUS) {
      sendStatusToSerial();
    }
  }
  
  // Keep serial command handling responsive.
  delay(10);
}
