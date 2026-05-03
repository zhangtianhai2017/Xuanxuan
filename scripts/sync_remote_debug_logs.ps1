param(
  [string]$DeviceId = "smart-door-eye-lab-01",
  [int]$Tail = 80,
  [switch]$NoPull
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $RepoRoot

function Repair-StaleGitRebase {
  param([string]$WorkDir)

  $gitDir = Join-Path $WorkDir ".git"
  $rebaseMerge = Join-Path $gitDir "rebase-merge"
  $rebaseApply = Join-Path $gitDir "rebase-apply"
  if (-not ((Test-Path $rebaseMerge) -or (Test-Path $rebaseApply))) {
    return
  }

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

if (-not $NoPull) {
  Write-Host "git pull --rebase --autostash"
  Repair-StaleGitRebase -WorkDir $RepoRoot
  & git pull --rebase --autostash
  if ($LASTEXITCODE -ne 0) {
    throw "git pull failed exit=$LASTEXITCODE"
  }
}

$safeDevice = $DeviceId -replace '[^A-Za-z0-9_.-]', '_'
$logDir = Join-Path $RepoRoot (Join-Path "test\remote-debug-logs" $safeDevice)
if (-not (Test-Path $logDir)) {
  Write-Host "No logs yet for device: $safeDevice"
  Write-Host "Expected directory: $logDir"
  exit 0
}

Write-Host ""
Write-Host "Log directory:"
Write-Host "  $logDir"
Write-Host ""

$latest = Get-ChildItem -File $logDir -Filter "*.log" |
  Sort-Object LastWriteTime -Descending |
  Select-Object -First 1

if ($null -eq $latest) {
  Write-Host "No .log files found yet."
  exit 0
}

Write-Host "Latest log:"
Write-Host "  $($latest.FullName)"
Write-Host ""
Write-Host "Last $Tail lines:"
Write-Host ""
Get-Content -Encoding UTF8 -Path $latest.FullName -Tail $Tail

$resultDir = Join-Path $logDir "command-results"
if (Test-Path $resultDir) {
  $latestResult = Get-ChildItem -File $resultDir -Filter "*.log" |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1
  if ($null -ne $latestResult) {
    Write-Host ""
    Write-Host "Latest command result:"
    Write-Host "  $($latestResult.FullName)"
    Write-Host ""
    Get-Content -Encoding UTF8 -Path $latestResult.FullName -Tail $Tail
  }
}
