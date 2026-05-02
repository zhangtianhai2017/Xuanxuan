param(
  [Parameter(Mandatory = $true)]
  [string]$FullRepoUrl,

  [Parameter(Mandatory = $true)]
  [string]$StudentRepoUrl,

  [string]$DeviceId = "smart-door-eye-lab-01",
  [string]$BootstrapPort = "COM5",
  [switch]$NoPush
)

$ErrorActionPreference = "Stop"

function Invoke-GitChecked {
  param(
    [string]$WorkDir,
    [string[]]$GitArgs,
    [string]$Message
  )
  Write-Host "[$WorkDir] git $($GitArgs -join ' ')"
  & git -C $WorkDir @GitArgs
  if ($LASTEXITCODE -ne 0) {
    throw "$Message exit=$LASTEXITCODE"
  }
}

function Set-Origin {
  param([string]$WorkDir, [string]$Url)
  $existing = & git -C $WorkDir remote get-url origin 2>$null
  if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($existing)) {
    Invoke-GitChecked -WorkDir $WorkDir -GitArgs @("remote", "set-url", "origin", $Url) -Message "git remote set-url failed"
  } else {
    Invoke-GitChecked -WorkDir $WorkDir -GitArgs @("remote", "add", "origin", $Url) -Message "git remote add failed"
  }
}

function Update-BootstrapConfig {
  param([string]$ConfigPath, [string]$RepoUrl, [string]$DeviceId, [string]$Port)
  $cfg = Get-Content -Raw -Encoding UTF8 $ConfigPath | ConvertFrom-Json
  $cfg.repoUrl = $RepoUrl
  $cfg.deviceId = $DeviceId
  $cfg.xiaoPort = $Port
  $cfg | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 $ConfigPath
}

function Update-Manifest {
  param([string]$Root)
  $manifest = Join-Path $Root "MANIFEST_SHA256.txt"
  Get-ChildItem -Recurse -File $Root |
    Where-Object { $_.FullName -ne (Resolve-Path $manifest -ErrorAction SilentlyContinue).Path } |
    Sort-Object FullName |
    ForEach-Object {
      $hash = (Get-FileHash -Algorithm SHA256 -Path $_.FullName).Hash
      $rel = Resolve-Path -Relative $_.FullName
      "$hash  $rel"
    } | Set-Content -Encoding UTF8 $manifest
}

function Refresh-BootstrapZip {
  param([string]$RepoRoot)
  $zip = Join-Path $RepoRoot "release\smart-door-eye-student-bootstrap-20260502.zip"
  if (Test-Path $zip) {
    Remove-Item -LiteralPath $zip -Force
  }
  Compress-Archive -Path (Join-Path $RepoRoot "release\smart-door-eye-student-bootstrap-20260502") -DestinationPath $zip -CompressionLevel Optimal
  $item = Get-Item $zip
  $hash = (Get-FileHash -Algorithm SHA256 -Path $zip).Hash
  Write-Host "Bootstrap zip: $($item.FullName)"
  Write-Host "Bootstrap zip size: $($item.Length)"
  Write-Host "Bootstrap zip SHA256: $hash"
}

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$StudentRoot = Join-Path $RepoRoot "git_prepared\student-remote-debug"
if (-not (Test-Path (Join-Path $RepoRoot ".git"))) {
  throw "Full project git repository not found: $RepoRoot"
}
if (-not (Test-Path (Join-Path $StudentRoot ".git"))) {
  throw "Student remote-debug git repository not found: $StudentRoot"
}

$fullBootstrap = Join-Path $RepoRoot "release\smart-door-eye-student-bootstrap-20260502\bootstrap_config.json"
$studentBootstrap = Join-Path $StudentRoot "release\smart-door-eye-student-bootstrap-20260502\bootstrap_config.json"
Update-BootstrapConfig -ConfigPath $fullBootstrap -RepoUrl $StudentRepoUrl -DeviceId $DeviceId -Port $BootstrapPort
Update-BootstrapConfig -ConfigPath $studentBootstrap -RepoUrl $StudentRepoUrl -DeviceId $DeviceId -Port $BootstrapPort

Update-Manifest -Root (Join-Path $RepoRoot "release\smart-door-eye-student-bootstrap-20260502")
Update-Manifest -Root (Join-Path $StudentRoot "release\smart-door-eye-student-bootstrap-20260502")
Refresh-BootstrapZip -RepoRoot $RepoRoot

Set-Origin -WorkDir $RepoRoot -Url $FullRepoUrl
Set-Origin -WorkDir $StudentRoot -Url $StudentRepoUrl

Invoke-GitChecked -WorkDir $RepoRoot -GitArgs @("add", ".") -Message "git add full project failed"
& git -C $RepoRoot diff --cached --quiet
if ($LASTEXITCODE -ne 0) {
  Invoke-GitChecked -WorkDir $RepoRoot -GitArgs @("commit", "-m", "Configure public remote repositories") -Message "git commit full project failed"
}

Invoke-GitChecked -WorkDir $StudentRoot -GitArgs @("add", ".") -Message "git add student repo failed"
& git -C $StudentRoot diff --cached --quiet
if ($LASTEXITCODE -ne 0) {
  Invoke-GitChecked -WorkDir $StudentRoot -GitArgs @("commit", "-m", "Configure student bootstrap repository URL") -Message "git commit student repo failed"
}

if (-not $NoPush) {
  Invoke-GitChecked -WorkDir $RepoRoot -GitArgs @("push", "-u", "origin", "main") -Message "push full project failed"
  Invoke-GitChecked -WorkDir $StudentRoot -GitArgs @("push", "-u", "origin", "main") -Message "push student repo failed"
}

Write-Host ""
Write-Host "Done."
Write-Host "Full project repo: $FullRepoUrl"
Write-Host "Student remote-debug repo: $StudentRepoUrl"
