param(
  [int]$Port = 8088,
  [string]$OutputDir = "uploaded_logs"
)

$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
$SaveDir = Join-Path $Root $OutputDir
New-Item -ItemType Directory -Force $SaveDir | Out-Null

$listener = New-Object System.Net.HttpListener
$prefix = "http://+:$Port/"
$listener.Prefixes.Add($prefix)

Write-Host ""
Write-Host "=== Simple log upload receiver ==="
Write-Host "Listening on: $prefix"
Write-Host "Upload URL for upload_config.json:"
Write-Host "  http://THIS_COMPUTER_IP:$Port/upload"
Write-Host "Saving logs to:"
Write-Host "  $SaveDir"
Write-Host ""
Write-Host "Press Ctrl+C to stop."
Write-Host ""

$listener.Start()
try {
  while ($listener.IsListening) {
    $ctx = $listener.GetContext()
    $req = $ctx.Request
    $res = $ctx.Response

    try {
      if ($req.HttpMethod -ne "POST" -or $req.Url.AbsolutePath -ne "/upload") {
        $res.StatusCode = 404
        $bytes = [System.Text.Encoding]::UTF8.GetBytes("Use POST /upload")
        $res.OutputStream.Write($bytes, 0, $bytes.Length)
        continue
      }

      $device = $req.Headers["X-Device-Id"]
      if ([string]::IsNullOrWhiteSpace($device)) { $device = "unknown-device" }
      $label = $req.Headers["X-Log-Label"]
      if ([string]::IsNullOrWhiteSpace($label)) { $label = "serial" }
      $reason = $req.Headers["X-Log-Reason"]
      if ([string]::IsNullOrWhiteSpace($reason)) { $reason = "upload" }
      $clientName = ($ctx.Request.RemoteEndPoint.Address.ToString() -replace '[^A-Za-z0-9_.-]', '_')
      $stamp = Get-Date -Format "yyyyMMdd_HHmmss"
      $safeDevice = $device -replace '[^A-Za-z0-9_.-]', '_'
      $safeLabel = $label -replace '[^A-Za-z0-9_.-]', '_'
      $safeReason = $reason -replace '[^A-Za-z0-9_.-]', '_'
      $outFile = Join-Path $SaveDir "$stamp-$safeDevice-$safeLabel-$safeReason-$clientName.log"

      $fs = [System.IO.File]::Open($outFile, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write)
      try {
        $req.InputStream.CopyTo($fs)
      } finally {
        $fs.Close()
      }

      $hash = (Get-FileHash -Algorithm SHA256 $outFile).Hash
      $expectedHash = $req.Headers["X-Log-Sha256"]
      $hashStatus = "no-client-hash"
      if (-not [string]::IsNullOrWhiteSpace($expectedHash)) {
        $hashStatus = if ($hash -eq $expectedHash) { "hash-ok" } else { "hash-mismatch client=$expectedHash server=$hash" }
      }

      Write-Host "$(Get-Date -Format o) saved $outFile bytes=$((Get-Item $outFile).Length) $hashStatus"
      $body = "OK $hashStatus`n$outFile`n"
      $bytes = [System.Text.Encoding]::UTF8.GetBytes($body)
      $res.StatusCode = 200
      $res.ContentType = "text/plain; charset=utf-8"
      $res.OutputStream.Write($bytes, 0, $bytes.Length)
    } catch {
      $res.StatusCode = 500
      $body = "ERROR $($_.Exception.Message)`n"
      $bytes = [System.Text.Encoding]::UTF8.GetBytes($body)
      $res.OutputStream.Write($bytes, 0, $bytes.Length)
      Write-Warning $_.Exception.Message
    } finally {
      $res.OutputStream.Close()
    }
  }
} finally {
  $listener.Stop()
  $listener.Close()
}
