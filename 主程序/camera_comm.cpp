/*
  ESP32-CAM 串口通信实现

  本文件运行在主控 XIAO ESP32C6 上，负责和 ESP32-CAM 说话。
  对初学者来说，这里最重要的是理解“文本命令 + 二进制数据”的混合协议：
    1. 主控发送文本命令 "CAPTURE\r\n"。
    2. CAM 返回文本 "OK\r\n" 表示准备发送图片。
    3. CAM 发送 4 字节图片长度，小端格式。
    4. CAM 发送 JPEG 二进制图片。
    5. CAM 发送文本 "DONE\r\n" 表示结束。

  为什么要先发长度？
  JPEG 图片里可能包含任意字节，不能靠某个特殊字符判断图片结束。
  先发长度后，主控精确读取指定字节数，更适合教学和调试。
*/

#include "camera_comm.h"
#include "debug_log.h"
#include "audio_task.h"
#include "face_recognition.h"
#include <string.h>

static const size_t CAM_LOG_MAX_LINE = 220;
static bool cameraBinaryTransferActive = false;
static String cameraLogLine = "";
static bool cameraLogLineTruncated = false;
static size_t cameraSkippedBinaryBytes = 0;
static unsigned int cameraConsecutiveFailures = 0;
static unsigned long cameraBackoffUntil = 0;
static bool lastPhotoUsedRemoteTestImage = false;

// These JPEGs are stored in this project Git repository under
// test/face-test-images/.  They are deliberately small public-domain portraits
// so their byte size is close to an ESP32-CAM QVGA JPEG, but they still contain
// real faces for testing the Baidu API path while the camera hardware is down.
static const char* REMOTE_TEST_IMAGE_URLS[] = {
    "https://raw.githubusercontent.com/zhangtianhai2017/Xuanxuan/main/test/face-test-images/cornelius_250.jpg",
    "https://raw.githubusercontent.com/zhangtianhai2017/Xuanxuan/main/test/face-test-images/cleveland_220.jpg",
    "https://raw.githubusercontent.com/zhangtianhai2017/Xuanxuan/main/test/face-test-images/john_dewey_220.jpg",
    "https://raw.githubusercontent.com/zhangtianhai2017/Xuanxuan/main/test/face-test-images/small_girl_watts_220.jpg"
};
static const size_t REMOTE_TEST_IMAGE_COUNT = sizeof(REMOTE_TEST_IMAGE_URLS) / sizeof(REMOTE_TEST_IMAGE_URLS[0]);
static size_t nextRemoteTestImageIndex = 0;

// A tiny valid JPEG used only when CAMERA_MOCK_IMAGE_ON_FAILURE is enabled.
// It is intentionally a plain gray image, not a visitor face.  The purpose is
// to test "photo bytes -> Base64 -> Baidu HTTP request -> no-face handling"
// while the real ESP32-CAM hardware is being repaired.
static const byte MOCK_JPEG_IMAGE[] = {
  0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46, 0x49, 0x46, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01,
  0x00, 0x01, 0x00, 0x00, 0xFF, 0xDB, 0x00, 0x43, 0x00, 0x0A, 0x07, 0x07, 0x08, 0x07, 0x06, 0x0A,
  0x08, 0x08, 0x08, 0x0B, 0x0A, 0x0A, 0x0B, 0x0E, 0x18, 0x10, 0x0E, 0x0D, 0x0D, 0x0E, 0x1D, 0x15,
  0x16, 0x11, 0x18, 0x23, 0x1F, 0x25, 0x24, 0x22, 0x1F, 0x22, 0x21, 0x26, 0x2B, 0x37, 0x2F, 0x26,
  0x29, 0x34, 0x29, 0x21, 0x22, 0x30, 0x41, 0x31, 0x34, 0x39, 0x3B, 0x3E, 0x3E, 0x3E, 0x25, 0x2E,
  0x44, 0x49, 0x43, 0x3C, 0x48, 0x37, 0x3D, 0x3E, 0x3B, 0xFF, 0xDB, 0x00, 0x43, 0x01, 0x0A, 0x0B,
  0x0B, 0x0E, 0x0D, 0x0E, 0x1C, 0x10, 0x10, 0x1C, 0x3B, 0x28, 0x22, 0x28, 0x3B, 0x3B, 0x3B, 0x3B,
  0x3B, 0x3B, 0x3B, 0x3B, 0x3B, 0x3B, 0x3B, 0x3B, 0x3B, 0x3B, 0x3B, 0x3B, 0x3B, 0x3B, 0x3B, 0x3B,
  0x3B, 0x3B, 0x3B, 0x3B, 0x3B, 0x3B, 0x3B, 0x3B, 0x3B, 0x3B, 0x3B, 0x3B, 0x3B, 0x3B, 0x3B, 0x3B,
  0x3B, 0x3B, 0x3B, 0x3B, 0x3B, 0x3B, 0x3B, 0x3B, 0x3B, 0x3B, 0x3B, 0x3B, 0x3B, 0x3B, 0xFF, 0xC0,
  0x00, 0x11, 0x08, 0x00, 0x10, 0x00, 0x10, 0x03, 0x01, 0x22, 0x00, 0x02, 0x11, 0x01, 0x03, 0x11,
  0x01, 0xFF, 0xC4, 0x00, 0x1F, 0x00, 0x00, 0x01, 0x05, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
  0x0A, 0x0B, 0xFF, 0xC4, 0x00, 0xB5, 0x10, 0x00, 0x02, 0x01, 0x03, 0x03, 0x02, 0x04, 0x03, 0x05,
  0x05, 0x04, 0x04, 0x00, 0x00, 0x01, 0x7D, 0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21,
  0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xA1, 0x08, 0x23,
  0x42, 0xB1, 0xC1, 0x15, 0x52, 0xD1, 0xF0, 0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0A, 0x16, 0x17,
  0x18, 0x19, 0x1A, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A,
  0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A,
  0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A,
  0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99,
  0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7,
  0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4, 0xD5,
  0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xF1,
  0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA, 0xFF, 0xC4, 0x00, 0x1F, 0x01, 0x00, 0x03,
  0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
  0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0xFF, 0xC4, 0x00, 0xB5, 0x11, 0x00,
  0x02, 0x01, 0x02, 0x04, 0x04, 0x03, 0x04, 0x07, 0x05, 0x04, 0x04, 0x00, 0x01, 0x02, 0x77, 0x00,
  0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13,
  0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xA1, 0xB1, 0xC1, 0x09, 0x23, 0x33, 0x52, 0xF0, 0x15,
  0x62, 0x72, 0xD1, 0x0A, 0x16, 0x24, 0x34, 0xE1, 0x25, 0xF1, 0x17, 0x18, 0x19, 0x1A, 0x26, 0x27,
  0x28, 0x29, 0x2A, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
  0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
  0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88,
  0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6,
  0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4,
  0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE2,
  0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9,
  0xFA, 0xFF, 0xDA, 0x00, 0x0C, 0x03, 0x01, 0x00, 0x02, 0x11, 0x03, 0x11, 0x00, 0x3F, 0x00, 0xDA,
  0xA2, 0x8A, 0x28, 0x03, 0xFF, 0xD9
};

static void serviceBackgroundWhileWaiting() {
    // 拍照串口等待可能持续几秒。这里顺手推进音频循环，
    // 这样门铃音或 PIR 吵架音不会因为 CAM 故障而完全停住。
    audioLoop();
}

static bool cameraBackoffActive() {
    if (cameraBackoffUntil == 0) {
        return false;
    }

    unsigned long now = millis();
    if (now < cameraBackoffUntil) {
        return true;
    }

    logInfo("CAM", "BACKOFF_END", String("failures=") + cameraConsecutiveFailures);
    cameraBackoffUntil = 0;
    return false;
}

static unsigned long cameraBackoffRemainingMs() {
    if (!cameraBackoffActive()) {
        return 0;
    }
    return cameraBackoffUntil - millis();
}

static void noteCameraCaptureSuccess() {
    if (cameraConsecutiveFailures > 0 || cameraBackoffUntil > 0) {
        logInfo("CAM", "FAILURE_COUNTER_RESET", String("previous_failures=") + cameraConsecutiveFailures);
    }
    cameraConsecutiveFailures = 0;
    cameraBackoffUntil = 0;
}

static void noteCameraCaptureFailure() {
    cameraConsecutiveFailures++;
    logWarn("CAM", "FAILURE_COUNT", String("consecutive=") + cameraConsecutiveFailures);

    if (cameraConsecutiveFailures >= CAMERA_FAILURE_BACKOFF_THRESHOLD) {
        cameraBackoffUntil = millis() + CAMERA_FAILURE_BACKOFF_MS;
        logWarn("CAM", "BACKOFF_START",
                String("duration_ms=") + CAMERA_FAILURE_BACKOFF_MS
                + " consecutive_failures=" + cameraConsecutiveFailures);
    }
}

static bool hasJpegMarkers(const byte* data, size_t size) {
    return size >= 2
        && data[0] == 0xFF
        && data[1] == 0xD8
        && data[size - 2] == 0xFF
        && data[size - 1] == 0xD9;
}

static bool downloadRemoteTestImage(byte** imageBuffer, size_t* imageSize, const char* reason) {
    if (!CAMERA_REMOTE_TEST_IMAGE_ON_FAILURE) {
        return false;
    }

    if (WiFi.status() != WL_CONNECTED) {
        logWarn("CAM", "REMOTE_TEST_IMAGE_SKIP", "reason=wifi_offline fallback=gray_jpeg");
        return false;
    }

    const char* url = REMOTE_TEST_IMAGE_URLS[nextRemoteTestImageIndex];
    nextRemoteTestImageIndex = (nextRemoteTestImageIndex + 1) % REMOTE_TEST_IMAGE_COUNT;

    HTTPClient http;
    http.setTimeout(CAMERA_REMOTE_TEST_IMAGE_TIMEOUT_MS);
    logWarn("CAM", "REMOTE_TEST_IMAGE_FETCH",
            String("reason=") + reason
            + " timeout_ms=" + CAMERA_REMOTE_TEST_IMAGE_TIMEOUT_MS
            + " url=" + url);

    if (!http.begin(url)) {
        logError("CAM", "REMOTE_TEST_IMAGE_BEGIN_FAILED", String("url=") + url);
        return false;
    }

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        logError("CAM", "REMOTE_TEST_IMAGE_HTTP_FAILED",
                 String("http=") + httpCode + " url=" + url);
        http.end();
        return false;
    }

    int contentLength = http.getSize();
    int maxBytes = min(CAMERA_REMOTE_TEST_IMAGE_MAX_BYTES, MAX_IMAGE_SIZE);
    if (contentLength <= 0 || contentLength > maxBytes) {
        logError("CAM", "REMOTE_TEST_IMAGE_SIZE_INVALID",
                 String("bytes=") + contentLength
                 + " max=" + maxBytes
                 + " url=" + url);
        http.end();
        return false;
    }

    byte* downloaded = new byte[contentLength];
    if (downloaded == nullptr) {
        logError("CAM", "REMOTE_TEST_IMAGE_ALLOC_FAILED",
                 String("bytes=") + contentLength + " free_heap=" + ESP.getFreeHeap());
        http.end();
        return false;
    }

    NetworkClient* stream = http.getStreamPtr();
    size_t received = 0;
    unsigned long startMs = millis();
    unsigned long lastDataMs = startMs;
    while (received < (size_t)contentLength) {
        int availableBytes = stream->available();
        if (availableBytes > 0) {
            size_t remaining = (size_t)contentLength - received;
            size_t toRead = min((size_t)availableBytes, remaining);
            size_t readBytes = stream->readBytes(downloaded + received, toRead);
            if (readBytes > 0) {
                received += readBytes;
                lastDataMs = millis();
            }
        }

        if (millis() - lastDataMs > CAMERA_REMOTE_TEST_IMAGE_TIMEOUT_MS) {
            logError("CAM", "REMOTE_TEST_IMAGE_RX_TIMEOUT",
                     String("received=") + received
                     + " expected=" + contentLength
                     + " elapsed_ms=" + (millis() - startMs));
            delete[] downloaded;
            http.end();
            return false;
        }

        serviceBackgroundWhileWaiting();
        delay(1);
    }

    http.end();

    if (!hasJpegMarkers(downloaded, received)) {
        logError("CAM", "REMOTE_TEST_IMAGE_MARKER_INVALID",
                 String("bytes=") + received + " url=" + url);
        delete[] downloaded;
        return false;
    }

    *imageBuffer = downloaded;
    *imageSize = received;
    lastPhotoUsedRemoteTestImage = true;
    logWarn("CAM", "REMOTE_TEST_IMAGE_USED",
            String("reason=") + reason
            + " bytes=" + received
            + " elapsed_ms=" + (millis() - startMs)
            + " url=" + url);
    return true;
}

static bool provideMockImage(byte** imageBuffer, size_t* imageSize, const char* reason) {
    if (!CAMERA_MOCK_IMAGE_ON_FAILURE) {
        return false;
    }

    if (downloadRemoteTestImage(imageBuffer, imageSize, reason)) {
        return true;
    }

    *imageSize = sizeof(MOCK_JPEG_IMAGE);
    *imageBuffer = new byte[*imageSize];
    if (*imageBuffer == nullptr) {
        *imageSize = 0;
        logError("CAM", "MOCK_IMAGE_ALLOC_FAILED", String("bytes=") + sizeof(MOCK_JPEG_IMAGE));
        return false;
    }

    memcpy(*imageBuffer, MOCK_JPEG_IMAGE, *imageSize);
    lastPhotoUsedRemoteTestImage = true;
    logWarn("CAM", "MOCK_IMAGE_USED",
            String("reason=") + reason
            + " bytes=" + *imageSize
            + " note=plain_gray_jpeg_no_real_visitor");
    return true;
}

static String stripLineEnding(const String& rawLine) {
    String line = rawLine;
    while (line.endsWith("\n") || line.endsWith("\r")) {
        line.remove(line.length() - 1);
    }
    return line;
}

static String commandLabel(const char* cmd) {
    return stripLineEnding(String(cmd));
}

static String hexByte(uint8_t value) {
    const char* digits = "0123456789ABCDEF";
    String out = "";
    out += digits[(value >> 4) & 0x0F];
    out += digits[value & 0x0F];
    return out;
}

static void forwardCameraRawLine(const String& rawLine) {
    String line = stripLineEnding(rawLine);
    if (line.length() == 0) {
        return;
    }

    // The original CAM text is kept after the prefix.  This makes remote logs
    // searchable by the main-controller timestamp while preserving CAM output.
    logInfo("CAM_RAW", "LINE", line);
}

static void reportSkippedCameraBinaryIfNeeded() {
    if (cameraSkippedBinaryBytes > 0) {
        logWarn("CAM_RAW", "BINARY_SKIPPED", String("bytes=") + cameraSkippedBinaryBytes);
        cameraSkippedBinaryBytes = 0;
    }
}

static void consumeCameraTextByte(char c) {
    if (c == '\n') {
        reportSkippedCameraBinaryIfNeeded();
        if (cameraLogLineTruncated) {
            logWarn("CAM_RAW", "LINE_TRUNCATED", String("max_chars=") + CAM_LOG_MAX_LINE);
        }
        forwardCameraRawLine(cameraLogLine);
        cameraLogLine = "";
        cameraLogLineTruncated = false;
        return;
    }

    if (c == '\r') {
        return;
    }

    // JPEG payload and electrical noise are not printable text.  During idle
    // forwarding we summarize such bytes instead of dumping them to USB Serial.
    bool printableAscii = (c >= 32 && c <= 126) || c == '\t';
    if (!printableAscii) {
        cameraSkippedBinaryBytes++;
        return;
    }

    if (cameraLogLine.length() < CAM_LOG_MAX_LINE) {
        cameraLogLine += c;
    } else {
        cameraLogLineTruncated = true;
    }
}

void pollCameraLogs() {
    if (cameraBinaryTransferActive) {
        return;
    }

    while (Serial1.available()) {
        consumeCameraTextByte((char)Serial1.read());
    }
}

void forwardCameraLogsFor(unsigned long durationMs) {
    unsigned long startTime = millis();
    while (millis() - startTime < durationMs) {
        pollCameraLogs();
        serviceBackgroundWhileWaiting();
        delay(2);
    }
}

static void flushSerial1() {
    // 清空串口残留数据。串口协议最怕“上一轮没读完的数据”混进下一轮。
    // 现在清理时会优先把 CAM 的文本日志转发到主控 USB 串口；
    // 只有不可打印的疑似二进制数据会被统计后丢弃，避免刷屏或影响性能。
    pollCameraLogs();
    delay(10);
    pollCameraLogs();
}

static void resetSerial1() {
    // 捕获失败后重启 Serial1，相当于给通信状态一个软复位。
    // 这不会复位 CAM，只是清理主控侧 UART 外设状态。
    Serial1.end();
    delay(50);
    Serial1.begin(CAMERA_BAUD_RATE, SERIAL_8N1, PIN_UART_RX, PIN_UART_TX);
    delay(50);
    flushSerial1();
}

bool sendCommand(const char* cmd, const char* expectedResponse, unsigned long timeout) {
    flushSerial1();

    String cmdLabel = commandLabel(cmd);
    logInfo("CAM", "COMMAND_SEND", String("cmd=") + cmdLabel);
    Serial1.print(cmd);
    Serial1.flush();

    unsigned long startTime = millis();
    String response = "";

    while (millis() - startTime < timeout) {
        while (Serial1.available()) {
            char c = Serial1.read();
            response += c;
            if (response.endsWith("\n")) {
                // CAM 的 OK/ERROR 是文本行，读到换行后再判断整行内容。
                String line = stripLineEnding(response);
                if (expectedResponse == nullptr || line == stripLineEnding(String(expectedResponse))) {
                    logInfo("CAM", "COMMAND_RESPONSE",
                            String("cmd=") + cmdLabel + " line=" + line + " elapsed_ms=" + (millis() - startTime));
                    return true;
                }
                if (line == "ERROR") {
                    logError("CAM", "COMMAND_RESPONSE_ERROR", String("cmd=") + cmdLabel);
                    return false;
                }
                forwardCameraRawLine(response);
                response = "";
            }
            if (response.length() > CAM_LOG_MAX_LINE) {
                forwardCameraRawLine(response);
                response = "";
            }
        }
        serviceBackgroundWhileWaiting();
        delay(2);
    }

    logError("CAM", "COMMAND_TIMEOUT", String("cmd=") + cmdLabel + " timeout_ms=" + timeout);
    flushSerial1();
    return false;
}

bool receiveImageData(byte** buffer, size_t* size, unsigned long timeout) {
    unsigned long lengthWaitStart = millis();
    cameraBinaryTransferActive = true;

    // 先等 4 字节长度。只有长度合法，才分配内存接收 JPEG。
    while (Serial1.available() < 4) {
        if (millis() - lengthWaitStart > timeout) {
            cameraBinaryTransferActive = false;
            logError("CAM", "IMAGE_LEN_TIMEOUT",
                     String("wait_ms=") + (millis() - lengthWaitStart) + " timeout_ms=" + timeout
                     + " available=" + Serial1.available());
            flushSerial1();
            return false;
        }
        serviceBackgroundWhileWaiting();
        delay(2);
    }

    uint8_t lenBytes[4];
    lenBytes[0] = (uint8_t)Serial1.read();
    lenBytes[1] = (uint8_t)Serial1.read();
    lenBytes[2] = (uint8_t)Serial1.read();
    lenBytes[3] = (uint8_t)Serial1.read();
    uint32_t len = 0;
    len |= (uint32_t)lenBytes[0];
    len |= (uint32_t)lenBytes[1] << 8;
    len |= (uint32_t)lenBytes[2] << 16;
    len |= (uint32_t)lenBytes[3] << 24;
    String rawLen = hexByte(lenBytes[0]) + " " + hexByte(lenBytes[1]) + " " + hexByte(lenBytes[2]) + " " + hexByte(lenBytes[3]);

    if (len == 0 || len > MAX_IMAGE_SIZE) {
        // 如果这里报错，常见原因是：
        // 1. CAM 串口混入了调试日志。
        // 2. 图片太大超过 MAX_IMAGE_SIZE。
        // 3. TX/RX 接线或供电不稳定导致数据损坏。
        cameraBinaryTransferActive = false;
        logError("CAM", "INVALID_IMAGE_LEN",
                 String("bytes=") + len + " max=" + MAX_IMAGE_SIZE + " raw_len_hex=\"" + rawLen + "\"");
        flushSerial1();
        return false;
    }

    *size = len;
    size_t heapBeforeAlloc = ESP.getFreeHeap();
    *buffer = new byte[len];
    if (*buffer == nullptr) {
        // ESP32-C6 内存有限，分配失败时必须停止本轮接收，避免空指针崩溃。
        cameraBinaryTransferActive = false;
        logError("CAM", "IMAGE_ALLOC_FAILED", String("bytes=") + len + " free_heap=" + heapBeforeAlloc);
        *size = 0;
        flushSerial1();
        return false;
    }

    logInfo("CAM", "IMAGE_LEN",
            String("bytes=") + len + " wait_ms=" + (millis() - lengthWaitStart)
            + " raw_len_hex=\"" + rawLen + "\" free_heap_before_alloc=" + heapBeforeAlloc
            + " free_heap_after_alloc=" + ESP.getFreeHeap());

    size_t received = 0;
    unsigned long rxStart = millis();
    unsigned long lastDataTime = rxStart;
    unsigned long firstByteWaitMs = 0;
    unsigned long maxSilentGapMs = 0;
    bool sawFirstByte = false;

    while (received < len) {
        // 只要不断收到字节，就刷新 startTime；超时表示 CAM 中断或线不稳。
        if (Serial1.available()) {
            unsigned long now = millis();
            if (!sawFirstByte) {
                sawFirstByte = true;
                firstByteWaitMs = now - rxStart;
            }
            unsigned long gapMs = now - lastDataTime;
            if (gapMs > maxSilentGapMs) {
                maxSilentGapMs = gapMs;
            }
            (*buffer)[received++] = Serial1.read();
            lastDataTime = now;
        }
        if (millis() - lastDataTime > timeout) {
            cameraBinaryTransferActive = false;
            logError("CAM", "IMAGE_DATA_TIMEOUT",
                     String("received=") + received + " expected=" + len
                     + " elapsed_ms=" + (millis() - rxStart)
                     + " idle_ms=" + (millis() - lastDataTime)
                     + " max_gap_ms=" + maxSilentGapMs
                     + " timeout_ms=" + timeout);
            delete[] *buffer;
            *buffer = nullptr;
            *size = 0;
            flushSerial1();
            return false;
        }
        serviceBackgroundWhileWaiting();
        delay(0);
    }

    unsigned long rxMs = millis() - rxStart;
    if (rxMs == 0) {
        rxMs = 1;
    }
    bool hasSoi = len >= 2 && (*buffer)[0] == 0xFF && (*buffer)[1] == 0xD8;
    bool hasEoi = len >= 2 && (*buffer)[len - 2] == 0xFF && (*buffer)[len - 1] == 0xD9;
    unsigned long bytesPerSecond = (unsigned long)((uint64_t)len * 1000ULL / rxMs);
    logInfo("CAM", "IMAGE_RX_OK",
            String("bytes=") + len + " rx_ms=" + rxMs
            + " first_byte_wait_ms=" + firstByteWaitMs
            + " max_gap_ms=" + maxSilentGapMs
            + " avg_Bps=" + bytesPerSecond
            + " soi=" + (hasSoi ? 1 : 0)
            + " eoi=" + (hasEoi ? 1 : 0));

    if (CAMERA_REQUIRE_JPEG_MARKERS && (!hasSoi || !hasEoi)) {
        // SOI/EOI 缺失说明收到的不是完整 JPEG。远程调试时这类数据最容易误导判断，
        // 所以直接失败，不继续 Base64，也不上传百度。
        cameraBinaryTransferActive = false;
        logError("CAM", "IMAGE_MARKER_INVALID",
                 String("soi=") + (hasSoi ? 1 : 0) + " eoi=" + (hasEoi ? 1 : 0)
                 + " bytes=" + len + " action=reject");
        delete[] *buffer;
        *buffer = nullptr;
        *size = 0;
        flushSerial1();
        return false;
    }

    // 图片完整收到后再等 DONE。DONE 是本项目自定义串口协议的结束线。
    // 如果 DONE 丢失，说明 CAM 或串口链路仍不稳定；调试阶段默认拒绝。
    String response = "";
    unsigned long doneWaitStart = millis();
    while (millis() - doneWaitStart < 2000) {
        if (Serial1.available()) {
            char c = Serial1.read();
            response += c;
            if (response.endsWith("DONE\r\n")) {
                cameraBinaryTransferActive = false;
                logInfo("CAM", "DONE_OK", String("wait_ms=") + (millis() - doneWaitStart));
                flushSerial1();
                return true;
            }
        }
        serviceBackgroundWhileWaiting();
        delay(2);
    }

    cameraBinaryTransferActive = false;
    if (CAMERA_REQUIRE_DONE) {
        logError("CAM", "DONE_MISSING",
                 String("wait_ms=") + (millis() - doneWaitStart) + " partial_response_chars=" + response.length()
                 + " action=reject_complete_image");
        delete[] *buffer;
        *buffer = nullptr;
        *size = 0;
        flushSerial1();
        return false;
    }

    logWarn("CAM", "DONE_MISSING",
            String("wait_ms=") + (millis() - doneWaitStart) + " partial_response_chars=" + response.length()
            + " action=accept_complete_image");
    flushSerial1();
    return true;
}

bool requestCameraStatus(unsigned long timeout) {
    flushSerial1();

    Serial1.print("STATUS\r\n");
    Serial1.flush();
    logInfo("CAM", "STATUS_REQUEST");

    unsigned long startTime = millis();
    bool gotStatus = false;
    String response = "";

    while (millis() - startTime < timeout) {
        while (Serial1.available()) {
            char c = Serial1.read();
            response += c;
            if (response.endsWith("\n")) {
                String line = stripLineEnding(response);
                if (line.startsWith("STATUS ")) {
                    logInfo("CAM_RAW", "LINE", line);
                    gotStatus = true;
                    response = "";
                    return true;
                }
                if (line == "ERROR") {
                    logWarn("CAM", "STATUS_ERROR_RESPONSE");
                    response = "";
                    return false;
                }
                forwardCameraRawLine(response);
                response = "";
            }
            if (response.length() > CAM_LOG_MAX_LINE) {
                forwardCameraRawLine(response);
                response = "";
            }
        }
        serviceBackgroundWhileWaiting();
        delay(2);
    }

    if (response.length() > 0) {
        forwardCameraRawLine(response);
    }

    if (!gotStatus) {
        logWarn("CAM", "STATUS_TIMEOUT", String("timeout_ms=") + timeout);
    }
    return gotStatus;
}

bool takePhoto(byte** imageBuffer, size_t* imageSize) {
    *imageBuffer = nullptr;
    *imageSize = 0;
    lastPhotoUsedRemoteTestImage = false;

    if (!CAMERA_CAPTURE_ENABLED) {
        logWarn("CAM", "CAPTURE_SKIPPED", "reason=disabled_by_config");
        return provideMockImage(imageBuffer, imageSize, "camera_disabled_by_config");
    }

    if (cameraBackoffActive()) {
        logWarn("CAM", "CAPTURE_SKIPPED",
                String("reason=backoff remaining_ms=") + cameraBackoffRemainingMs()
                + " consecutive_failures=" + cameraConsecutiveFailures);
        return provideMockImage(imageBuffer, imageSize, "camera_backoff");
    }

    flushSerial1();

    // 拍照失败自动重试一次。硬件调试阶段，偶发失败常来自供电波动或串口干扰。
    for (int retry = 0; retry < 2; retry++) {
        if (retry > 0) {
            logWarn("CAM", "CAPTURE_RETRY", String("retry=") + retry);
            resetSerial1();
            serviceBackgroundWhileWaiting();
            delay(200);
        }

        logInfo("CAM", "CAPTURE_START", String("retry=") + retry);
        unsigned long attemptStart = millis();
        if (sendCommand(CMD_CAPTURE, RESP_OK, 5000)) {
            if (receiveImageData(imageBuffer, imageSize, 15000)) {
                logInfo("CAM", "CAPTURE_OK", String("bytes=") + *imageSize + " attempt_ms=" + (millis() - attemptStart));
                lastPhotoUsedRemoteTestImage = false;
                noteCameraCaptureSuccess();
                return true;
            }
        }

        flushSerial1();
        serviceBackgroundWhileWaiting();
        delay(300);
    }

    logError("CAM", "CAPTURE_FAILED", "retries=2");
    requestCameraStatus(600);
    noteCameraCaptureFailure();
    return provideMockImage(imageBuffer, imageSize, "real_camera_capture_failed");
}

FaceProcessResult takePhotoAndProcess(int photoIndex, bool allowNewRegistration) {
    // 这是“拍照并上传识别”的一站式函数：
    // 门铃和 PIR 都调用它，保证两类照片走同一套百度识别流程。
    byte* imageBuffer = nullptr;
    size_t imageSize = 0;

    if (photoIndex == 0) {
        playFieldTestPrompt(FIELD_PROMPT_CAMERA_PROMPT);
    }

    if (takePhoto(&imageBuffer, &imageSize)) {
        logInfo("PHOTO", "CAPTURED", String("index=") + photoIndex + " bytes=" + imageSize);

        if (photoIndex == 0) {
            playFieldTestPrompt(lastPhotoUsedRemoteTestImage
                                ? FIELD_PROMPT_CAMERA_NOT_DETECTED
                                : FIELD_PROMPT_CAMERA_DETECTED);
        }

        String base64Image = base64::encode(imageBuffer, imageSize);
        delete[] imageBuffer;

        if (base64Image.length() > 0) {
            logInfo("PHOTO", "BASE64_OK", String("index=") + photoIndex + " chars=" + base64Image.length());
            bool allowRegistrationForThisImage = allowNewRegistration;
            if (lastPhotoUsedRemoteTestImage && !CAMERA_REMOTE_TEST_IMAGE_ALLOW_REGISTER) {
                allowRegistrationForThisImage = false;
                logWarn("FACE", "REGISTER_BLOCKED",
                        String("reason=remote_test_image index=") + photoIndex);
            }
            if (photoIndex == 0) {
                playFieldTestPrompt(FIELD_PROMPT_FACE_PROMPT);
            }
            FaceProcessResult result = handleFaceRecognition(base64Image, allowRegistrationForThisImage);
            if (photoIndex == 0 && result != FACE_PROCESS_SKIPPED) {
                playFieldTestPrompt(FIELD_PROMPT_FACE_OK);
            }
            return result;
        } else {
            logError("PHOTO", "BASE64_FAILED", String("index=") + photoIndex);
        }
    } else {
        logError("PHOTO", "CAPTURE_FAILED", String("index=") + photoIndex);
        if (photoIndex == 0) {
            playFieldTestPrompt(FIELD_PROMPT_CAMERA_NOT_DETECTED);
        }
    }
    return FACE_PROCESS_SKIPPED;
}
