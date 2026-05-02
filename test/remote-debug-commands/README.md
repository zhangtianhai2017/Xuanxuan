# Remote Debug Commands

远程调试 Agent 会定期从 Git 拉取这个目录下的命令文件。

设备专属命令路径：

```text
test/remote-debug-commands/<deviceId>/command.json
```

示例：远程更新 XIAO 主控固件

```json
{
  "id": "20260502-001",
  "action": "flash_main",
  "firmware": "release/smart-door-eye-remote-debug-20260502/firmware/main_xiao_esp32c6/smartdooreye.ino.merged.bin",
  "port": "COM5",
  "erase": true
}
```

仓库里可以保留 `command.example.json` 作为模板。真正要执行时，复制成 `command.json` 并提交；不要把示例文件直接改名，除非确定现场 agent 已经准备好执行。

推荐使用脚本自动生成并推送命令：

```powershell
.\scripts\send_remote_debug_command.cmd
```

查看日志和命令结果：

```powershell
.\scripts\sync_remote_debug_logs.cmd
```

支持的 `action`：

- `flash_main`：烧录 XIAO ESP32C6 主控固件。
- `flash_xvf_test`：烧录 XIAO ESP32C6 音频测试固件。
- `flash_cam`：烧录 ESP32-CAM 固件。注意：CAM 必须能远程进入下载模式，否则会失败。

安全规则：

- Agent 只执行白名单 action，不执行任意 shell 命令。
- 每个 `id` 只执行一次。
- 执行结果会写入 `test/remote-debug-logs/<deviceId>/command-results/`。
