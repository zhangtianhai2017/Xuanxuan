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
#include "face_recognition.h"

static const size_t CAM_LOG_MAX_LINE = 220;
static bool cameraBinaryTransferActive = false;
static String cameraLogLine = "";
static bool cameraLogLineTruncated = false;
static size_t cameraSkippedBinaryBytes = 0;

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

    // 图片完整收到后再等 DONE。为了提高容错性，如果 DONE 丢失但图片完整，
    // 当前实现仍接受图片，方便学生先验证主链路。
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
        delay(2);
    }

    cameraBinaryTransferActive = false;
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
    flushSerial1();

    // 拍照失败自动重试一次。硬件调试阶段，偶发失败常来自供电波动或串口干扰。
    for (int retry = 0; retry < 2; retry++) {
        if (retry > 0) {
            logWarn("CAM", "CAPTURE_RETRY", String("retry=") + retry);
            resetSerial1();
            delay(200);
        }

        logInfo("CAM", "CAPTURE_START", String("retry=") + retry);
        unsigned long attemptStart = millis();
        if (sendCommand(CMD_CAPTURE, RESP_OK, 5000)) {
            if (receiveImageData(imageBuffer, imageSize, 15000)) {
                logInfo("CAM", "CAPTURE_OK", String("bytes=") + *imageSize + " attempt_ms=" + (millis() - attemptStart));
                return true;
            }
        }

        flushSerial1();
        delay(300);
    }

    logError("CAM", "CAPTURE_FAILED", "retries=2");
    requestCameraStatus(600);
    return false;
}

void takePhotoAndProcess(int photoIndex) {
    // 这是“拍照并上传识别”的一站式函数：
    // 门铃和 PIR 都调用它，保证两类照片走同一套百度识别流程。
    byte* imageBuffer = nullptr;
    size_t imageSize = 0;

    if (takePhoto(&imageBuffer, &imageSize)) {
        logInfo("PHOTO", "CAPTURED", String("index=") + photoIndex + " bytes=" + imageSize);

        String base64Image = base64::encode(imageBuffer, imageSize);
        delete[] imageBuffer;

        if (base64Image.length() > 0) {
            logInfo("PHOTO", "BASE64_OK", String("index=") + photoIndex + " chars=" + base64Image.length());
            handleFaceRecognition(base64Image);
        } else {
            logError("PHOTO", "BASE64_FAILED", String("index=") + photoIndex);
        }
    } else {
        logError("PHOTO", "CAPTURE_FAILED", String("index=") + photoIndex);
    }
}
