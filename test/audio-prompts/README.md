# Audio Prompts

This folder stores short voice prompts used by the remote debug firmware.

## Doorbell Button Test

- File: `please_press_doorbell_button_22k.wav`
- Text: `请配合测试，请按一下门铃按钮。`
- Format: WAV, 22.05 kHz, 16-bit, mono
- Raw URL: `https://raw.githubusercontent.com/zhangtianhai2017/Xuanxuan/main/test/audio-prompts/please_press_doorbell_button_22k.wav`

The main firmware plays this prompt while the remote button test is waiting for
the first valid doorbell press on XIAO ESP32C6 D10 / GPIO18. The prompt is a
debug aid only; it should not interrupt normal doorbell or PIR audio playback.

## Field Test Guide MP3 Prompts

The `field-test-guide/` folder contains smaller MP3 prompts for step-by-step
field testing. The XIAO ESP32C6 downloads these files and decodes MP3 in
software, then sends PCM audio to the reSpeaker XVF3800 board through I2S.

Use the guide prompts when the firmware needs to tell students what to do next,
for example "press the doorbell button", "not detected, check wiring", or
"PIR hold test passed".
