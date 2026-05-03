param(
  [string]$ConfigPath = ""
)

$ErrorActionPreference = "Stop"

function Write-Step {
  param([string]$Message)
  Write-Host ""
  Write-Host ">>> $Message"
}

function Expand-ConfigPath {
  param([string]$PathValue)
  if ([string]::IsNullOrWhiteSpace($PathValue)) {
    return ""
  }
  return [Environment]::ExpandEnvironmentVariables($PathValue)
}

function Find-GitRoot {
  param([string]$StartPath)
  $dir = Resolve-Path $StartPath
  while ($null -ne $dir) {
    if (Test-Path (Join-Path $dir ".git")) {
      return $dir.Path
    }
    $parent = Split-Path $dir -Parent
    if ([string]::IsNullOrWhiteSpace($parent) -or $parent -eq $dir.Path) {
      break
    }
    $dir = Resolve-Path $parent
  }
  return $null
}

function Invoke-Git {
  param(
    [string[]]$GitArgs,
    [string]$FailureMessage
  )
  Write-Host "git $($GitArgs -join ' ')"
  & git @GitArgs
  if ($LASTEXITCODE -ne 0) {
    throw "$FailureMessage exit=$LASTEXITCODE"
  }
}

function Repair-StaleGitRebase {
  param([string]$WorkDir)

  $gitDir = Join-Path $WorkDir ".git"
  $rebaseMerge = Join-Path $gitDir "rebase-merge"
  $rebaseApply = Join-Path $gitDir "rebase-apply"
  if (-not ((Test-Path $rebaseMerge) -or (Test-Path $rebaseApply))) {
    return
  }

  # 现场远程调试时，学生可能会在 Git 正在 pull/push 日志时拔线、断电或关闭窗口。
  # 这会留下 rebase 临时状态，让后续自动更新和日志上传一直失败。先尝试 abort，
  # 如果 Git 只剩空的临时目录，再清掉这个目录，让仓库恢复到可继续同步的状态。
  Write-Host "Detected unfinished Git rebase state. Trying to recover..."
  & git -C $WorkDir rebase --abort 2>$null
  if ($LASTEXITCODE -eq 0) {
    Write-Host "Git rebase state recovered by rebase --abort."
    return
  }

  if (Test-Path $rebaseMerge) {
    Remove-Item -LiteralPath $rebaseMerge -Recurse -Force -ErrorAction SilentlyContinue
  }
  if (Test-Path $rebaseApply) {
    Remove-Item -LiteralPath $rebaseApply -Recurse -Force -ErrorAction SilentlyContinue
  }
  Write-Host "Removed stale Git rebase state directories."
}

function Repair-StaleGitIndexLock {
  param([string]$WorkDir)

  $lockPath = Join-Path (Join-Path $WorkDir ".git") "index.lock"
  if (-not (Test-Path $lockPath)) {
    return
  }

  $lock = Get-Item -LiteralPath $lockPath -ErrorAction SilentlyContinue
  if ($null -eq $lock) {
    return
  }
  $ageSeconds = [int]((Get-Date) - $lock.LastWriteTime).TotalSeconds
  if ($ageSeconds -lt 30) {
    Write-Host "Git index.lock exists and is recent; waiting for the current Git operation to finish."
    return
  }

  $runningGit = @()
  try {
    $runningGit = @(Get-CimInstance Win32_Process -ErrorAction SilentlyContinue | Where-Object {
      $_.Name -match '^(git|git-remote-https|ssh)\.exe$' -and
      ([string]$_.CommandLine) -like "*$WorkDir*"
    })
  } catch {
    $runningGit = @()
  }

  if ($runningGit.Count -gt 0) {
    Write-Host "Git index.lock exists, but another Git process seems to be running. Keeping the lock file."
    return
  }

  # index.lock 是 Git 为保护索引文件创建的临时锁。现场强行关闭窗口时它可能残留，
  # 导致后续 git pull/add/commit 都失败。确认没有 Git 进程后再删除。
  Remove-Item -LiteralPath $lockPath -Force -ErrorAction SilentlyContinue
  Write-Host "Removed stale Git index.lock: $lockPath"
}

function Set-LocalGitIdentity {
  param(
    [string]$WorkDir,
    [string]$Name,
    [string]$Email
  )
  if ([string]::IsNullOrWhiteSpace($Name)) {
    $Name = "Smart Door Eye Remote Agent"
  }
  if ([string]::IsNullOrWhiteSpace($Email)) {
    $Email = "smart-door-eye-agent@example.invalid"
  }

  $currentName = (& git -C $WorkDir config user.name 2>$null)
  if ([string]::IsNullOrWhiteSpace($currentName)) {
    Invoke-Git -GitArgs @("-C", $WorkDir, "config", "user.name", $Name) -FailureMessage "git config user.name failed"
  }

  $currentEmail = (& git -C $WorkDir config user.email 2>$null)
  if ([string]::IsNullOrWhiteSpace($currentEmail)) {
    Invoke-Git -GitArgs @("-C", $WorkDir, "config", "user.email", $Email) -FailureMessage "git config user.email failed"
  }
}

function Require-File {
  param([string]$PathValue, [string]$Description)
  if (-not (Test-Path $PathValue)) {
    throw "$Description not found: $PathValue"
  }
}

function To-RepoRelativePath {
  param([string]$PathValue)
  return ($PathValue -replace '\\', '/').TrimStart('/')
}

$PackageRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
if ([string]::IsNullOrWhiteSpace($ConfigPath)) {
  $ConfigPath = Join-Path $PackageRoot "bootstrap_config.json"
}
Require-File -PathValue $ConfigPath -Description "Bootstrap config"
$Config = Get-Content -Raw -Encoding UTF8 $ConfigPath | ConvertFrom-Json

$gitCommand = Get-Command git -ErrorAction SilentlyContinue
if ($null -eq $gitCommand) {
  throw "Git was not found. Install Git for Windows first, then run this script again."
}

$repoUrl = [string]$Config.repoUrl
$repoUrlNeedsTeacher = [string]::IsNullOrWhiteSpace($repoUrl) -or $repoUrl -eq "REPO_URL_FILLED_BY_TEACHER_BEFORE_SEND"
$repoRoot = Expand-ConfigPath ([string]$Config.repoRoot)
if ([string]::IsNullOrWhiteSpace($repoRoot)) {
  $repoRoot = Find-GitRoot -StartPath $PackageRoot
}
if ([string]::IsNullOrWhiteSpace($repoRoot)) {
  $repoRoot = Join-Path $env:USERPROFILE "Documents\XuanXuan-smart-door-eye"
}

$deviceId = [string]$Config.deviceId
if ([string]::IsNullOrWhiteSpace($deviceId)) {
  throw "deviceId is empty. Edit bootstrap_config.json."
}

$xiaoPort = [string]$Config.xiaoPort
if ([string]::IsNullOrWhiteSpace($xiaoPort)) {
  throw "xiaoPort is empty. Run scripts\list_ports.cmd, then edit bootstrap_config.json."
}

Write-Host ""
Write-Host "=== Smart Door Eye Remote Debug Bootstrap ==="
Write-Host "repoRoot=$repoRoot"
Write-Host "deviceId=$deviceId"
Write-Host "xiaoPort=$xiaoPort"

if (Test-Path (Join-Path $repoRoot ".git")) {
  Write-Step "Updating existing Git repository"
  Repair-StaleGitRebase -WorkDir $repoRoot
  Repair-StaleGitIndexLock -WorkDir $repoRoot
  Invoke-Git -GitArgs @("-C", $repoRoot, "pull", "--rebase", "--autostash") -FailureMessage "git pull failed"
} else {
  if ($repoUrlNeedsTeacher) {
    throw "This bootstrap package is not ready: repoUrl is still a placeholder. The maintainer must fill the student remote-debug Git URL before sending this package."
  }
  Write-Step "Cloning project Git repository"
  $parent = Split-Path $repoRoot -Parent
  if (-not (Test-Path $parent)) {
    New-Item -ItemType Directory -Force $parent | Out-Null
  }
  Invoke-Git -GitArgs @("clone", $repoUrl, $repoRoot) -FailureMessage "git clone failed"
}

Write-Step "Configuring local Git identity"
Set-LocalGitIdentity -WorkDir $repoRoot -Name ([string]$Config.gitUserName) -Email ([string]$Config.gitUserEmail)

Write-Step "Checking downloaded remote debug agent"
$agentPackageDir = [string]$Config.agentPackageDir
if ([string]::IsNullOrWhiteSpace($agentPackageDir)) {
  $agentPackageDir = "release/smart-door-eye-remote-debug-20260502"
}
$agentPackageRel = To-RepoRelativePath $agentPackageDir
$agentRoot = Join-Path $repoRoot ($agentPackageRel -replace '/', '\')
$agentScript = Join-Path $agentRoot "scripts\remote_debug_agent.ps1"
$esptoolPath = Join-Path $agentRoot "tools\esptool\esptool.exe"
$mainFirmwareRel = "$agentPackageRel/firmware/main_xiao_esp32c6/smartdooreye.ino.merged.bin"
$xvfFirmwareRel = "$agentPackageRel/firmware/xvf_i2s_test_xiao_esp32c6/sketch_apr18a.ino.merged.bin"
$camFirmwareRel = "$agentPackageRel/firmware/cam_ai_thinker_esp32cam/CameraWebServer.ino.merged.bin"

Require-File -PathValue $agentScript -Description "Remote agent script"
Require-File -PathValue $esptoolPath -Description "esptool"
Require-File -PathValue (Join-Path $repoRoot ($mainFirmwareRel -replace '/', '\')) -Description "Main firmware"
Require-File -PathValue (Join-Path $repoRoot ($xvfFirmwareRel -replace '/', '\')) -Description "XVF test firmware"

Write-Step "Writing runtime agent config"
$runtimeConfigPath = Join-Path $agentRoot "remote_agent_config.from_bootstrap.json"
$runtimeConfig = [ordered]@{
  deviceId = $deviceId
  repoRoot = $repoRoot
  xiaoPort = $xiaoPort
  autoDetectXiaoPort = $Config.autoDetectXiaoPort -ne $false
  xiaoBaud = if ([int]$Config.xiaoBaud -gt 0) { [int]$Config.xiaoBaud } else { 115200 }
  logDir = if ([string]::IsNullOrWhiteSpace([string]$Config.logDir)) { "test/remote-debug-logs" } else { [string]$Config.logDir }
  commandDir = if ([string]::IsNullOrWhiteSpace([string]$Config.commandDir)) { "test/remote-debug-commands" } else { [string]$Config.commandDir }
  logPushIntervalSeconds = if ([int]$Config.logPushIntervalSeconds -gt 0) { [int]$Config.logPushIntervalSeconds } else { 60 }
  commandPollIntervalSeconds = if ([int]$Config.commandPollIntervalSeconds -gt 0) { [int]$Config.commandPollIntervalSeconds } else { 20 }
  autoPush = $Config.autoPush -eq $true
  allowFlashMain = $Config.allowFlashMain -eq $true
  allowFlashXvfTest = $Config.allowFlashXvfTest -eq $true
  allowFlashCam = $Config.allowFlashCam -eq $true
  gitUserName = if ([string]::IsNullOrWhiteSpace([string]$Config.gitUserName)) { "Smart Door Eye Remote Agent" } else { [string]$Config.gitUserName }
  gitUserEmail = if ([string]::IsNullOrWhiteSpace([string]$Config.gitUserEmail)) { "smart-door-eye-agent@example.invalid" } else { [string]$Config.gitUserEmail }
  defaultMainFirmware = $mainFirmwareRel
  defaultXvfTestFirmware = $xvfFirmwareRel
  defaultCamFirmware = $camFirmwareRel
}
$runtimeConfig | ConvertTo-Json -Depth 6 | Set-Content -Encoding UTF8 $runtimeConfigPath

Write-Step "Starting remote debug agent"
Write-Host "Agent script: $agentScript"
Write-Host "Agent config: $runtimeConfigPath"
Write-Host "Leave this window open after the agent starts."
& powershell -NoProfile -ExecutionPolicy Bypass -File $agentScript -ConfigPath $runtimeConfigPath
