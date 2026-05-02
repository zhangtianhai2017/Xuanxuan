/*
  ESP32-CAM 板型选择

  初学者常见错误：代码选择的摄像头板型和手上的硬件不一致。
  板型不同，OV2640 摄像头的 D0-D7、XCLK、PCLK、VSYNC 等引脚可能完全不同。
  如果这里选错，典型现象是：
    - Camera init failed
    - esp_camera_fb_get() 返回空
    - 串口一直回 ERROR

  当前硬件按 AI Thinker ESP32-CAM 处理。
*/

#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

//#define CAMERA_MODEL_WROVER_KIT
//#define CAMERA_MODEL_ESP_EYE
//#define CAMERA_MODEL_ESP32S3_EYE
//#define CAMERA_MODEL_M5STACK_PSRAM
//#define CAMERA_MODEL_M5STACK_V2_PSRAM
//#define CAMERA_MODEL_M5STACK_WIDE
//#define CAMERA_MODEL_M5STACK_ESP32CAM
//#define CAMERA_MODEL_M5STACK_UNITCAM
//#define CAMERA_MODEL_M5STACK_CAMS3_UNIT
#define CAMERA_MODEL_AI_THINKER
//#define CAMERA_MODEL_TTGO_T_JOURNAL
//#define CAMERA_MODEL_XIAO_ESP32S3
//#define CAMERA_MODEL_ESP32_CAM_BOARD
//#define CAMERA_MODEL_ESP32S2_CAM_BOARD
//#define CAMERA_MODEL_ESP32S3_CAM_LCD
//#define CAMERA_MODEL_DFRobot_FireBeetle2_ESP32S3
//#define CAMERA_MODEL_DFRobot_Romeo_ESP32S3

// camera_pins.h 会根据上面的 CAMERA_MODEL_xxx 宏展开具体 GPIO 定义。
#include "camera_pins.h"

#endif
