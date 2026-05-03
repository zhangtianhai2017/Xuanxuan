/*
  音频模块头文件

  reSpeaker XVF3800 在本项目中承担“发声”角色：
    - 门铃按键触发时播放门铃提示音。
    - PIR 逗留 30 秒触发时连续播放 3 首吵架声音。

  这里仅声明函数，具体 I2S 引脚、网络 MP3 播放和三首音频队列逻辑在 audio_task.cpp。
*/

#ifndef AUDIO_TASK_H
#define AUDIO_TASK_H

#include "config.h"

void audioLoop();
void playAudioFromUrl(const char* url);
void playRandomQuarrel();
void playRandomQuarrelSequence(int trackCount);
void playDoorbell();
void playButtonTestPrompt();
bool isAudioBusy();
void stopAudio();

#endif
