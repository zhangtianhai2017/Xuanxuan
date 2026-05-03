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

enum FieldTestPrompt {
    FIELD_PROMPT_TEST_START = 0,
    FIELD_PROMPT_SPEAKER_CHECK,
    FIELD_PROMPT_PRESS_DOORBELL,
    FIELD_PROMPT_DOORBELL_NOT_DETECTED,
    FIELD_PROMPT_DOORBELL_DETECTED,
    FIELD_PROMPT_PIR_PROMPT,
    FIELD_PROMPT_PIR_NOT_DETECTED,
    FIELD_PROMPT_PIR_DETECTED,
    FIELD_PROMPT_PIR_HOLD_PASSED,
    FIELD_PROMPT_CAMERA_PROMPT,
    FIELD_PROMPT_CAMERA_NOT_DETECTED,
    FIELD_PROMPT_CAMERA_DETECTED,
    FIELD_PROMPT_FACE_PROMPT,
    FIELD_PROMPT_FACE_OK,
    FIELD_PROMPT_TEST_COMPLETE,
    FIELD_PROMPT_COUNT
};

void audioLoop();
void playAudioFromUrl(const char* url);
void playRandomQuarrel();
void playRandomQuarrelSequence(int trackCount);
void playDoorbell();
void playButtonTestPrompt();
void playFieldTestPrompt(FieldTestPrompt prompt);
void clearFieldTestPrompts();
bool isAudioBusy();
void stopAudio();

#endif
