# 智能猫眼系统项目文档

本项目是一个基于 XIAO ESP32C6、ESP32-CAM、reSpeaker XVF3800、PIR 传感器和 Blynk 的智能猫眼/门铃系统。系统支持门铃触发拍照、人体异常逗留检测、音频提示/威慑、百度云 AI 人脸识别和远程控制。

## 1. 功能需求

### 1.1 门铃触发

访客按下门铃按键后，系统执行：

- ESP32-CAM 抓拍照片。
- reSpeaker 播放门铃提示音。
- 抓拍照片上传至百度云 AI 人脸识别 API，用于识别访客身份。

### 1.2 人体移动侦测

触发条件：

- PIR 传感器检测到人体。
- 人体逗留时间大于或等于 30 秒。

执行动作：

- ESP32-CAM 连续抓拍照片。
- reSpeaker 随机播放 3 首吵架声音，本地存储或网络流均可。
- 所有抓拍照片上传至百度云 AI 人脸识别 API，用于识别访客身份。

### 1.3 人脸识别

- 门铃触发照片和异常逗留照片都必须上传至百度云 AI 人脸识别 API。
- 系统根据识别结果判断访客身份。
- 若未匹配已有身份，可按性别或未知类别注册新访客。

## 2. 系统组成

- 主控：XIAO ESP32C6。
- 摄像头：ESP32-CAM。
- 音频模块：reSpeaker XVF3800。
- 人体检测：PIR 传感器，例如 HC-SR501。
- 门铃按键：TS-1187A-B-A-B 或同类按键。
- 云服务：百度云 AI 人脸识别 API。
- 远程控制：Blynk。

## 3. 电路连接方案

本方案仅使用 XIAO ESP32C6 正面引脚，不使用任何背面引脚。所有信号连接均通过正面排针完成。

### 3.1 电源网络

| 电源网络 | 来源 | 去向模块 | 说明 |
| --- | --- | --- | --- |
| 5V | USB-C VBUS，A9 + B9 短接 | XIAO ESP32C6 5V、ESP32-CAM POW 5V、reSpeaker XVF3800 5V、PIR VCC | 统一 5V 供电 |
| GND | USB-C GND，A12 + B12 短接 | 所有模块 GND | 公共地 |
| CC 配置 | USB-C CC1/A5、CC2/B5 | 各通过 5.1kΩ 电阻下拉到 GND | 使 USB-C 适配器输出 5V |

电源滤波：

- 在 XIAO ESP32C6 的 5V 与 GND 之间并联 100uF 电解电容和 0.1uF 陶瓷电容。
- 在 ESP32-CAM 的 5V 与 GND 之间并联 470uF 电解电容，提高摄像头供电稳定性。

### 3.2 XIAO ESP32C6 正面引脚分配

| 功能 | XIAO 正面引脚 | GPIO 编号 | 连接对象 | 备注 |
| --- | --- | --- | --- | --- |
| UART TX | D6/TX | GPIO16 | ESP32-CAM U0RXD/GPIO3 | 主控向摄像头发送拍照指令 |
| UART RX | D7/RX | GPIO17 | ESP32-CAM U0TXD/GPIO1 | 摄像头向主控发送图像数据 |
| I2C SDA | D4/SDA | GPIO22 | reSpeaker I2C_SDA | 配置音频参数 |
| I2C SCL | D5/SCL | GPIO23 | reSpeaker I2C_SCL | 配置音频参数 |
| I2S BCLK | D0 | GPIO0 | reSpeaker I2S_BCLK | 音频位时钟，主控输出 |
| I2S LRCK | D1 | GPIO1 | reSpeaker I2S_LRCK | 左右声道时钟，主控输出 |
| I2S DATA0 | D2 | GPIO2 | reSpeaker I2S_DATA0 | 主控到 reSpeaker，威慑音/通话音频 |
| I2S DATA1 | D3 | GPIO21 | reSpeaker I2S_DATA1 | reSpeaker 到主控，麦克风录音 |
| I2S MCLK | D8/SCK | GPIO19 | reSpeaker MCLK | 主时钟，推荐连接 |
| PIR 输入 | D9/MISO | GPIO20 | PIR OUT | 检测人体 |
| 门铃按键 | D10/MOSI | GPIO18 | 按键，另一端接 GND | 内部上拉，按下为低电平 |

注意：

- D8、D9、D10 的丝印标注为 SCK、MISO、MOSI，但可作为普通 GPIO 使用。
- 如需释放备用引脚，可以省略 I2S MCLK 并将 GPIO19 悬空；但 reSpeaker XVF3800 作为 I2S 从机通常需要外部主时钟，省略后音频可能不稳定。

### 3.3 ESP32-CAM 连接

| ESP32-CAM 引脚 | 功能 | 连接至 XIAO ESP32C6 | 说明 |
| --- | --- | --- | --- |
| U0RXD/GPIO3 | 串口接收 | D6/GPIO16 | 接收主控拍照指令 |
| U0TXD/GPIO1 | 串口发送 | D7/GPIO17 | 发送图像数据给主控 |
| POW/5V | 电源输入 | 5V | 从 USB-C 取电 |
| GND | 地 | GND | 公共地 |

其余 ESP32-CAM 引脚如 GPIO0、GPIO2、GPIO4、GPIO12-GPIO15 等悬空不接。

固件要求：

- ESP32-CAM 需预先烧录支持串口拍照的固件。
- 当前项目的 `cam代码/CameraWebServer.ino` 使用自定义串口协议响应 `CAPTURE` 命令。

### 3.4 reSpeaker XVF3800 连接

| reSpeaker 引脚 | 信号 | 连接至 XIAO ESP32C6 | 说明 |
| --- | --- | --- | --- |
| I2C_SDA | I2C 数据 | D4/GPIO22 | 配置音频参数 |
| I2C_SCL | I2C 时钟 | D5/GPIO23 | 配置音频参数 |
| I2S_BCLK | 位时钟 | D0/GPIO0 | I2S 时钟输入 |
| I2S_LRCK | 帧时钟 | D1/GPIO1 | 左右声道切换 |
| I2S_DATA0 | 音频输入，至 reSpeaker | D2/GPIO2 | 主控播放威慑音/通话音频 |
| I2S_DATA1 | 音频输出，从 reSpeaker | D3/GPIO21 | 麦克风录音输入 |
| MCLK | 主时钟 | D8/GPIO19 | 提高音频稳定性 |
| 5V | 电源 | 5V | 供电 |
| GND | 地 | GND | 公共地 |

注意：reSpeaker 左侧的 XIAO_D0 到 XIAO_D3 引脚在本方案中不使用。

### 3.5 PIR 传感器

| HC-SR501 引脚 | 连接至 XIAO ESP32C6 | 说明 |
| --- | --- | --- |
| VCC | 5V | 供电 |
| OUT | D9/GPIO20 | 高电平表示检测到人 |
| GND | GND | 公共地 |

注意：

- 部分 PIR 模块输出 5V 信号，需要用电阻分压到 3.3V 或使用电平转换。
- 请确认模块输出为 3.3V 逻辑。
- 若输出为 5V，可在 OUT 与 GPIO20 之间串联 10kΩ 电阻，并加 3.3V 稳压管钳位。

### 3.6 门铃按键

| 按键引脚对 | 连接 | 说明 |
| --- | --- | --- |
| 1-2 短接 | D10/GPIO18 | 代码中启用内部上拉 INPUT_PULLUP |
| 3-4 短接 | GND | 按下时 GPIO18 被拉低 |

去抖：

- 软件建议做 20ms 到 50ms 消抖。
- 可选硬件去抖：在 GPIO18 与 GND 之间并联 0.1uF 陶瓷电容。

## 4. 完整连接示意图

```text
USB-C 插座
 ├─ VBUS (A9+B9) ──────┬─ 5V ──┬─ XIAO ESP32C6 (5V)
 │                     │       ├─ ESP32-CAM (POW 5V)
 │                     │       ├─ reSpeaker XVF3800 (5V)
 │                     │       └─ PIR (VCC)
 ├─ GND (A12+B12) ─────┴─ GND ─┬─ 所有模块 GND
 ├─ CC1 (A5) ── 5.1kΩ ── GND
 └─ CC2 (B5) ── 5.1kΩ ── GND

XIAO ESP32C6 正面
 D0  (GPIO0)  ── reSpeaker BCLK
 D1  (GPIO1)  ── reSpeaker LRCK
 D2  (GPIO2)  ── reSpeaker DATA0
 D3  (GPIO21) ── reSpeaker DATA1
 D4  (GPIO22) ── reSpeaker SDA
 D5  (GPIO23) ── reSpeaker SCL
 D6  (GPIO16) ── ESP32-CAM U0RXD
 D7  (GPIO17) ── ESP32-CAM U0TXD
 D8  (GPIO19) ── reSpeaker MCLK
 D9  (GPIO20) ── PIR OUT
 D10 (GPIO18) ── 按键一端，按键另一端接 GND
 5V            ── 来自 USB-C VBUS
 GND           ── 来自 USB-C GND
```

## 5. 电阻和电容接法

### 5.1 CC 下拉电阻

作用：让 USB-C 适配器识别设备并输出 5V。

| 电阻 | 一端 | 另一端 |
| --- | --- | --- |
| R1，5.1kΩ | USB-C CC1/A5 | GND |
| R2，5.1kΩ | USB-C CC2/B5 | GND |

### 5.2 电源滤波电容

作用：稳定 5V 电源，滤除低频和高频噪声。

| 电容 | 正极/一端 | 负极/另一端 |
| --- | --- | --- |
| C1，100uF 电解 | 5V 网络，靠近 XIAO 5V 引脚 | GND |
| C2，0.1uF 陶瓷 | 5V 网络，靠近 XIAO 5V 引脚 | GND |
| C4，470uF 电解 | ESP32-CAM 5V | ESP32-CAM GND |

注意：

- 电解电容有极性，长脚为正极，短脚为负极，负极接 GND。
- 陶瓷电容无极性。
- 电容应尽量靠近对应模块的电源引脚。

### 5.3 I2C 上拉电阻

作用：保证 I2C 总线 SDA、SCL 通信可靠。

| 电阻 | 一端 | 另一端 |
| --- | --- | --- |
| R3，4.7kΩ | SDA 线，XIAO D4/GPIO22 | XIAO 3.3V |
| R4，4.7kΩ | SCL 线，XIAO D5/GPIO23 | XIAO 3.3V |

注意：

- 如果 reSpeaker 模块已经自带 I2C 上拉电阻，可以不加。
- 若重复加上，等效阻值约为 2.35kΩ，通常仍可工作。

### 5.4 按键去抖电容

作用：降低门铃按键机械抖动。

| 电容 | 一端 | 另一端 |
| --- | --- | --- |
| C3，0.1uF 陶瓷 | GPIO18，XIAO D10 | GND |

该电容与门铃按键并联。

## 6. 当前代码对应关系

主控程序位于 `主程序/`：

- `smartdooreye.ino`：主流程，包含门铃按键、PIR 逗留检测、Token 刷新和主循环。
- `camera_comm.cpp`：向 ESP32-CAM 发起拍照，并接收 JPEG 图像数据。
- `face_recognition.cpp`：调用百度云 AI 人脸识别 API。
- `audio_task.cpp`：通过 I2S 播放门铃声和吵架声音。
- `blynk_handlers.cpp`：Blynk 远程拍照和远程播放控制。

ESP32-CAM 程序位于 `cam代码/`：

- `CameraWebServer.ino`：启动摄像头、Web 预览服务和串口拍照协议。
- `board_config.h`：选择摄像头板型，当前为 AI Thinker ESP32-CAM。

reSpeaker/XVF3800 资料位于 `xvf烧录/`：

- 包含 DFU 工具、XVF3800 I2S 固件和 I2S 播放测试 sketch。

## 7. 串口拍照协议

主控向 ESP32-CAM 发送：

```text
CAPTURE\r\n
```

ESP32-CAM 返回：

```text
OK\r\n
4 字节小端 JPEG 长度
JPEG 二进制数据
DONE\r\n
```

主控收到 JPEG 后转换为 Base64，并上传至百度云 AI。

