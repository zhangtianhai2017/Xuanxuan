param(
  [string]$ConfigPath = ""
)

$ErrorActionPreference = "Stop"
$PackageRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
if ([string]::IsNullOrWhiteSpace($ConfigPath)) {
  $ConfigPath = Join-Path $PackageRoot "remote_agent_config.json"
}
if (-not (Test-Path $ConfigPath)) {
  throw "Config not found: $ConfigPath"
}

$ConfigPath = (Resolve-Path $ConfigPath).Path
$AgentScriptPath = if (-not [string]::IsNullOrWhiteSpace($PSCommandPath)) {
  (Resolve-Path $PSCommandPath).Path
} else {
  (Resolve-Path $MyInvocation.MyCommand.Path).Path
}
$script:AgentRestartRequested = $false

$Config = Get-Content -Raw -Encoding UTF8 $ConfigPath | ConvertFrom-Json

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

function Resolve-RepoPath {
  param([string]$PathValue)
  if ([string]::IsNullOrWhiteSpace($PathValue)) {
    return $null
  }
  if ([System.IO.Path]::IsPathRooted($PathValue)) {
    return $PathValue
  }
  return Join-Path $RepoRoot $PathValue
}

function Repair-StaleGitRebase {
  param([string]$WorkDir)

  $gitDir = Join-Path $WorkDir ".git"
  $rebaseMerge = Join-Path $gitDir "rebase-merge"
  $rebaseApply = Join-Path $gitDir "rebase-apply"
  $hasRebaseState = (Test-Path $rebaseMerge) -or (Test-Path $rebaseApply)
  if (-not $hasRebaseState) {
    return
  }

  # 远程调试 Agent 会频繁 pull/push 日志。现场电脑断电、拔 USB 或关闭窗口时，
  # Git 可能留下 rebase-merge / rebase-apply 临时目录，之后每次 pull 都会失败。
  # 对这个教学项目来说，现场本地只应产生日志提交；先 abort 可以回到可同步状态，
  # 避免因为 Git 的中间状态而丢失后续硬件调试日志。
  Write-AgentLog "git_rebase_state_found action=abort"
  & git -C $WorkDir rebase --abort 2>$null
  if ($LASTEXITCODE -eq 0) {
    Write-AgentLog "git_rebase_abort_ok"
    return
  }

  # 有时 Git 已经没有可 abort 的 rebase，但临时目录还留着。此时只删除 Git
  # 的 rebase 状态目录，不删除项目文件和日志文件。
  if (Test-Path $rebaseMerge) {
    Remove-Item -LiteralPath $rebaseMerge -Recurse -Force -ErrorAction SilentlyContinue
  }
  if (Test-Path $rebaseApply) {
    Remove-Item -LiteralPath $rebaseApply -Recurse -Force -ErrorAction SilentlyContinue
  }
  Write-AgentLog "git_rebase_state_removed"
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
    Write-AgentLog "git_index_lock_present action=wait age_s=$ageSeconds"
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
    Write-AgentLog "git_index_lock_present action=keep running_git=$($runningGit.Count) age_s=$ageSeconds"
    return
  }

  # index.lock 是 Git 为保护索引文件创建的临时锁。现场如果强行关闭窗口，
  # 锁文件可能留下来，之后 git pull/add/commit 都会失败。这里确认锁文件
  # 已经不是刚创建的、且没有正在运行的 Git 进程后才删除它。
  Remove-Item -LiteralPath $lockPath -Force -ErrorAction SilentlyContinue
  Write-AgentLog "git_index_lock_removed age_s=$ageSeconds path=$lockPath"
}

function Remove-OversizedDebugLogs {
  if ([string]::IsNullOrWhiteSpace($DeviceLogDir) -or -not (Test-Path $DeviceLogDir)) {
    return
  }

  $maxBytes = [int64]$Config.maxGitLogFileBytes
  if ($maxBytes -le 0) {
    $maxBytes = 8388608
  }

  Get-ChildItem -LiteralPath $DeviceLogDir -Recurse -File -ErrorAction SilentlyContinue |
    Where-Object { $_.Length -gt $maxBytes } |
    ForEach-Object {
      # GitHub 单文件硬限制是 100MB。现场调试日志如果异常膨胀，继续提交只会
      # 让 push 永远失败。这里直接删除超限日志，让 Agent 保持可远程更新和可上传
      # 后续小日志；真正的历史大日志不作为本项目的调试目标。
      Write-Host "Removing oversized debug log from Git sync: $($_.FullName) bytes=$($_.Length)"
      Remove-Item -LiteralPath $_.FullName -Force -ErrorAction SilentlyContinue
    }
}

function Invoke-GitSync {
  param([string]$Message)

  $git = Get-Command git -ErrorAction SilentlyContinue
  if ($null -eq $git) {
    Write-Warning "git command not found; logs stay local."
    return
  }

  Repair-StaleGitRebase -WorkDir $RepoRoot
  Repair-StaleGitIndexLock -WorkDir $RepoRoot
  & git -C $RepoRoot pull --rebase --autostash
  if ($LASTEXITCODE -ne 0) {
    Write-Warning "git pull failed; continuing local logging."
  }
  Test-AgentSelfUpdate | Out-Null

  Ensure-GitIdentity

  Remove-OversizedDebugLogs

  Repair-StaleGitIndexLock -WorkDir $RepoRoot
  & git -C $RepoRoot add -- $RelDeviceLogDir
  if ($LASTEXITCODE -ne 0) {
    Write-Warning "git add failed."
    return
  }

  & git -C $RepoRoot diff --cached --quiet -- $RelDeviceLogDir
  if ($LASTEXITCODE -eq 0) {
    return
  }

  & git -C $RepoRoot commit -m $Message
  if ($LASTEXITCODE -ne 0) {
    Write-Warning "git commit failed."
    return
  }

  if ($Config.autoPush -eq $true) {
    & git -C $RepoRoot push
    if ($LASTEXITCODE -ne 0) {
      Write-Warning "git push failed."
    }
  }
}

function Ensure-GitIdentity {
  $name = [string]$Config.gitUserName
  $email = [string]$Config.gitUserEmail
  if ([string]::IsNullOrWhiteSpace($name)) {
    $name = "Smart Door Eye Remote Agent"
  }
  if ([string]::IsNullOrWhiteSpace($email)) {
    $email = "smart-door-eye-agent@example.invalid"
  }

  $currentName = (& git -C $RepoRoot config user.name 2>$null)
  if ([string]::IsNullOrWhiteSpace($currentName)) {
    & git -C $RepoRoot config user.name $name
    Write-AgentLog "git_config_set key=user.name value=`"$name`""
  }

  $currentEmail = (& git -C $RepoRoot config user.email 2>$null)
  if ([string]::IsNullOrWhiteSpace($currentEmail)) {
    & git -C $RepoRoot config user.email $email
    Write-AgentLog "git_config_set key=user.email value=`"$email`""
  }
}

function Write-AgentLog {
  param([string]$Line)
  $out = "$(Get-Date -Format o) [AGENT] $Line"
  # PowerShell 函数会把没有接住的管道输出当作“返回值”。这个 Agent
  # 有些函数会一边写日志一边返回 COM 口名，例如 Resolve-XiaoPort 返回
  # "COM7" 给 esptool 的 --port 参数。如果这里直接 Tee-Object，日志文本
  # 会混进返回值，导致刷写命令把整行日志当成端口名。这里显式写文件和
  # 控制台，不向函数调用者返回任何内容。
  Add-Content -Encoding UTF8 -LiteralPath $AgentLogFile -Value $out
  Write-Host $out
}

function Get-SerialPortInventory {
  $portNames = @()
  try {
    $portNames = [System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object
  } catch {
    Write-AgentLog "serial_list_failed error=$($_.Exception.Message)"
    return @()
  }

  $pnpEntities = @()
  try {
    # 这里读取 Windows 设备管理器里的友好名称，例如
    # "USB Serial Device (COM7)"。这些名称能帮助 Agent 避开蓝牙虚拟串口，
    # 在远程调试时自动找回 XIAO ESP32C6 的 USB 调试口。
    $pnpEntities = Get-CimInstance Win32_PnPEntity -ErrorAction SilentlyContinue |
      Where-Object { $_.Name -match '\(COM\d+\)' }
  } catch {
    $pnpEntities = @()
  }

  $inventory = @()
  foreach ($portName in $portNames) {
    $friendlyName = ""
    $matchPattern = "\($([regex]::Escape($portName))\)"
    $entity = $pnpEntities | Where-Object { $_.Name -match $matchPattern } | Select-Object -First 1
    if ($null -ne $entity) {
      $friendlyName = [string]$entity.Name
    }
    $inventory += [pscustomobject]@{
      Port = $portName
      Name = $friendlyName
    }
  }
  return $inventory
}

function Format-SerialPortInventory {
  param([object[]]$Inventory)
  if ($null -eq $Inventory -or $Inventory.Count -eq 0) {
    return "none"
  }
  return (($Inventory | ForEach-Object {
    $name = [string]$_.Name
    if ([string]::IsNullOrWhiteSpace($name)) {
      "$($_.Port):unknown"
    } else {
      "$($_.Port):$($name -replace '\s+', '_')"
    }
  }) -join ",")
}

function Resolve-XiaoPort {
  param([string]$PreferredPort)

  $autoDetect = $Config.autoDetectXiaoPort -ne $false
  $inventory = @(Get-SerialPortInventory)
  $ports = @($inventory | ForEach-Object { $_.Port })

  if (-not [string]::IsNullOrWhiteSpace($PreferredPort) -and
      $PreferredPort.ToUpperInvariant() -ne "AUTO" -and
      ($ports -contains $PreferredPort)) {
    return $PreferredPort
  }

  if (-not $autoDetect) {
    return $PreferredPort
  }

  $usablePorts = @($inventory | Where-Object {
    $name = [string]$_.Name
    # 现场电脑经常有 COM3/COM4/COM5/COM6 这类蓝牙虚拟串口。
    # 它们能被 Windows 列出来，但不是 XIAO 的 USB 调试口，自动选择时要排除。
    $name -notmatch 'Bluetooth|蓝牙'
  })

  $priorityPorts = @($usablePorts | Where-Object {
    $name = [string]$_.Name
    $name -match 'XIAO|ESP32|USB|Serial|串行|CP210|CH340|CH910|UART|CDC|JTAG'
  })

  $selected = $null
  if ($priorityPorts.Count -gt 0) {
    $selected = $priorityPorts | Select-Object -First 1
  } elseif ($usablePorts.Count -eq 1) {
    $selected = $usablePorts | Select-Object -First 1
  }

  if ($null -ne $selected) {
    $inventoryText = Format-SerialPortInventory -Inventory $inventory
    Write-AgentLog "serial_auto_detect preferred=$PreferredPort selected=$($selected.Port) inventory=$inventoryText"
    return [string]$selected.Port
  }

  $inventoryText = Format-SerialPortInventory -Inventory $inventory
  Write-AgentLog "serial_auto_detect_failed preferred=$PreferredPort inventory=$inventoryText"
  return $PreferredPort
}

function Get-AgentScriptHash {
  if ([string]::IsNullOrWhiteSpace($AgentScriptPath) -or -not (Test-Path $AgentScriptPath)) {
    return ""
  }
  return (Get-FileHash -Algorithm SHA256 -LiteralPath $AgentScriptPath).Hash
}

function Test-AgentSelfUpdate {
  $autoRestart = $Config.autoRestartOnAgentUpdate -ne $false
  if (-not $autoRestart -or $script:AgentRestartRequested) {
    return $false
  }
  $currentHash = Get-AgentScriptHash
  if (-not [string]::IsNullOrWhiteSpace($script:InitialAgentScriptHash) -and
      -not [string]::IsNullOrWhiteSpace($currentHash) -and
      $currentHash -ne $script:InitialAgentScriptHash) {
    $script:AgentRestartRequested = $true
    Write-AgentLog "agent_update_detected action=restart script=$AgentScriptPath"
    return $true
  }
  return $false
}

function ConvertTo-ProcessArgument {
  param([string]$Value)
  return '"' + ($Value -replace '"', '\"') + '"'
}

function Start-AgentReplacement {
  $powershellPath = (Get-Process -Id $PID).Path
  if ([string]::IsNullOrWhiteSpace($powershellPath)) {
    $powershellPath = Join-Path $PSHOME "powershell.exe"
  }

  # The bootstrap wrapper normally restarts the agent in the same console.
  # This fallback matters when an older bootstrap package starts a newer agent:
  # the agent can still relaunch itself after pulling an updated script.
  $argumentList = @(
    "-NoProfile",
    "-ExecutionPolicy",
    "Bypass",
    "-File",
    (ConvertTo-ProcessArgument $AgentScriptPath),
    "-ConfigPath",
    (ConvertTo-ProcessArgument $ConfigPath)
  ) -join " "
  Start-Process -FilePath $powershellPath -ArgumentList $argumentList -WorkingDirectory $PackageRoot -WindowStyle Hidden
  Write-AgentLog "agent_replacement_started mode=direct script=$AgentScriptPath"
}

function Get-CommandFile {
  $commandPath = Join-Path $RepoRoot (Join-Path ([string]$Config.commandDir) (Join-Path $DeviceId "command.json"))
  if (Test-Path $commandPath) {
    return $commandPath
  }
  return $null
}

function Get-FirmwarePathFromCommand {
  param([object]$Command, [string]$DefaultPath)
  $firmware = [string]$Command.firmware
  if ([string]::IsNullOrWhiteSpace($firmware)) {
    $firmware = $DefaultPath
  }
  $resolved = Resolve-RepoPath $firmware
  if (-not (Test-Path $resolved)) {
    throw "Firmware not found: $resolved"
  }
  return $resolved
}

function Invoke-EsptoolToLog {
  param(
    [string[]]$ToolArgs,
    [string]$ResultLog
  )
  $esptool = Join-Path $PackageRoot "tools\esptool\esptool.exe"
  if (-not (Test-Path $esptool)) {
    throw "esptool.exe not found: $esptool"
  }
  ">>> esptool $($ToolArgs -join ' ')" | Tee-Object -FilePath $ResultLog -Append | Out-Null
  & $esptool @ToolArgs *>&1 | Tee-Object -FilePath $ResultLog -Append | Out-Null
  $toolExitCode = [int]$LASTEXITCODE
  return $toolExitCode
}

function Close-XiaoSerial {
  if ($null -ne $script:Serial -and $script:Serial.IsOpen) {
    $script:Serial.Close()
  }
}

function Open-XiaoSerial {
  param(
    [int]$Retries = 1,
    [int]$DelaySeconds = 3
  )
  Close-XiaoSerial
  for ($attempt = 1; $attempt -le $Retries; $attempt++) {
    try {
      $resolvedPort = Resolve-XiaoPort -PreferredPort $script:XiaoPort
      if (-not [string]::IsNullOrWhiteSpace($resolvedPort)) {
        $script:XiaoPort = $resolvedPort
      }
      $script:Serial = New-Object System.IO.Ports.SerialPort $script:XiaoPort, $XiaoBaud, "None", 8, "One"
      $script:Serial.Encoding = [System.Text.Encoding]::ASCII
      $script:Serial.ReadTimeout = 1000
      $script:Serial.DtrEnable = $true
      $script:Serial.RtsEnable = $true
      $script:Serial.Open()
      Write-AgentLog "serial_open port=$script:XiaoPort baud=$XiaoBaud attempt=$attempt"
      return $true
    } catch {
      Write-AgentLog "serial_open_failed port=$script:XiaoPort attempt=$attempt error=$($_.Exception.Message)"
      Close-XiaoSerial
      if ($attempt -lt $Retries) {
        Start-Sleep -Seconds $DelaySeconds
      }
    }
  }
  return $false
}

function Invoke-FlashCommand {
  param([object]$Command)

  $id = [string]$Command.id
  if ([string]::IsNullOrWhiteSpace($id)) {
    throw "command.id is required"
  }
  $action = [string]$Command.action
  if ([string]::IsNullOrWhiteSpace($action)) {
    throw "command.action is required"
  }

  $resultDir = Join-Path $DeviceLogDir "command-results"
  New-Item -ItemType Directory -Force $resultDir | Out-Null
  $safeId = $id -replace '[^A-Za-z0-9_.-]', '_'
  $resultLog = Join-Path $resultDir "$safeId-$action.log"

  "command_id=$id" | Set-Content -Encoding UTF8 $resultLog
  "action=$action" | Add-Content -Encoding UTF8 $resultLog
  "started=$(Get-Date -Format o)" | Add-Content -Encoding UTF8 $resultLog

  $port = [string]$Command.port
  $erase = $Command.erase -ne $false
  $exitCode = 1

  if ($action -eq "flash_main") {
    if ($Config.allowFlashMain -ne $true) { throw "flash_main disabled by config" }
    if ([string]::IsNullOrWhiteSpace($port)) { $port = Resolve-XiaoPort -PreferredPort $script:XiaoPort }
    $fw = Get-FirmwarePathFromCommand -Command $Command -DefaultPath ([string]$Config.defaultMainFirmware)
    Close-XiaoSerial
    try {
      if ($erase) {
        $exitCode = Invoke-EsptoolToLog -ToolArgs @("--chip","esp32c6","--port",$port,"--baud","921600","--before","default-reset","--after","hard-reset","erase-flash") -ResultLog $resultLog
        if ($exitCode -ne 0) { throw "erase-flash failed exit=$exitCode" }
      }
      $exitCode = Invoke-EsptoolToLog -ToolArgs @("--chip","esp32c6","--port",$port,"--baud","921600","--before","default-reset","--after","hard-reset","write-flash","-z","--flash-mode","dio","--flash-freq","80m","--flash-size","4MB","0x0",$fw) -ResultLog $resultLog
    } finally {
      Open-XiaoSerial -Retries 10 -DelaySeconds 2 | Out-Null
    }
  } elseif ($action -eq "flash_xvf_test") {
    if ($Config.allowFlashXvfTest -ne $true) { throw "flash_xvf_test disabled by config" }
    if ([string]::IsNullOrWhiteSpace($port)) { $port = Resolve-XiaoPort -PreferredPort $script:XiaoPort }
    $fw = Get-FirmwarePathFromCommand -Command $Command -DefaultPath ([string]$Config.defaultXvfTestFirmware)
    Close-XiaoSerial
    try {
      if ($erase) {
        $exitCode = Invoke-EsptoolToLog -ToolArgs @("--chip","esp32c6","--port",$port,"--baud","921600","--before","default-reset","--after","hard-reset","erase-flash") -ResultLog $resultLog
        if ($exitCode -ne 0) { throw "erase-flash failed exit=$exitCode" }
      }
      $exitCode = Invoke-EsptoolToLog -ToolArgs @("--chip","esp32c6","--port",$port,"--baud","921600","--before","default-reset","--after","hard-reset","write-flash","-z","--flash-mode","dio","--flash-freq","80m","--flash-size","4MB","0x0",$fw) -ResultLog $resultLog
    } finally {
      Open-XiaoSerial -Retries 10 -DelaySeconds 2 | Out-Null
    }
  } elseif ($action -eq "flash_cam") {
    if ($Config.allowFlashCam -ne $true) { throw "flash_cam disabled by config" }
    if ([string]::IsNullOrWhiteSpace($port)) { $port = [string]$Config.camPort }
    if ([string]::IsNullOrWhiteSpace($port)) { throw "cam port is required" }
    $fw = Get-FirmwarePathFromCommand -Command $Command -DefaultPath ([string]$Config.defaultCamFirmware)
    if ($erase) {
      $exitCode = Invoke-EsptoolToLog -ToolArgs @("--chip","esp32","--port",$port,"--baud","460800","--before","default-reset","--after","hard-reset","erase-flash") -ResultLog $resultLog
      if ($exitCode -ne 0) { throw "erase-flash failed exit=$exitCode" }
    }
    $exitCode = Invoke-EsptoolToLog -ToolArgs @("--chip","esp32","--port",$port,"--baud","460800","--before","default-reset","--after","hard-reset","write-flash","-z","--flash-mode","dio","--flash-freq","40m","--flash-size","4MB","0x0",$fw) -ResultLog $resultLog
  } else {
    throw "unsupported action: $action"
  }

  "finished=$(Get-Date -Format o)" | Add-Content -Encoding UTF8 $resultLog
  "exitCode=$exitCode" | Add-Content -Encoding UTF8 $resultLog
  if ($exitCode -ne 0) {
    throw "flash command failed exit=$exitCode"
  }
  return $resultLog
}

function Poll-RemoteCommand {
  try {
    Repair-StaleGitRebase -WorkDir $RepoRoot
    Repair-StaleGitIndexLock -WorkDir $RepoRoot
    & git -C $RepoRoot pull --rebase --autostash
    if (Test-AgentSelfUpdate) {
      return
    }
    $commandFile = Get-CommandFile
    if ($null -eq $commandFile) {
      return
    }
    $command = Get-Content -Raw -Encoding UTF8 $commandFile | ConvertFrom-Json
    $id = [string]$command.id
    if ([string]::IsNullOrWhiteSpace($id)) {
      Write-AgentLog "command_ignored reason=missing_id file=$commandFile"
      return
    }
    if (Test-Path $LastCommandFile) {
      $last = Get-Content -Raw -Encoding UTF8 $LastCommandFile
      if ($last.Trim() -eq $id) {
        return
      }
    }

    Write-AgentLog "command_start id=$id action=$($command.action)"
    try {
      $result = Invoke-FlashCommand -Command $command
      $id | Set-Content -Encoding UTF8 $LastCommandFile
      Write-AgentLog "command_ok id=$id result=$result"
    } catch {
      Write-AgentLog "command_failed id=$id error=$($_.Exception.Message)"
      $id | Set-Content -Encoding UTF8 $LastCommandFile
    }
    Invoke-GitSync -Message "Remote debug command result $DeviceId $id"
  } catch {
    Write-AgentLog "command_poll_failed error=$($_.Exception.Message)"
  }
}

$DeviceId = [string]$Config.deviceId
if ([string]::IsNullOrWhiteSpace($DeviceId)) { $DeviceId = "unknown-device" }
$safeDevice = $DeviceId -replace '[^A-Za-z0-9_.-]', '_'

$repoRootConfig = [string]$Config.repoRoot
if ([string]::IsNullOrWhiteSpace($repoRootConfig)) {
  $repoRootConfig = Find-GitRoot -StartPath $PackageRoot
}
if ([string]::IsNullOrWhiteSpace($repoRootConfig)) {
  throw "repoRoot is empty and no parent .git directory was found. Edit remote_agent_config.json."
}
$RepoRoot = (Resolve-Path $repoRootConfig).Path

$script:XiaoPort = [string]$Config.xiaoPort
if ([string]::IsNullOrWhiteSpace($script:XiaoPort)) { $script:XiaoPort = "AUTO" }
$XiaoBaud = [int]$Config.xiaoBaud
if ($XiaoBaud -le 0) { $XiaoBaud = 115200 }

$RelDeviceLogDir = Join-Path ([string]$Config.logDir) $safeDevice
$DeviceLogDir = Join-Path $RepoRoot $RelDeviceLogDir
New-Item -ItemType Directory -Force $DeviceLogDir | Out-Null

$sessionStamp = Get-Date -Format "yyyyMMdd_HHmmss"
$SerialLogFile = Join-Path $DeviceLogDir "$sessionStamp-live-XIAO-$($script:XiaoPort -replace '[^A-Za-z0-9_.-]', '_').log"
$AgentLogFile = Join-Path $DeviceLogDir "$sessionStamp-agent.log"
$LastCommandFile = Join-Path $DeviceLogDir "last_command_id.txt"
$script:InitialAgentScriptHash = Get-AgentScriptHash

$LogPushInterval = [int]$Config.logPushIntervalSeconds
if ($LogPushInterval -le 0) { $LogPushInterval = 60 }
$CommandPollInterval = [int]$Config.commandPollIntervalSeconds
if ($CommandPollInterval -le 0) { $CommandPollInterval = 20 }

Write-Host ""
Write-Host "=== Remote Debug Agent ==="
Write-Host "deviceId=$DeviceId"
Write-Host "repoRoot=$RepoRoot"
Write-Host "xiaoPort=$script:XiaoPort"
Write-Host "autoDetectXiaoPort=$($Config.autoDetectXiaoPort -ne $false)"
Write-Host "serialLog=$SerialLogFile"
Write-Host "agentLog=$AgentLogFile"
Write-Host "commandPath=$(Join-Path $RepoRoot (Join-Path ([string]$Config.commandDir) (Join-Path $DeviceId 'command.json')))"
Write-Host "Press Ctrl+C to stop."
Write-Host ""

Write-AgentLog "agent_start deviceId=$DeviceId repoRoot=$RepoRoot xiaoPort=$script:XiaoPort autoDetectXiaoPort=$($Config.autoDetectXiaoPort -ne $false)"
if (-not (Open-XiaoSerial -Retries 1)) {
  # 不在启动阶段死等串口。远程调试最怕现场 USB 暂时断开后，
  # Agent 也停止拉取命令和上传日志。主循环会继续定期重试串口，
  # 同时保持 Git 同步，让我们仍能看到“现在没有 COM 口”这个事实。
  Write-Host "XIAO serial is not open yet; the agent will keep syncing Git and retrying."
  Write-AgentLog "serial_initial_open_pending port=$script:XiaoPort"
}

$nextPushAt = (Get-Date).AddSeconds($LogPushInterval)
$nextCommandAt = (Get-Date).AddSeconds(3)

try {
  "=== remote agent serial log started $(Get-Date -Format o), deviceId=$DeviceId, port=$script:XiaoPort, baud=$XiaoBaud ===" | Tee-Object -FilePath $SerialLogFile -Append
  while ($true) {
    if ($null -eq $Serial -or -not $Serial.IsOpen) {
      Open-XiaoSerial -Retries 1 | Out-Null
      if ($null -eq $Serial -or -not $Serial.IsOpen) {
        Start-Sleep -Seconds 3
      }
    } else {
      try {
        $line = $Serial.ReadLine()
        $line = $line.TrimEnd("`r", "`n")
        "$(Get-Date -Format o) $line" | Tee-Object -FilePath $SerialLogFile -Append
      } catch [System.TimeoutException] {
        # Allows periodic git sync and command polling.
      } catch {
        Write-AgentLog "serial_read_failed port=$script:XiaoPort error=$($_.Exception.Message)"
        Close-XiaoSerial
        Start-Sleep -Seconds 2
      }
    }

    $now = Get-Date
    if ($now -ge $nextCommandAt) {
      $nextCommandAt = $now.AddSeconds($CommandPollInterval)
      Poll-RemoteCommand
      if ($script:AgentRestartRequested) {
        break
      }
    }
    if ($now -ge $nextPushAt) {
      $nextPushAt = $now.AddSeconds($LogPushInterval)
      Invoke-GitSync -Message "Remote debug live log $DeviceId $sessionStamp"
      if ($script:AgentRestartRequested) {
        break
      }
    }
  }
} finally {
  if ($script:AgentRestartRequested) {
    Write-AgentLog "agent_stop reason=self_update"
  } else {
    Write-AgentLog "agent_stop"
  }
  Close-XiaoSerial
  Invoke-GitSync -Message "Remote debug final log $DeviceId $sessionStamp"
}

if ($script:AgentRestartRequested) {
  if ($env:SMART_DOOR_EYE_AGENT_WRAPPER -ne "1") {
    Start-AgentReplacement
  }
  exit 75
}

