param(
  [switch]$RunClone
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$PackageDir = Join-Path $RepoRoot "release\smart-door-eye-new-git-agent-20260503"
$ZipPath = Join-Path $RepoRoot "release\smart-door-eye-new-git-agent-20260503-fixed.zip"

function Assert-True {
  param([bool]$Condition, [string]$Message)
  if (-not $Condition) {
    throw $Message
  }
}

function Assert-File {
  param([string]$Path)
  Assert-True -Condition (Test-Path $Path) -Message "Missing file: $Path"
}

function Test-PowerShellSyntax {
  param([string]$Path)
  $tokens = $null
  $errors = $null
  [System.Management.Automation.Language.Parser]::ParseFile($Path, [ref]$tokens, [ref]$errors) | Out-Null
  if ($errors.Count -gt 0) {
    $messages = ($errors | ForEach-Object { "line $($_.Extent.StartLineNumber): $($_.Message)" }) -join "; "
    throw "PowerShell syntax error in ${Path}: $messages"
  }
}

function Get-FunctionSource {
  param([string]$Path, [string]$Name)
  $tokens = $null
  $errors = $null
  $ast = [System.Management.Automation.Language.Parser]::ParseFile($Path, [ref]$tokens, [ref]$errors)
  if ($errors.Count -gt 0) {
    throw "Cannot parse ${Path} while extracting function ${Name}."
  }
  $fn = $ast.Find({
    param($node)
    $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $Name
  }, $true)
  if ($null -eq $fn) {
    throw "Function not found: $Name"
  }
  return $fn.Extent.Text
}

function Test-AgentPortReturnIsClean {
  param([string]$AgentPath)

  Write-Host "Testing Agent auto-detect return value is not polluted by logs..."
  $agentText = Get-Content -Raw -Encoding UTF8 $AgentPath
  Assert-True -Condition ($agentText -match 'Write-Host \$out') -Message "Write-AgentLog must print to console explicitly."
  Assert-True -Condition ($agentText -notmatch '\$out\s*\|\s*Tee-Object\s+-FilePath\s+\$AgentLogFile\s+-Append\s*(\r?\n\s*)?}') -Message "Write-AgentLog must not return Tee-Object output."
  Assert-True -Condition ($agentText -match 'Invoke-XiaoEsp32C6FlashToLog') -Message "Agent must use XIAO ESP32C6 flash reset fallback."
  Assert-True -Condition ($agentText -match 'usb-reset') -Message "Agent must try usb-reset for XIAO ESP32C6 native USB flashing."
  Assert-True -Condition ($agentText -notmatch 'Tee-Object\s+-FilePath\s+\$ResultLog') -Message "esptool result logs must stay plain UTF-8 text."

  $tempDir = Join-Path "C:\tmp" ("smart-door-eye-agent-return-test-" + (Get-Date -Format "yyyyMMddHHmmssfff"))
  New-Item -ItemType Directory -Force $tempDir | Out-Null
  try {
    $tempScript = Join-Path $tempDir "test_agent_port_return.ps1"
    $tempLog = Join-Path $tempDir "agent.log"
    $escapedLog = $tempLog -replace "'", "''"
    $scriptText = @"
`$ErrorActionPreference = "Stop"
`$AgentLogFile = '$escapedLog'
`$Config = [pscustomobject]@{ autoDetectXiaoPort = `$true }
$(Get-FunctionSource -Path $AgentPath -Name "Write-AgentLog")
$(Get-FunctionSource -Path $AgentPath -Name "Format-SerialPortInventory")
function Get-SerialPortInventory {
  return @([pscustomobject]@{ Port = "COM7"; Name = "USB Serial Device (COM7)" })
}
$(Get-FunctionSource -Path $AgentPath -Name "Resolve-XiaoPort")
`$result = @(Resolve-XiaoPort -PreferredPort "AUTO")
if (`$result.Count -ne 1 -or `$result[0] -ne "COM7") {
  throw "Resolve-XiaoPort returned polluted output: [`$(`$result -join '|')]"
}
"@
    Set-Content -Encoding UTF8 -LiteralPath $tempScript -Value $scriptText
    & powershell -NoProfile -ExecutionPolicy Bypass -File $tempScript
    if ($LASTEXITCODE -ne 0) {
      throw "Agent port return test failed, exit=$LASTEXITCODE"
    }
  } finally {
    if (Test-Path $tempDir) {
      Remove-Item -LiteralPath $tempDir -Recurse -Force -ErrorAction SilentlyContinue
    }
  }
}

Write-Host "Testing package files..."
Assert-File (Join-Path $PackageDir "start_remote_debug.cmd")
Assert-File (Join-Path $PackageDir "start_new_git_agent.ps1")
Assert-File (Join-Path $PackageDir "remote_debug_agent.ps1")
Assert-File (Join-Path $PackageDir "README_field_steps.md")
Assert-File $ZipPath

Write-Host "Testing PowerShell syntax..."
Test-PowerShellSyntax (Join-Path $PackageDir "start_new_git_agent.ps1")
Test-PowerShellSyntax (Join-Path $PackageDir "remote_debug_agent.ps1")
Test-AgentPortReturnIsClean (Join-Path $PackageDir "remote_debug_agent.ps1")

Write-Host "Testing Git wrapper parameter names..."
$launcherText = Get-Content -Raw -Encoding UTF8 (Join-Path $PackageDir "start_new_git_agent.ps1")
Assert-True -Condition ($launcherText -match 'param\(\[string\[\]\]\$GitArgs\)') -Message "Invoke-Git must use GitArgs parameter."
Assert-True -Condition ($launcherText -notmatch '@Args\b') -Message "Invoke-Git must not splat PowerShell built-in Args."
Assert-True -Condition ($launcherText -notmatch '\[string\[\]\]\$Args\b') -Message "Invoke-Git must not declare Args parameter."

Write-Host "Testing ZIP expands with required files..."
$tempRoot = Join-Path "C:\tmp" ("smart-door-eye-package-test-" + (Get-Date -Format "yyyyMMddHHmmss"))
if (Test-Path $tempRoot) {
  Remove-Item -LiteralPath $tempRoot -Recurse -Force
}
New-Item -ItemType Directory -Force $tempRoot | Out-Null
try {
  Expand-Archive -LiteralPath $ZipPath -DestinationPath $tempRoot -Force
  $expandedDir = Join-Path $tempRoot "smart-door-eye-new-git-agent-20260503"
  Assert-File (Join-Path $expandedDir "start_remote_debug.cmd")
  Assert-File (Join-Path $expandedDir "start_new_git_agent.ps1")
  Assert-File (Join-Path $expandedDir "remote_debug_agent.ps1")

  if ($RunClone) {
    Write-Host "Testing real Git clone into temporary directory..."
    $cloneDir = Join-Path $tempRoot "cloned-project"
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $expandedDir "start_new_git_agent.ps1") `
      -RepoRoot $cloneDir `
      -XiaoPort "AUTO" `
      -SkipAgentStart
    if ($LASTEXITCODE -ne 0) {
      throw "Package launcher failed during real clone test, exit=$LASTEXITCODE"
    }

    Assert-File (Join-Path $cloneDir ".git")
    Assert-File (Join-Path $cloneDir "test\audio-prompts\field-test-guide\02_press_doorbell.mp3")
    Assert-File (Join-Path $cloneDir "test\face-test-images\README.md")
    Assert-File (Join-Path $cloneDir "release\smart-door-eye-remote-debug-20260502\firmware\main_xiao_esp32c6\smartdooreye.ino.merged.bin")
    Assert-File (Join-Path $cloneDir "release\smart-door-eye-remote-debug-20260502\remote_agent_config.new_git_package.json")

    $runtimeConfig = Get-Content -Raw -Encoding UTF8 (Join-Path $cloneDir "release\smart-door-eye-remote-debug-20260502\remote_agent_config.new_git_package.json") | ConvertFrom-Json
    Assert-True -Condition ([string]$runtimeConfig.repoRoot -eq $cloneDir) -Message "Runtime config repoRoot does not match clone directory."
    Assert-True -Condition ([string]$runtimeConfig.defaultMainFirmware -match 'smartdooreye\.ino\.merged\.bin$') -Message "Runtime config does not point at main firmware."
  }
} finally {
  if (Test-Path $tempRoot) {
    Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
  }
}

Write-Host "PASS: new Git Agent package test complete."
