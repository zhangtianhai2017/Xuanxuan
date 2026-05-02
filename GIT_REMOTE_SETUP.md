# Git Remote Setup

本项目拆成两个远程 Git 仓库使用。

## 1. 完整项目仓库

用途：保存完整源码、文档、硬件资料、完整远程调试包。

本地工作树：

```text
F:\work\2026\codex\XuanXuan
```

当前本地提交：

```text
1649320 Prepare smart door eye project repository
```

临时本地 bare remote：

```text
F:\work\2026\codex\XuanXuan\_remotes\full-project.git
```

## 2. 学生/远程调试仓库

用途：现场启动包自动 clone/pull 的仓库。它只包含远程调试所需内容，不包含完整源码和本机编译工具链。

本地工作树：

```text
F:\work\2026\codex\XuanXuan\git_prepared\student-remote-debug
```

当前本地提交：

```text
1e2b87b Prepare student remote debug repository
```

临时本地 bare remote：

```text
F:\work\2026\codex\XuanXuan\_remotes\student-remote-debug.git
```

## 现场启动包的 repoUrl

现场启动包里的 `bootstrap_config.json` 应该指向“学生/远程调试仓库”，不是完整项目仓库。

发给学生前，由维护者填写：

```json
{
  "repoUrl": "学生/远程调试仓库的 Git URL",
  "xiaoPort": "COM5"
}
```

现场学生只改 `xiaoPort`。

## 推送到公网远端

创建公网远端仓库后，分别执行：

```powershell
git remote add origin <完整项目仓库URL>
git push -u origin main
```

以及：

```powershell
cd git_prepared\student-remote-debug
git remote add origin <学生远程调试仓库URL>
git push -u origin main
```

然后把 `release/smart-door-eye-student-bootstrap-20260502/bootstrap_config.json` 的 `repoUrl` 改为学生远程调试仓库 URL，重新生成启动包。
