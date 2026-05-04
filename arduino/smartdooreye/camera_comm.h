/*
  ESP32-CAM 串口通信头文件

  主控 XIAO ESP32C6 和 ESP32-CAM 之间用 TTL UART 通信：
    XIAO TX(GPIO16) -> CAM RX(GPIO3)
    XIAO RX(GPIO17) <- CAM TX(GPIO1)
    两块板必须共地。

  协议设计成“先发长度，再发 JPEG 二进制”，这样主控知道要读多少字节，
  不会把图片中的任意二进制值误当成结束符。
*/

#ifndef CAMERA_COMM_H
#define CAMERA_COMM_H

#include "config.h"
#include "face_recognition.h"

#define CMD_CAPTURE     "CAPTURE\r\n"
#define RESP_OK         "OK\r\n"
#define RESP_READY      "READY\r\n"
#define RESP_ERROR      "ERROR\r\n"

bool takePhoto(byte** imageBuffer, size_t* imageSize);
FaceProcessResult takePhotoAndProcess(int photoIndex, bool allowNewRegistration = true);
bool sendCommand(const char* cmd, const char* expectedResponse, unsigned long timeout);
bool receiveImageData(byte** buffer, size_t* size, unsigned long timeout);
void pollCameraLogs();
void forwardCameraLogsFor(unsigned long durationMs);
bool requestCameraStatus(unsigned long timeout);

#endif
