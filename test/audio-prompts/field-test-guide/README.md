# 智能猫眼现场语音测试引导

这些 MP3 文件用于远程调试时的现场语音提示。它们的目标不是替代串口日志，而是让现场同学不用一直看电脑，也能按语音一步一步完成硬件测试。

## 使用方式

主控 XIAO ESP32C6 通过 WiFi 下载 MP3 文件，在本机用 `MP3DecoderHelix` 解码成 PCM 音频，再通过 I2S 输出给 reSpeaker XVF3800 板。也就是说，MP3 解码由 XIAO 完成，XVF3800/reSpeaker 音频板负责接收 I2S 音频并输出声音。

## 测试流程脚本

| 步骤 | 播放文件 | 触发时机 | 现场动作 | 通过日志 |
| --- | --- | --- | --- | --- |
| 0 | `00_test_start.mp3` | 进入现场测试模式 | 接好喇叭，保持远程调试窗口打开 | `FIELD_TEST START` |
| 1 | `01_speaker_check.mp3` | 音频自检开始 | 听到声音后告诉远程调试人员 | `AUDIO PLAY_DONE` |
| 2 | `02_press_doorbell.mp3` | 等待门铃按键 | 按住门铃约 0.5 秒后松开 | 等待下一步判断 |
| 3A | `03_doorbell_not_detected.mp3` | 超时未检测到门铃 | 检查 D10/GPIO18 到按键、按键到 GND | `BUTTON TEST_PROMPT_WAITING` 但没有 `DOORBELL_PRESSED` |
| 3B | `04_doorbell_detected.mp3` | 检测到门铃 | 继续下一项 | `BUTTON DOORBELL_PRESSED` 或 `BUTTON TEST_PASSED` |
| 4 | `05_pir_prompt.mp3` | 开始 PIR 测试 | 站到 PIR 前方并保持 30 秒 | 等待 `PIR MOTION_START` |
| 5A | `06_pir_not_detected.mp3` | 超时未检测到 PIR | 检查 PIR VCC、GND、OUT 到 D9/GPIO20 | 没有 `PIR MOTION_START` |
| 5B | `07_pir_detected.mp3` | PIR 检测到人体 | 继续保持不动 | `PIR MOTION_START` |
| 6 | `08_pir_hold_passed.mp3` | PIR 逗留 30 秒通过 | 等待连续抓拍和威慑音流程 | `PIR HOLD_REACHED` |
| 7 | `09_camera_prompt.mp3` | 开始 CAM 拍照测试 | 不操作，等待系统发送拍照指令 | `CAM CAPTURE_START` |
| 8A | `10_camera_not_detected.mp3` | CAM 拍照失败 | 检查 CAM 供电、共地、TX/RX、GPIO0 状态 | `CAM COMMAND_TIMEOUT` 或 `PHOTO CAPTURE_FAILED` |
| 8B | `11_camera_detected.mp3` | 收到 CAM 照片 | 继续云端识别测试 | `IMAGE_RX_OK` 或 `PHOTO CAPTURE_OK` |
| 9 | `12_face_prompt.mp3` | 开始百度云识别 | 保持 WiFi 可用 | `FACE SEARCH_START` |
| 10 | `13_face_ok.mp3` | 人脸接口返回结果 | 测试进入收尾 | `FACE SEARCH_MATCHED`、`SEARCH_NO_MATCH` 或接口错误已记录 |
| 11 | `14_test_complete.mp3` | 测试结束 | 保持远程调试窗口打开，等待日志上传 | `FIELD_TEST COMPLETE` |

## 原始文本

- `00_test_start`: 智能猫眼现场测试开始。请先接好喇叭，并保持电脑上的远程调试窗口不要关闭。
- `01_speaker_check`: 正在测试喇叭。如果你听到了这句话，请告诉远程调试人员，喇叭有声音。
- `02_press_doorbell`: 请按门铃，按住半秒。
- `03_doorbell_not_detected`: 没检测到门铃。检查 D 十到按钮，按钮到 G N D。
- `04_doorbell_detected`: 已经检测到门铃按键。门铃输入测试通过。
- `05_pir_prompt`: 现在测试人体移动传感器。请站到 P I R 传感器前方，并保持三十秒。
- `06_pir_not_detected`: 没检测到 P I R。检查 V C C，G N D，输出线到 D 九。
- `07_pir_detected`: 已经检测到 P I R 人体移动信号。请继续保持，等待三十秒逗留测试。
- `08_pir_hold_passed`: 三十秒逗留测试通过。系统将执行连续抓拍和威慑声音流程。
- `09_camera_prompt`: 现在测试 E S P 三二摄像头。系统会向摄像头发送拍照指令。
- `10_camera_not_detected`: 没收到摄像头。检查供电，共地，T X，R X，和 GPIO 零。
- `11_camera_detected`: 已经收到摄像头照片。摄像头串口通信测试通过。
- `12_face_prompt`: 现在测试百度云人脸识别。请保持网络连接，并等待识别结果。
- `13_face_ok`: 人脸识别接口已经返回结果。云端识别流程测试通过。
- `14_test_complete`: 现场测试流程结束。请把远程调试窗口保持打开，让日志自动上传。

## GitHub Raw URL 规则

程序中可按下面格式拼接 URL：

```text
https://raw.githubusercontent.com/zhangtianhai2017/Xuanxuan/main/test/audio-prompts/field-test-guide/<file-name>.mp3
```
