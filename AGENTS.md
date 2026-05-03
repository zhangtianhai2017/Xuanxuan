# Agent 要求

本项目是给正在学习嵌入式开发、物联网和 Arduino/ESP32 的大学生阅读和改造的示例项目。后续所有 Agent 在修改本项目时必须遵守以下要求：

1. 代码注释必须面向初学者，优先解释“为什么这样做”，不要只重复变量名。
2. 涉及硬件的代码必须写清楚相关硬件背景，例如引脚连接、通信协议、电平/供电注意事项和模块职责。
3. 涉及功能需求的代码必须说明它响应的是哪一条需求，例如门铃触发、PIR 30 秒逗留、连续抓拍、人脸识别、播放 3 首吵架声音等。
4. 修改功能时要保持示例项目的可读性，避免为了压缩代码而牺牲教学性。
5. 日志输出、状态机、超时和错误分支要保留清晰说明，方便学生通过串口监视器理解系统运行过程。
6. 尽量使用简单直接的结构，只有在能明显降低理解成本时才新增抽象。
7. 若改动会影响硬件接线、烧录步骤或模块间协议，必须同步更新代码注释。

## 本地编译与调试备忘

这些信息是当前仓库在本机已经验证过的编译环境。后续 Agent 不要重新猜测工具链位置或板卡参数，优先使用这里的记录。

1. Arduino CLI 路径：`F:\work\2026\codex\XuanXuan\tools\arduino-cli\arduino-cli.exe`。
2. Arduino CLI 配置文件：`F:\work\2026\codex\XuanXuan\arduino-cli.yaml`。
3. 已安装并验证的 ESP32 Arduino core：`esp32:esp32 3.3.8`。
4. 主控 XIAO ESP32C6 的编译 FQBN 必须使用：`esp32:esp32:XIAO_ESP32C6:PartitionScheme=huge_app`。
5. 必须使用 `HUGE_APP` 分区。当前主控固件已经超过默认 app 分区容量；`huge_app` 下已验证可编译通过。
6. Arduino 要求草图目录名和 `.ino` 文件同名。本项目主控源代码目录是中文 `主程序`，而主草图是 `smartdooreye.ino`，所以本地编译时要复制一份到临时目录，例如 `C:\tmp\smartdooreye_build_YYYYMMDD_HHMMSS\smartdooreye\smartdooreye.ino`。这样可以满足 Arduino 规则，同时不改项目结构。
7. 已验证的主控编译命令示例：

   ```powershell
   .\tools\arduino-cli\arduino-cli.exe compile --config-file .\arduino-cli.yaml --fqbn "esp32:esp32:XIAO_ESP32C6:PartitionScheme=huge_app" C:\tmp\smartdooreye_build_YYYYMMDD_HHMMSS\smartdooreye
   ```

8. 发布包内主控固件位置：`release\smart-door-eye-remote-debug-20260502\firmware\main_xiao_esp32c6\`。重新编译后需要同步更新 `smartdooreye.ino.bin`、`smartdooreye.ino.merged.bin` 和 `MANIFEST_SHA256.txt`。
9. 本机图片格式和百度 API 测试工具：`scripts\test_baidu_image_format.cmd` / `scripts\test_baidu_image_format.ps1`。这个工具只在电脑上验证 GitHub 测试图片、Base64 和百度 detect/search API，不依赖现场 XIAO 或 ESP32-CAM。

## 主控固件编译要点

这一轮已经确认主控固件编译耗时较长，后续 Agent 要减少无意义的重复编译。

1. 只有修改 `主程序` 里的 `.ino`、`.cpp`、`.h` 等会进入 XIAO ESP32C6 固件的代码时，才需要重新编译主控固件。
2. 只改远程 Agent、PowerShell 脚本、文档、GitHub 测试图片、GitHub 音频文件时，不需要重新编译主控固件。
3. 编译前不要删除 `tools`、Arduino core、Arduino CLI 缓存或已经安装好的板卡包；这些内容重新下载和索引会明显拖慢进度。
4. 因为 Arduino 要求草图目录名和 `.ino` 同名，编译时复制 `主程序` 到临时目录 `...\smartdooreye\`，不要为了编译而重命名项目目录。
5. 编译必须带 `PartitionScheme=huge_app`，否则当前固件体积可能超过默认分区。
6. 建议把编译输出放到临时输出目录，例如 `C:\tmp\smartdooreye_build_YYYYMMDD_HHMMSS\out`，确认成功后只把需要发布的 `.bin` 文件复制回 release。
7. 需要同步到发布包的主要产物是：
   - `smartdooreye.ino.bin`
   - `smartdooreye.ino.merged.bin`
   - `MANIFEST_SHA256.txt`
8. 编译通过的关键判断不是只看命令退出，还要确认日志里出现程序占用信息，例如 `Sketch uses ... bytes`，并且没有分区容量错误。
9. 这一轮已验证的构建重点：Baidu 上传修复只改了主控代码，使用 `huge_app` 后编译通过；发布时更新主控 bin、merged bin 和校验清单即可，不需要重新处理 ESP32-CAM 固件。

## 远程现场调试反复踩坑记录

这些规则来自本项目 2026-05-03 的现场远程调试。后续 Agent 必须优先遵守，避免再次浪费现场时间。

1. 如果用户要求“新的 ZIP 包”“不要管以前的东西”“不用发过去”，就只在本机生成一个新的独立 ZIP 包，不再尝试通过 Git 更新现场旧 Agent。
2. 给现场同学的包必须是最小操作：解压、双击 `start_remote_debug.cmd`、窗口保持打开。不要让现场同学执行复杂 Git 命令，除非用户明确要求。
3. 新 Agent 包必须新建一个全新的项目目录，不读取、不修复、不上传旧目录里的日志和旧状态。
4. 新 Agent 第一次启动时，必须把仓库里当前已有的 `test/remote-debug-commands/<device>/command.json` 记录为“已处理”，避免一启动就重复执行上一轮 `flash_main` 烧写命令。
5. 打包前必须本机测试启动脚本，至少验证：能 clone 到临时新目录、能生成 Agent 配置、能写入 `last_command_id.txt`、能用 `-SkipAgentStart` 跳过真正启动。
6. 旧日志不要进入新包，也不要为了“保留历史”上传超大日志。远程日志只保留用于当前判断的短结论；压缩上下文或写记忆时不要保存大段现场原始 log。
7. 如果 GitHub 拒绝 push 是因为历史日志超过 100MB，不要继续围绕旧仓库状态让现场处理。优先给新的干净 ZIP 包，恢复调试链路。
8. 当串口日志显示 `PLAY_START` / `PLAY_DONE` 时，只能说明软件已经发起播放流程；如果现场听不到声音，仍然要判断 reSpeaker、喇叭、I2S 接线、供电或音量等硬件输出问题，不能直接说“现场已经播放成功”。
9. 回答现场状态时要直接给结论：主控、WiFi、PIR、Baidu、CAM、门铃、音频软件流程分别是否正常。不要把大段日志贴给用户。
10. 用户已经明确反感反复复杂化流程。遇到可用 ZIP 解决的问题，优先给 ZIP 路径和唯一运行入口，不再展开多套方案。
11. Agent 本身不允许自更新，也不允许运行中检测到脚本变化后自动重启。这个机制在现场调试中过于复杂且危险：旧窗口会停在 pause，新旧状态可能交叉，还可能重新读到历史命令。以后更新 Agent 只允许重新生成新的 ZIP 包，由现场明确手动启动。
