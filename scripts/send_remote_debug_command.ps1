param(
  [string]$DeviceId = "smart-door-eye-lab-01",
  [ValidateSet("flash_main", "flash_xvf_test", "flash_cam")]
  [string]$Action = "flash_main",
  [string]$Port = "",
  [string]$Firmware = "",
  [bool]$Erase = $true,
  [switch]$AllowCam,
  [switch]$NoPush
)

$ErrorActionPreference = "Stop"

function Invoke-GitChecked {
  param([string[]]$GitArgs, [string]$Message)
  Write-Host "git $($GitArgs -join ' ')"
  & git @GitArgs
  if ($LASTEXITCODE -ne 0) {
    throw "$Message exit=$LASTEXITCODE"
  }
}

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $RepoRoot

Invoke-GitChecked -GitArgs @("pull", "--rebase", "--autostash") -Message "git pull failed"

if ($Action -eq "flash_cam" -and -not $AllowCam) {
  throw "flash_cam is disabled by default because ESP32-CAM usually needs onsite GPIO0/EN bootloader wiring. Re-run with -AllowCam only after onsite preparation."
}

if ([string]::IsNullOrWhiteSpace($Firmware)) {
  if ($Action -eq "flash_main") {
    $Firmware = "release/smart-door-eye-remote-debug-20260502/firmware/main_xiao_esp32c6/smartdooreye.ino.merged.bin"
  } elseif ($Action -eq "flash_xvf_test") {
    $Firmware = "release/smart-door-eye-remote-debug-20260502/firmware/xvf_i2s_test_xiao_esp32c6/sketch_apr18a.ino.merged.bin"
  } else {
    $Firmware = "release/smart-door-eye-remote-debug-20260502/firmware/cam_ai_thinker_esp32cam/CameraWebServer.ino.merged.bin"
  }
}

$firmwarePath = Join-Path $RepoRoot ($Firmware -replace '/', '\')
if (-not (Test-Path $firmwarePath)) {
  throw "Firmware not found: $firmwarePath"
}

$safeDevice = $DeviceId -replace '[^A-Za-z0-9_.-]', '_'
$commandDir = Join-Path $RepoRoot (Join-Path "test\remote-debug-commands" $safeDevice)
New-Item -ItemType Directory -Force $commandDir | Out-Null
$commandPath = Join-Path $commandDir "command.json"

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$commandId = "$stamp-$Action"
$command = [ordered]@{
  id = $commandId
  action = $Action
  firmware = ($Firmware -replace '\\', '/')
  erase = $Erase
}
if (-not [string]::IsNullOrWhiteSpace($Port)) {
  $command.port = $Port
}

$command | ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 $commandPath

Write-Host ""
Write-Host "Remote command written:"
Write-Host "  deviceId=$safeDevice"
Write-Host "  action=$Action"
Write-Host "  firmware=$Firmware"
Write-Host "  port=$Port"
Write-Host "  erase=$Erase"
Write-Host "  path=$commandPath"
Write-Host ""

Invoke-GitChecked -GitArgs @("add", "--", "test/remote-debug-commands/$safeDevice/command.json") -Message "git add failed"
Invoke-GitChecked -GitArgs @("commit", "-m", "Send remote debug command $safeDevice $commandId") -Message "git commit failed"

if (-not $NoPush) {
  Invoke-GitChecked -GitArgs @("push") -Message "git push failed"
}

Write-Host ""
Write-Host "Command id: $commandId"
Write-Host "Agent will run it once, then upload result under:"
Write-Host "  test/remote-debug-logs/$safeDevice/command-results/"
