# 智能猫眼远程调试启动包

这个包会自动下载项目 Git 仓库里的最新调试文件，然后启动远程日志 Agent。

如果启动窗口提示 `repoUrl is still a placeholder`，说明这个包还没有由维护者写入远程仓库地址。这个问题不是 COM 口问题，请把截图发回，不需要自己修改其他配置。

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
5. 双击：

```text
scripts\setup_and_run_remote_agent.cmd
```

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
  "xiaoPort": "COM5"
}
```

把 `COM5` 改成第 1 步看到的 COM 口。其他内容不要改。

## 第 3 步：启动

双击：

```text
scripts\setup_and_run_remote_agent.cmd
```

第一次运行会自动 clone 项目仓库。以后运行会自动 pull 最新内容。启动成功后，不要关闭窗口，保持电脑联网，保持 XIAO USB 连接。

## 它会自动做什么

1. 自动下载或更新项目 Git 仓库。
2. 自动使用仓库里的最新调试文件。
3. 自动读取 XIAO 串口日志。
4. 自动把日志上传到项目 Git 仓库。
5. 自动接收远程调试人员下发的调试命令。
6. 自动设置本仓库的 Git 提交身份，不需要手动运行 `git config`。

## 如果窗口报错

请截图发回，并附上这三项：

```text
xiaoPort =
电脑能否 git push = 是 / 否 / 不确定
```

如果窗口提示 `Author identity unknown`，请按 `Ctrl+C` 停止窗口，然后重新双击 `scripts\setup_and_run_remote_agent.cmd`。

## 不要做的事

- 不要手动 clone 或 pull 仓库，脚本会自动做。
- 不要关闭 Agent 窗口。
- 不要修改 `scripts` 目录里的文件。
