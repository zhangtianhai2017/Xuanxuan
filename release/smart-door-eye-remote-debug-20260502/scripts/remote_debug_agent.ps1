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

function Invoke-GitSync {
  param([string]$Message)

  $git = Get-Command git -ErrorAction SilentlyContinue
  if ($null -eq $git) {
    Write-Warning "git command not found; logs stay local."
    return
  }

  & git -C $RepoRoot pull --rebase --autostash
  if ($LASTEXITCODE -ne 0) {
    Write-Warning "git pull failed; continuing local logging."
  }
  Test-AgentSelfUpdate | Out-Null

  Ensure-GitIdentity

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
  $out | Tee-Object -FilePath $AgentLogFile -Append
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
  ">>> esptool $($ToolArgs -join ' ')" | Tee-Object -FilePath $ResultLog -Append
  & $esptool @ToolArgs *>&1 | Tee-Object -FilePath $ResultLog -Append | Out-Null
  return $LASTEXITCODE
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
      $script:Serial = New-Object System.IO.Ports.SerialPort $XiaoPort, $XiaoBaud, "None", 8, "One"
      $script:Serial.Encoding = [System.Text.Encoding]::ASCII
      $script:Serial.ReadTimeout = 1000
      $script:Serial.DtrEnable = $true
      $script:Serial.RtsEnable = $true
      $script:Serial.Open()
      Write-AgentLog "serial_open port=$XiaoPort baud=$XiaoBaud attempt=$attempt"
      return $true
    } catch {
      Write-AgentLog "serial_open_failed port=$XiaoPort attempt=$attempt error=$($_.Exception.Message)"
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
    if ([string]::IsNullOrWhiteSpace($port)) { $port = $XiaoPort }
    $fw = Get-FirmwarePathFromCommand -Command $Command -DefaultPath ([string]$Config.defaultMainFirmware)
    Close-XiaoSerial
    try {
      if ($erase) {
        $exitCode = Invoke-EsptoolToLog -ToolArgs @("--chip","esp32c6","--port",$port,"--baud","921600","--before","default-reset","--after","hard-reset","erase-flash") -ResultLog $resultLog
        if ($exitCode -ne 0) { throw "erase-flash failed exit=$exitCode" }
      }
      $exitCode = Invoke-EsptoolToLog -ToolArgs @("--chip","esp32c6","--port",$port,"--baud","921600","--before","default-reset","--after","hard-reset","write-flash","-z","--flash-mode","qio","--flash-freq","80m","--flash-size","4MB","0x0",$fw) -ResultLog $resultLog
    } finally {
      Open-XiaoSerial -Retries 10 -DelaySeconds 2 | Out-Null
    }
  } elseif ($action -eq "flash_xvf_test") {
    if ($Config.allowFlashXvfTest -ne $true) { throw "flash_xvf_test disabled by config" }
    if ([string]::IsNullOrWhiteSpace($port)) { $port = $XiaoPort }
    $fw = Get-FirmwarePathFromCommand -Command $Command -DefaultPath ([string]$Config.defaultXvfTestFirmware)
    Close-XiaoSerial
    try {
      if ($erase) {
        $exitCode = Invoke-EsptoolToLog -ToolArgs @("--chip","esp32c6","--port",$port,"--baud","921600","--before","default-reset","--after","hard-reset","erase-flash") -ResultLog $resultLog
        if ($exitCode -ne 0) { throw "erase-flash failed exit=$exitCode" }
      }
      $exitCode = Invoke-EsptoolToLog -ToolArgs @("--chip","esp32c6","--port",$port,"--baud","921600","--before","default-reset","--after","hard-reset","write-flash","-z","--flash-mode","qio","--flash-freq","80m","--flash-size","4MB","0x0",$fw) -ResultLog $resultLog
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

$XiaoPort = [string]$Config.xiaoPort
if ([string]::IsNullOrWhiteSpace($XiaoPort)) { throw "xiaoPort is required in remote_agent_config.json" }
$XiaoBaud = [int]$Config.xiaoBaud
if ($XiaoBaud -le 0) { $XiaoBaud = 115200 }

$RelDeviceLogDir = Join-Path ([string]$Config.logDir) $safeDevice
$DeviceLogDir = Join-Path $RepoRoot $RelDeviceLogDir
New-Item -ItemType Directory -Force $DeviceLogDir | Out-Null

$sessionStamp = Get-Date -Format "yyyyMMdd_HHmmss"
$SerialLogFile = Join-Path $DeviceLogDir "$sessionStamp-live-XIAO-$($XiaoPort -replace '[^A-Za-z0-9_.-]', '_').log"
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
Write-Host "xiaoPort=$XiaoPort"
Write-Host "serialLog=$SerialLogFile"
Write-Host "agentLog=$AgentLogFile"
Write-Host "commandPath=$(Join-Path $RepoRoot (Join-Path ([string]$Config.commandDir) (Join-Path $DeviceId 'command.json')))"
Write-Host "Press Ctrl+C to stop."
Write-Host ""

Write-AgentLog "agent_start deviceId=$DeviceId repoRoot=$RepoRoot xiaoPort=$XiaoPort"
while (-not (Open-XiaoSerial -Retries 1)) {
  Write-Host "Waiting for XIAO serial port $XiaoPort ..."
  Start-Sleep -Seconds 5
}

$nextPushAt = (Get-Date).AddSeconds($LogPushInterval)
$nextCommandAt = (Get-Date).AddSeconds(3)

try {
  "=== remote agent serial log started $(Get-Date -Format o), deviceId=$DeviceId, port=$XiaoPort, baud=$XiaoBaud ===" | Tee-Object -FilePath $SerialLogFile -Append
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
        Write-AgentLog "serial_read_failed port=$XiaoPort error=$($_.Exception.Message)"
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

