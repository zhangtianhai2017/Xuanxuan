/*
  Blynk 远程控制头文件

  Blynk 用来给学生演示“手机 App 远程触发硬件”的方式。
  当前项目提供两个远程入口：
    - 远程拍照：等效于有人触发拍照流程。
    - 远程播放吵架音：用于测试音频输出。
*/

#ifndef BLYNK_HANDLERS_H
#define BLYNK_HANDLERS_H

#include "config.h"
#include <Arduino.h>

void blynkRun();
void blynkVirtualWrite(int pin, const String& value);
void blynkVirtualWrite(int pin, int value);
void setupBlynk();

#endif
