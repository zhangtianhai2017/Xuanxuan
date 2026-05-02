# 智能猫眼远程调试与主程序更新启动包

这个包用于现场电脑。启动后，远程调试人员可以查看 XIAO 日志，并在需要时远程更新 XIAO 主程序。

现场同学通常只需要改 COM 口，然后保持窗口打开。配置里的自动串口识别会保持开启，Windows 临时改 COM 号时能自动找回 XIAO。

## 你需要准备

1. 一台能联网的 Windows 电脑。
2. 电脑已经安装 Git for Windows。
3. XIAO ESP32C6 用 USB 线接到这台电脑。
4. 智能猫眼整机正常上电。

## 如果还没有安装 Git

1. 打开浏览器，访问：

```text
https://git-scm.com/download/win
```

2. 下载 Windows 版 Git 安装包。
3. 双击安装包，一路使用默认选项安装即可。
4. 安装完成后，重新打开本文件夹。

如果窗口提示找不到 `git`，请重启电脑后再试一次。

## 第 1 步：确认 XIAO 的 COM 口

双击：

```text
scripts\list_ports.cmd
```

记下 XIAO 对应的 COM 口，例如 `COM5`。

如果不确定哪个是 XIAO，可以先拔掉 XIAO，运行一次；再插上 XIAO，运行一次。新出现的那个通常就是 XIAO。

## 第 2 步：修改 COM 口

用记事本打开：

```text
bootstrap_config.json
```

只改 `xiaoPort` 这一项：

```json
{
  "xiaoPort": "COM5",
  "autoDetectXiaoPort": true
}
```

把 `COM5` 改成第 1 步看到的 COM 口。`autoDetectXiaoPort` 保持 `true`，这样换 USB 孔或重新插拔后 COM 号变化也能继续远程调试。其他内容不要改。

## 第 3 步：启动

双击：

```text
scripts\setup_and_run_remote_agent.cmd
```

第一次运行会自动下载项目调试文件。以后运行会自动更新到最新版本。

启动成功后：

- 不要关闭窗口。
- 不要拔掉 XIAO USB。
- 保持电脑联网。
- 如果弹出 GitHub 登录窗口，请用已获得仓库写入权限的 GitHub 账号登录。
- 脚本会自动设置本仓库的 Git 提交身份，不需要手动运行 `git config`。

## 它会自动做什么

1. 自动读取 XIAO 串口日志。
2. 自动把日志上传到远程仓库。
3. 自动接收远程调试人员下发的调试命令。
4. 需要时自动更新 XIAO 主程序。

## 如果窗口报错

请截图发回，并附上：

```text
xiaoPort =
电脑能否 git push = 是 / 否 / 不确定
```

如果窗口提示 `Author identity unknown`，请按 `Ctrl+C` 停止窗口，然后重新双击 `scripts\setup_and_run_remote_agent.cmd`。

## 不要做的事

- 不要手动 clone 或 pull 仓库，脚本会自动做。
- 不要关闭 Agent 窗口。
- 不要修改 `scripts` 目录里的文件。
- 远程更新 XIAO 主程序时，不要拔 USB，不要关闭电脑。
