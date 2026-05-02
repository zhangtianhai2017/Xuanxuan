# Remote Debug Runbook

这份文档给远程调试人员使用。现场同学只看启动包里的 `README_现场同学只看这个.md`。

## 当前远程仓库

```text
https://github.com/zhangtianhai2017/Xuanxuan.git
```

现场启动包已经指向这个仓库。现场同学只需要修改 `xiaoPort`。

## 现场启动

发给现场同学：

```text
release/smart-door-eye-student-bootstrap-20260502.zip
```

现场同学操作：

1. 解压。
2. 运行 `scripts\list_ports.cmd` 找 XIAO 的 COM 口。
3. 修改 `bootstrap_config.json` 里的 `xiaoPort`。
4. 双击 `scripts\setup_and_run_remote_agent.cmd`。
5. 保持窗口打开，保持 XIAO USB 连接，保持电脑联网。

## 我们这边查看日志

运行：

```powershell
.\scripts\sync_remote_debug_logs.cmd
```

或：

```powershell
.\scripts\sync_remote_debug_logs.ps1 -DeviceId smart-door-eye-lab-01
```

日志位置：

```text
test/remote-debug-logs/<deviceId>/
```

命令执行结果位置：

```text
test/remote-debug-logs/<deviceId>/command-results/
```

## 远程刷 XIAO 主控

运行：

```powershell
.\scripts\send_remote_debug_command.cmd
```

默认动作是：

```text
flash_main
```

也可以直接运行：

```powershell
.\scripts\send_remote_debug_command.ps1 -DeviceId smart-door-eye-lab-01 -Action flash_main
```

脚本会写入并推送：

```text
test/remote-debug-commands/<deviceId>/command.json
```

现场 Agent 拉到命令后会：

1. 暂停 XIAO 串口日志。
2. 调用仓库里的 `esptool.exe`。
3. 擦除并烧录 XIAO。
4. 重启后重新打开串口日志。
5. 把结果日志 push 回仓库。

## 远程刷 XIAO 音频测试固件

用于检查 reSpeaker/I2S 音频链路：

```powershell
.\scripts\send_remote_debug_command.ps1 -DeviceId smart-door-eye-lab-01 -Action flash_xvf_test
```

测试完再刷回主控：

```powershell
.\scripts\send_remote_debug_command.ps1 -DeviceId smart-door-eye-lab-01 -Action flash_main
```

## ESP32-CAM 固件

CAM 首次烧录和普通重刷需要现场介入，因为通常要 `GPIO0` 接 GND 并复位进入下载模式。

远程脚本里有 `flash_cam`，但默认需要显式加 `-AllowCam`，并且现场必须已经准备好 CAM 下载模式：

```powershell
.\scripts\send_remote_debug_command.ps1 -DeviceId smart-door-eye-lab-01 -Action flash_cam -Port COM7 -AllowCam
```

没有现场配合时，不要下发 `flash_cam`。

## 常见状态

- 只看到 `serial_open_failed`：现场 COM 口填错，或 XIAO 没接到电脑。
- 没有日志上传：现场 Git 没有 push 权限，或 Agent 窗口被关闭。
- 命令不执行：确认 `command.json` 的 `id` 是新值，Agent 每个 `id` 只执行一次。
- 烧写失败但日志还在：查看 `command-results` 下的最新结果文件。

## 权限提醒

现场电脑必须能 push 到这个仓库，否则日志和命令结果无法自动回传。首次运行时 Git 可能要求登录 GitHub。
