param(
  [string]$Label = "SERIAL",
  [int]$Baud = 115200
)

$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
$LogDir = Join-Path $Root "logs"
New-Item -ItemType Directory -Force $LogDir | Out-Null

function Get-UploadConfig {
  $configPath = Join-Path $Root "upload_config.json"
  if (-not (Test-Path $configPath)) {
    return $null
  }

  try {
    $config = Get-Content -Raw -Encoding UTF8 $configPath | ConvertFrom-Json
    if ($config.enabled -ne $true) {
      return $null
    }
    if ([string]::IsNullOrWhiteSpace([string]$config.url)) {
      Write-Warning "upload_config.json is enabled but url is empty."
      return $null
    }
    return $config
  } catch {
    Write-Warning "Failed to read upload_config.json: $($_.Exception.Message)"
    return $null
  }
}

function Invoke-LogUpload {
  param(
    [string]$FilePath,
    [object]$Config,
    [string]$Reason
  )

  if ($null -eq $Config -or -not (Test-Path $FilePath)) {
    return
  }

  try {
    $hash = (Get-FileHash -Algorithm SHA256 $FilePath).Hash
    $headers = @{
      "X-Device-Id" = [string]$Config.deviceId
      "X-Log-Label" = [string]$Label
      "X-Log-Reason" = [string]$Reason
      "X-Log-File" = [System.IO.Path]::GetFileName($FilePath)
      "X-Log-Sha256" = $hash
    }
    if (-not [string]::IsNullOrWhiteSpace([string]$Config.authHeaderName)) {
      $headers[[string]$Config.authHeaderName] = [string]$Config.authHeaderValue
    }

    Write-Host "Uploading log ($Reason) to $($Config.url) ..."
    Invoke-WebRequest -Uri ([string]$Config.url) -Method Post -InFile $FilePath -ContentType "text/plain; charset=utf-8" -Headers $headers -TimeoutSec 20 | Out-Null
    Write-Host "Upload OK: $([System.IO.Path]::GetFileName($FilePath)) sha256=$hash"
  } catch {
    Write-Warning "Upload failed: $($_.Exception.Message)"
  }
}

function Get-GitUploadConfig {
  $configPath = Join-Path $Root "git_upload_config.json"
  if (-not (Test-Path $configPath)) {
    return $null
  }

  try {
    $config = Get-Content -Raw -Encoding UTF8 $configPath | ConvertFrom-Json
    if ($config.enabled -ne $true) {
      return $null
    }
    return $config
  } catch {
    Write-Warning "Failed to read git_upload_config.json: $($_.Exception.Message)"
    return $null
  }
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

function Invoke-GitLogUpload {
  param(
    [string]$FilePath,
    [object]$Config
  )

  if ($null -eq $Config -or -not (Test-Path $FilePath)) {
    return
  }

  try {
    $repoRoot = [string]$Config.repoRoot
    if ([string]::IsNullOrWhiteSpace($repoRoot)) {
      $repoRoot = Find-GitRoot -StartPath $Root
    }
    if ([string]::IsNullOrWhiteSpace($repoRoot)) {
      Write-Warning "Git upload enabled, but repoRoot is empty and no parent .git directory was found. Edit git_upload_config.json."
      return
    }
    $repoRoot = (Resolve-Path $repoRoot).Path

    $testLogDir = [string]$Config.testLogDir
    if ([string]::IsNullOrWhiteSpace($testLogDir)) {
      $testLogDir = "test/remote-debug-logs"
    }

    $deviceId = [string]$Config.deviceId
    if ([string]::IsNullOrWhiteSpace($deviceId)) {
      $deviceId = "unknown-device"
    }
    $safeDevice = $deviceId -replace '[^A-Za-z0-9_.-]', '_'
    $safePort = $Port -replace '[^A-Za-z0-9_.-]', '_'
    $targetDir = Join-Path $repoRoot (Join-Path $testLogDir $safeDevice)
    New-Item -ItemType Directory -Force $targetDir | Out-Null

    $targetName = "$(Get-Date -Format yyyyMMdd_HHmmss)-$SafeLabel-$safePort.log"
    $targetFile = Join-Path $targetDir $targetName
    Copy-Item $FilePath $targetFile -Force

    $hash = (Get-FileHash -Algorithm SHA256 $targetFile).Hash
    $metaFile = "$targetFile.meta.txt"
    @(
      "deviceId=$deviceId",
      "label=$Label",
      "port=$Port",
      "baud=$Baud",
      "capturedAt=$(Get-Date -Format o)",
      "sha256=$hash",
      "sourceFile=$FilePath"
    ) | Set-Content -Encoding UTF8 $metaFile

    Write-Host "Copied log into git test directory:"
    Write-Host "  $targetFile"

    $repoFull = (Resolve-Path $repoRoot).Path.TrimEnd('\')
    $relLog = $targetFile.Substring($repoFull.Length + 1)
    $relMeta = $metaFile.Substring($repoFull.Length + 1)

    if ($Config.autoCommit -eq $true) {
      $git = Get-Command git -ErrorAction SilentlyContinue
      if ($null -eq $git) {
        Write-Warning "git command not found. Log was copied, but not committed."
        return
      }

      & git -C $repoRoot add -- $relLog $relMeta
      if ($LASTEXITCODE -ne 0) {
        Write-Warning "git add failed."
        return
      }

      & git -C $repoRoot diff --cached --quiet -- $relLog $relMeta
      if ($LASTEXITCODE -eq 0) {
        Write-Host "No git changes to commit for this log."
      } else {
        $prefix = [string]$Config.commitMessagePrefix
        if ([string]::IsNullOrWhiteSpace($prefix)) {
          $prefix = "Add remote debug log"
        }
        & git -C $repoRoot commit -m "$prefix $targetName"
        if ($LASTEXITCODE -ne 0) {
          Write-Warning "git commit failed. Check git user.name/user.email or repository state."
          return
        }
      }

      if ($Config.autoPush -eq $true) {
        & git -C $repoRoot push
        if ($LASTEXITCODE -ne 0) {
          Write-Warning "git push failed. Check network, branch, and credentials."
          return
        }
        Write-Host "Git push OK."
      } else {
        Write-Host "Git commit OK. autoPush=false, so the log was not pushed."
      }
    }
  } catch {
    Write-Warning "Git log upload failed: $($_.Exception.Message)"
  }
}

Write-Host ""
Write-Host "=== Capture $Label serial log ==="
Write-Host "Available ports:"
$ports = [System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object
if ($ports.Count -eq 0) {
  Write-Host "  No COM ports found."
} else {
  $ports | ForEach-Object { Write-Host "  $_" }
}
Write-Host ""
$Port = Read-Host "Enter COM port, for example COM5"
if ([string]::IsNullOrWhiteSpace($Port)) {
  throw "No COM port entered."
}

$MinutesText = Read-Host "Record how many minutes? Press Enter for 10"
$Minutes = 10
if (-not [string]::IsNullOrWhiteSpace($MinutesText)) {
  $Minutes = [int]$MinutesText
}

$Timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$SafeLabel = $Label -replace '[^A-Za-z0-9_-]', '_'
$LogFile = Join-Path $LogDir "$Timestamp-$SafeLabel-$Port.log"
$UploadConfig = Get-UploadConfig
$GitUploadConfig = Get-GitUploadConfig
$UploadEverySeconds = 0
if ($null -ne $UploadConfig -and $null -ne $UploadConfig.uploadEverySeconds) {
  $UploadEverySeconds = [int]$UploadConfig.uploadEverySeconds
}

Write-Host ""
Write-Host "Saving log to: $LogFile"
if ($null -ne $UploadConfig) {
  Write-Host "Auto upload enabled: $($UploadConfig.url)"
  if ($UploadEverySeconds -gt 0) {
    Write-Host "Periodic upload every $UploadEverySeconds seconds. This uploads the current whole log file."
  } else {
    Write-Host "Periodic upload disabled; final upload will run when capture ends."
  }
} else {
  Write-Host "Auto upload disabled. To enable it, copy upload_config.example.json to upload_config.json and edit it."
}
if ($null -ne $GitUploadConfig) {
  Write-Host "Git log upload enabled: testLogDir=$($GitUploadConfig.testLogDir) autoCommit=$($GitUploadConfig.autoCommit) autoPush=$($GitUploadConfig.autoPush)"
} else {
  Write-Host "Git log upload disabled. To enable it, copy git_upload_config.example.json to git_upload_config.json and edit it."
}
Write-Host "After the port opens, press RESET on the board, then perform the requested test steps."
Write-Host "Press Ctrl+C to stop early."
Write-Host ""

$serial = New-Object System.IO.Ports.SerialPort $Port, $Baud, "None", 8, "One"
$serial.Encoding = [System.Text.Encoding]::ASCII
$serial.ReadTimeout = 1000
$serial.DtrEnable = $true
$serial.RtsEnable = $true

try {
  $serial.Open()
  $endAt = (Get-Date).AddMinutes($Minutes)
  $lastUploadAt = Get-Date
  "=== $Label log started $(Get-Date -Format o), port=$Port, baud=$Baud, minutes=$Minutes ===" | Tee-Object -FilePath $LogFile -Append

  while ((Get-Date) -lt $endAt) {
    try {
      $line = $serial.ReadLine()
      $line = $line.TrimEnd("`r", "`n")
      $out = "$(Get-Date -Format o) $line"
      $out | Tee-Object -FilePath $LogFile -Append
    } catch [System.TimeoutException] {
      # Keep waiting; timeout just lets us check the end time.
    }

    if ($null -ne $UploadConfig -and $UploadEverySeconds -gt 0) {
      if (((Get-Date) - $lastUploadAt).TotalSeconds -ge $UploadEverySeconds) {
        $lastUploadAt = Get-Date
        Invoke-LogUpload -FilePath $LogFile -Config $UploadConfig -Reason "periodic"
      }
    }
  }

  "=== $Label log ended $(Get-Date -Format o) ===" | Tee-Object -FilePath $LogFile -Append
  if ($null -ne $UploadConfig -and ($UploadConfig.uploadAtEnd -ne $false)) {
    Invoke-LogUpload -FilePath $LogFile -Config $UploadConfig -Reason "end"
  }
  if ($null -ne $GitUploadConfig) {
    Invoke-GitLogUpload -FilePath $LogFile -Config $GitUploadConfig
  }
} finally {
  if ($serial.IsOpen) {
    $serial.Close()
  }
}

Write-Host ""
Write-Host "Done. Please send this file back:"
Write-Host $LogFile
