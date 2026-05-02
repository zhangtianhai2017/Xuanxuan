# 智能猫眼远程调试固件包

这个包用于异地调试。目标是让现场同学只做固定动作，把串口日志自动发回，并允许我们远程更新部分固件。

## 远程调试架构

仅靠门口设备自己上电，不能凭空把日志传回来，也不能凭空重刷串口固件；现场必须有一台常驻的调试网关，例如 Windows 笔记本、迷你主机或树莓派。推荐链路是：

```text
智能猫眼硬件
  └─ XIAO USB 串口
      └─ 现场调试网关
          └─ 项目 Git 仓库
              ├─ test/remote-debug-logs/      日志自动上传到这里
              └─ test/remote-debug-commands/  我们从这里下发烧录命令
```

现场同学只需要把 XIAO 接到调试网关、上电、运行一次 `scripts/run_remote_debug_agent.cmd`。之后：

- 我们看 Git 里的 `test/remote-debug-logs/<deviceId>/`，不需要他们手动复制日志。
- 我们提交 `test/remote-debug-commands/<deviceId>/command.json`，agent 会执行白名单烧录动作。
- XIAO 主控固件可以远程更新；ESP32-CAM 固件只有在现场硬件支持远程进入下载模式时才可以更新。

## 包内文件

- `firmware/main_xiao_esp32c6/`：XIAO ESP32C6 主控固件。
- `firmware/cam_ai_thinker_esp32cam/`：AI Thinker ESP32-CAM 串口拍照固件。
- `firmware/xvf_i2s_test_xiao_esp32c6/`：reSpeaker XVF3800 I2S 单独测试固件。
- `tools/esptool/esptool.exe`：Windows 烧录工具。
- `scripts/flash_main_xiao_esp32c6.cmd`：烧录主控。
- `scripts/flash_cam_ai_thinker_esp32cam.cmd`：烧录 ESP32-CAM。
- `scripts/flash_xvf_i2s_test_xiao_esp32c6.cmd`：烧录音频测试固件。
- `scripts/capture_xiao_log.cmd`：采集 XIAO 主控日志。
- `scripts/capture_cam_usbttl_log.cmd`：单独采集 CAM USB-TTL 日志。
- `git_upload_config.example.json`：把日志自动放进项目 Git 的测试目录。
- `remote_agent_config.json`：现场常驻远程调试 Agent 配置。
- `scripts/run_remote_debug_agent.cmd`：持续看日志、定期 push、接收 Git 远程烧录命令。
- `现场记录表_请填写.md`：请现场同学填写并发回。

## 重要原则

正常整机调试只看 XIAO 的 USB 串口日志，波特率 `115200`。

主控会转发 CAM 的文本日志，格式类似：

```text
[812][INFO][CAM_RAW][LINE] [95][INFO][CAM][READY] sensor=OV2640 frame=QVGA jpeg_quality=15
[1240][INFO][CAM_RAW][LINE] STATUS ready=1 init_err=0x0 captures=0 fb_fail=0 last_fb_len=0 last_capture_ms=0 last_send_ms=0 last_jpeg_write_bytes=0 free_heap=293000 psram=1
```

JPEG 图片本体不会打印到调试口。日志只报告传输统计和错误：

```text
[5020][INFO][CAM][IMAGE_LEN] bytes=18342 wait_ms=2 raw_len_hex="A6 47 00 00" free_heap_before_alloc=279000 free_heap_after_alloc=260000
[7630][INFO][CAM][IMAGE_RX_OK] bytes=18342 rx_ms=2610 first_byte_wait_ms=0 max_gap_ms=4 avg_Bps=7027 soi=1 eoi=1
[7635][INFO][CAM][DONE_OK] wait_ms=5
```

## 第 1 步：识别 COM 口

双击：

```text
scripts/list_ports.cmd
```

插上 XIAO 后记下新增的 COM 口。插上 ESP32-CAM 的 USB-TTL 后也记下新增的 COM 口。

## 第 2 步：烧录 ESP32-CAM

烧录前：

1. ESP32-CAM 的 `GPIO0` 接 `GND`。
2. USB-TTL 的 TX/RX 与 CAM 交叉连接。
3. CAM 使用稳定 5V 供电。

双击：

```text
scripts/flash_cam_ai_thinker_esp32cam.cmd
```

烧录完成后必须：

1. 断开 `GPIO0` 与 `GND`。
2. 重启或重新上电 ESP32-CAM。

## 第 3 步：烧录 XIAO 主控

双击：

```text
scripts/flash_main_xiao_esp32c6.cmd
```

这会擦除 XIAO flash 并写入主控固件。

## 第 4 步：采集主控日志

## 推荐模式：现场常驻远程调试 Agent

如果你的目标是“现场只接好上电，我们这边远程看日志、远程更新部分固件”，请使用这个模式。

现场同学只需要：

1. 电脑上 clone 项目 Git 仓库。
2. 解压本调试包到项目仓库内。
3. 编辑 `remote_agent_config.json`：

```json
{
  "deviceId": "smart-door-eye-lab-01",
  "repoRoot": "",
  "xiaoPort": "COM5",
  "autoDetectXiaoPort": true,
  "camPort": "",
  "autoPush": true,
  "allowFlashMain": true,
  "allowFlashXvfTest": true,
  "allowFlashCam": false
}
```

4. 双击：

```text
scripts/run_remote_debug_agent.cmd
```

Agent 会持续做三件事：

1. 读取 XIAO USB 串口日志。
2. 定期提交并 push 到：

```text
test/remote-debug-logs/<deviceId>/
```

3. 定期从 Git 拉取远程命令：

```text
test/remote-debug-commands/<deviceId>/command.json
```

### 远程更新 XIAO 主控固件

我们这边把新的固件放入 Git，例如：

```text
release/smart-door-eye-remote-debug-20260502/firmware/main_xiao_esp32c6/smartdooreye.ino.merged.bin
```

然后提交命令文件：

```json
{
  "id": "20260502-001",
  "action": "flash_main",
  "firmware": "release/smart-door-eye-remote-debug-20260502/firmware/main_xiao_esp32c6/smartdooreye.ino.merged.bin",
  "erase": true
}
```

Agent 拉到命令后会：

1. 暂停串口日志。
2. 调用 `esptool.exe` 烧录 XIAO。
3. 重新打开串口日志。
4. 把烧录结果提交到：

```text
test/remote-debug-logs/<deviceId>/command-results/
```

### 远程更新 CAM 固件的限制

`flash_cam` 命令已经支持，但默认关闭：

```json
"allowFlashCam": false
```

原因是 ESP32-CAM 进入下载模式通常需要 `GPIO0` 接 GND 并复位。若现场没有 USB-TTL 自动下载电路，或没有用继电器/三极管把 `GPIO0/EN` 做成可远程控制，纯远程无法可靠让 CAM 进入烧录模式。

要远程更新 CAM，需要满足至少一个条件：

- CAM 的 USB-TTL 模块支持 DTR/RTS 自动下载，并且已正确接到 `GPIO0/EN`。
- 现场加了可远程控制的继电器/小电路，用来拉低 `GPIO0` 并复位 CAM。
- CAM 固件以后改成 WiFi OTA。

否则 CAM 只能远程看状态和日志，不能保证远程重刷。

### 能远程更新哪些“部分”

当前可靠支持：

- XIAO 主控固件：可以远程更新，只要 XIAO USB 接在现场电脑上。
- XVF I2S 测试固件：可以远程临时烧到 XIAO，用来测音频链路。

有硬件条件才支持：

- ESP32-CAM 固件。

不建议远程更新：

- reSpeaker XVF3800 本体固件，除非现场有对应 DFU 连接和明确流程。

### 可选：自动提交到项目 Git 测试目录

如果现场同学电脑上已经 clone 了这个项目 Git 仓库，可以启用自动上传到仓库的测试目录。

操作方法：

1. 包里已经带有 `git_upload_config.json`，默认启用 Git 上传。
2. 如果调试包放在项目仓库里面，`repoRoot` 可以留空，脚本会向上查找 `.git`。
3. 如果调试包不在项目仓库里面，把 `repoRoot` 改成项目仓库路径，例如：

```json
"repoRoot": "D:\\work\\XuanXuan"
```

4. 默认日志会复制到：

```text
test/remote-debug-logs/<deviceId>/
```

5. `autoCommit=true` 会自动 `git add` 和 `git commit`。
6. `autoPush=true` 会自动 `git push`。只有确认现场同学 Git 凭据可用时再打开。

如果现场同学没有 Git push 权限，先改成：

```json
"autoCommit": true,
"autoPush": false
```

有 push 权限时使用默认配置：

```json
"autoPush": true
```

双击：

```text
scripts/capture_xiao_log.cmd
```

输入 XIAO 的 COM 口，记录时间建议填 `10` 分钟。串口打开后，按一下 XIAO 的 RESET，然后按下面顺序测试：

1. 上电后等待 20 秒。
2. 按门铃 1 次。
3. PIR 前停留至少 35 秒。
4. 如果 Blynk 可用，按远程拍照按钮 1 次。
5. 如果 Blynk 可用，按播放吵架音按钮 1 次。

日志会自动保存到：

```text
logs/
```

如果启用了 `git_upload_config.json`，脚本结束时还会把 `.log` 和 `.meta.txt` 复制到项目 Git 的 `test/remote-debug-logs/` 下，并按配置自动 commit/push。

如果没有启用 Git 自动上传，请把生成的 `.log` 文件发回。

## 第 5 步：如果怀疑音频链路

先烧录音频测试固件：

```text
scripts/flash_xvf_i2s_test_xiao_esp32c6.cmd
```

然后运行：

```text
scripts/capture_xiao_log.cmd
```

如果 reSpeaker 与功放/喇叭正常，应能听到持续的 440Hz 测试音。测试完后重新烧录主控：

```text
scripts/flash_main_xiao_esp32c6.cmd
```

## 第 6 步：需要发回的东西

请发回：

1. `logs` 目录里的 `.log` 文件。
2. 填好的 `现场记录表_请填写.md`。
3. XIAO 与 ESP32-CAM 串口接线照片。
4. ESP32-CAM 供电照片。
5. PIR、门铃按键、reSpeaker 接线照片。

## 常见日志判断

`CAM STATUS_TIMEOUT`：
主控问 CAM 状态没有回应。优先查 TX/RX 接反、没共地、CAM 没运行、CAM 卡在烧录模式。

`CAM COMMAND_TIMEOUT`：
主控发了 `CAPTURE`，但 CAM 没回 `OK`。优先查 CAM 是否上电、GPIO0 是否还接 GND、串口线、共地。

`CAM COMMAND_RESPONSE_ERROR`：
CAM 回了 `ERROR`，说明命令到了 CAM，但 `esp_camera_fb_get()` 失败或摄像头没准备好。优先查 CAM 供电、排线、PWDN、GPIO0。

`CAM INVALID_IMAGE_LEN`：
长度头异常。看 `raw_len_hex`，如果出现 ASCII 字母，通常是 CAM 日志混入了协议；如果是很大的随机数，通常是串口干扰或供电抖动。

`CAM IMAGE_DATA_TIMEOUT`：
图片传到一半断了。看 `received/expected/max_gap_ms`，优先查供电瞬间压降、串口线、CAM 重启。

`CAM IMAGE_RX_OK soi=0` 或 `eoi=0`：
收到指定字节数，但 JPEG 头尾不完整，属于损坏图片。

`FACE SEARCH_HTTP_FAILED` / `BAIDU TOKEN_HTTP_FAILED`：
百度 API 网络或 Token 问题，不是 CAM 问题。

`AUDIO SKIP_WIFI_OFFLINE`：
网络 MP3 播放需要 WiFi，离线时不会播放网络音频。
