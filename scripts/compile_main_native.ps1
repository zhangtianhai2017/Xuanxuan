param(
  [string]$Fqbn = "esp32:esp32:XIAO_ESP32C6:PartitionScheme=huge_app"
)

$ErrorActionPreference = "Stop"

# This script compiles the native Arduino sketch directory:
#   arduino/smartdooreye/smartdooreye.ino
#
# Why this avoids the old slow path:
# Arduino requires the sketch directory name to match the .ino file name.
# Previously we copied "main program" files into C:\tmp\smartdooreye before
# compiling. That changed source paths and reduced the value of incremental
# build caches. The new layout keeps a stable source path in the repository.

$RepoRoot = Split-Path -Parent $PSScriptRoot
$SketchDir = Join-Path $RepoRoot "arduino\smartdooreye"
$Cli = if ($env:ARDUINO_CLI) { $env:ARDUINO_CLI } else { Join-Path $RepoRoot "tools\arduino-cli\arduino-cli.exe" }
$Config = Join-Path $RepoRoot "arduino-cli.yaml"
$BuildPath = Join-Path $RepoRoot ".arduino-build\smartdooreye"

if (!(Test-Path -LiteralPath $Cli)) {
  throw "Arduino CLI not found: $Cli. Install/download the build tools, or set ARDUINO_CLI to arduino-cli.exe."
}

if (!(Test-Path -LiteralPath (Join-Path $SketchDir "smartdooreye.ino"))) {
  throw "Sketch is incomplete: $SketchDir. Arduino requires smartdooreye/ and smartdooreye.ino to have the same name."
}

New-Item -ItemType Directory -Force -Path $BuildPath | Out-Null

$StartedAt = Get-Date
Write-Host "SketchDir=$SketchDir"
Write-Host "BuildPath=$BuildPath"
Write-Host "FQBN=$Fqbn"

& $Cli compile `
  --config-file $Config `
  --fqbn $Fqbn `
  --build-path $BuildPath `
  $SketchDir

if ($LASTEXITCODE -ne 0) {
  throw "Arduino compile failed, exit=$LASTEXITCODE"
}

$Elapsed = (Get-Date) - $StartedAt
Write-Host ("compile_seconds={0:N1}" -f $Elapsed.TotalSeconds)
