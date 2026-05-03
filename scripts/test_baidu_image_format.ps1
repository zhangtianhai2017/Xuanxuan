param(
  [string[]]$Image = @(),
  [ValidateSet("detect", "search", "both")]
  [string]$Mode = "both",
  [string]$GroupId = "visitors",
  [int]$DelayMs = 800,
  [int]$TimeoutSec = 20,
  [string]$ApiKey = "",
  [string]$SecretKey = ""
)

$ErrorActionPreference = "Stop"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

function Get-RepoRoot {
  return (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}

function Get-ProjectCredential {
  param([string]$RepoRoot)

  $sourceFile = Get-ChildItem -Path $RepoRoot -Recurse -Filter "face_recognition.cpp" |
    Where-Object {
      $_.FullName -notmatch '\\release\\' -and
      $_.FullName -notmatch '\\test\\remote-debug-logs\\'
    } |
    Select-Object -First 1

  if ($null -eq $sourceFile) {
    throw "Cannot find face_recognition.cpp under $RepoRoot"
  }

  # This tool is only for local debugging.  It reads the same keys used by the
  # firmware so the PC test and the XIAO firmware test use the same Baidu app.
  $sourcePath = $sourceFile.FullName
  $text = Get-Content -Raw -Encoding UTF8 $sourcePath
  $apiMatch = [regex]::Match($text, 'baidu_api_key\s*=\s*"([^"]+)"')
  $secretMatch = [regex]::Match($text, 'baidu_secret_key\s*=\s*"([^"]+)"')
  if (-not $apiMatch.Success -or -not $secretMatch.Success) {
    throw "Cannot read baidu_api_key / baidu_secret_key from $sourcePath"
  }

  return @{
    ApiKey = $apiMatch.Groups[1].Value
    SecretKey = $secretMatch.Groups[1].Value
  }
}

function Get-InputBytes {
  param([string]$Source)

  if ($Source -match '^https?://') {
    $client = New-Object Net.WebClient
    $client.Headers["User-Agent"] = "smart-door-eye-baidu-image-format-test"
    try {
      return @{
        Bytes = $client.DownloadData($Source)
        Display = $Source
      }
    } finally {
      $client.Dispose()
    }
  }

  $path = (Resolve-Path $Source).Path
  return @{
    Bytes = [IO.File]::ReadAllBytes($path)
    Display = $path
  }
}

function Test-ImageMagic {
  param([byte[]]$Bytes)

  if ($Bytes.Length -ge 4 -and $Bytes[0] -eq 0xFF -and $Bytes[1] -eq 0xD8) {
    $hasEoi = $Bytes[$Bytes.Length - 2] -eq 0xFF -and $Bytes[$Bytes.Length - 1] -eq 0xD9
    return @{
      Format = "JPEG"
      MarkerOk = $hasEoi
      Marker = if ($hasEoi) { "SOI+EOI" } else { "SOI_ONLY_NO_EOI" }
    }
  }

  $pngHeader = [byte[]](0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A)
  if ($Bytes.Length -ge 8) {
    $pngOk = $true
    for ($i = 0; $i -lt 8; $i++) {
      if ($Bytes[$i] -ne $pngHeader[$i]) {
        $pngOk = $false
        break
      }
    }
    if ($pngOk) {
      return @{ Format = "PNG"; MarkerOk = $true; Marker = "PNG_HEADER" }
    }
  }

  return @{ Format = "UNKNOWN"; MarkerOk = $false; Marker = "UNKNOWN_MAGIC" }
}

function Invoke-JsonPost {
  param(
    [string]$Uri,
    [string]$Json,
    [int]$TimeoutSec
  )

  # Use explicit UTF-8 bytes so this PC test matches the firmware's JSON body:
  # image is raw Base64 text, not a data:image/... prefix and not URL encoded.
  $bodyBytes = [Text.Encoding]::UTF8.GetBytes($Json)
  $request = [Net.HttpWebRequest]::Create($Uri)
  $request.Method = "POST"
  $request.ContentType = "application/json; charset=utf-8"
  $request.Accept = "application/json"
  $request.Timeout = $TimeoutSec * 1000
  $request.ReadWriteTimeout = $TimeoutSec * 1000
  $request.ContentLength = $bodyBytes.Length

  $stream = $request.GetRequestStream()
  try {
    $stream.Write($bodyBytes, 0, $bodyBytes.Length)
  } finally {
    $stream.Dispose()
  }

  try {
    $response = $request.GetResponse()
  } catch [Net.WebException] {
    if ($_.Exception.Response -ne $null) {
      $response = $_.Exception.Response
    } else {
      throw
    }
  }

  $reader = New-Object IO.StreamReader($response.GetResponseStream(), [Text.Encoding]::UTF8)
  try {
    return $reader.ReadToEnd()
  } finally {
    $reader.Dispose()
    $response.Dispose()
  }
}

function Get-BaiduAccessToken {
  param([string]$ApiKey, [string]$SecretKey)

  $uri = "https://aip.baidubce.com/oauth/2.0/token?grant_type=client_credentials&client_id=$ApiKey&client_secret=$SecretKey"
  $resp = Invoke-RestMethod -Method Get -Uri $uri -TimeoutSec 20
  if ([string]::IsNullOrWhiteSpace($resp.access_token)) {
    throw "Baidu token response did not include access_token"
  }
  return $resp.access_token
}

function Invoke-BaiduDetect {
  param([string]$Token, [string]$Base64, [int]$TimeoutSec)

  $body = [ordered]@{
    image = $Base64
    image_type = "BASE64"
    face_field = "gender"
    max_face_num = 1
  } | ConvertTo-Json -Compress

  $uri = "https://aip.baidubce.com/rest/2.0/face/v3/detect?access_token=$Token"
  return (Invoke-JsonPost -Uri $uri -Json $body -TimeoutSec $TimeoutSec) | ConvertFrom-Json
}

function Invoke-BaiduSearch {
  param([string]$Token, [string]$Base64, [string]$GroupId, [int]$TimeoutSec)

  $body = [ordered]@{
    image = $Base64
    image_type = "BASE64"
    group_id_list = $GroupId
    match_threshold = 80
    quality_control = "NONE"
    liveness_control = "NONE"
  } | ConvertTo-Json -Compress

  $uri = "https://aip.baidubce.com/rest/2.0/face/v3/search?access_token=$Token"
  return (Invoke-JsonPost -Uri $uri -Json $body -TimeoutSec $TimeoutSec) | ConvertFrom-Json
}

$repoRoot = Get-RepoRoot
if ([string]::IsNullOrWhiteSpace($ApiKey) -or [string]::IsNullOrWhiteSpace($SecretKey)) {
  $cred = Get-ProjectCredential -RepoRoot $repoRoot
  if ([string]::IsNullOrWhiteSpace($ApiKey)) { $ApiKey = $cred.ApiKey }
  if ([string]::IsNullOrWhiteSpace($SecretKey)) { $SecretKey = $cred.SecretKey }
}

if ($Image.Count -eq 0) {
  $Image = @(Get-ChildItem (Join-Path $repoRoot "test\face-test-images") -Filter "*.jpg" | Sort-Object Name | ForEach-Object { $_.FullName })
}

Write-Host "Baidu image format/API test"
Write-Host "repoRoot=$repoRoot"
Write-Host "mode=$Mode groupId=$GroupId images=$($Image.Count)"
Write-Host ""

$token = Get-BaiduAccessToken -ApiKey $ApiKey -SecretKey $SecretKey
Write-Host "TOKEN_OK chars=$($token.Length)"
Write-Host ""

$rows = New-Object System.Collections.Generic.List[object]
foreach ($source in $Image) {
  $input = Get-InputBytes -Source $source
  $bytes = [byte[]]$input.Bytes
  $magic = Test-ImageMagic -Bytes $bytes
  $base64 = [Convert]::ToBase64String($bytes)

  $detectCode = ""
  $detectMsg = ""
  $detectFaces = ""
  $searchCode = ""
  $searchMsg = ""
  $searchUsers = ""

  if ($Mode -eq "detect" -or $Mode -eq "both") {
    try {
      $detect = Invoke-BaiduDetect -Token $token -Base64 $base64 -TimeoutSec $TimeoutSec
      $detectCode = [string]$detect.error_code
      $detectMsg = [string]$detect.error_msg
      if ($detect.result -ne $null) { $detectFaces = [string]$detect.result.face_num }
    } catch {
      $detectCode = "HTTP_EXCEPTION"
      $detectMsg = $_.Exception.Message
    }
    Start-Sleep -Milliseconds $DelayMs
  }

  if ($Mode -eq "search" -or $Mode -eq "both") {
    try {
      $search = Invoke-BaiduSearch -Token $token -Base64 $base64 -GroupId $GroupId -TimeoutSec $TimeoutSec
      $searchCode = [string]$search.error_code
      $searchMsg = [string]$search.error_msg
      if ($search.result -ne $null -and $search.result.user_list -ne $null) {
        $searchUsers = [string]$search.result.user_list.Count
      }
    } catch {
      $searchCode = "HTTP_EXCEPTION"
      $searchMsg = $_.Exception.Message
    }
    Start-Sleep -Milliseconds $DelayMs
  }

  $verdict = "CHECK_RESULT"
  if (-not $magic.MarkerOk) {
    $verdict = "LOCAL_IMAGE_MAGIC_BAD"
  } elseif ($detectCode -eq "0" -or $searchCode -eq "0" -or $searchCode -eq "222207" -or $searchCode -eq "222202") {
    $verdict = "FORMAT_OK"
  } elseif ($detectCode -eq "222013" -or $searchCode -eq "222013") {
    $verdict = "BAIDU_FORMAT_ERROR"
  } elseif ($detectCode -eq "18" -or $searchCode -eq "18") {
    $verdict = "BAIDU_QPS_LIMIT_RETRY"
  }

  $rows.Add([pscustomobject]@{
    Source = [IO.Path]::GetFileName($input.Display)
    Bytes = $bytes.Length
    Format = $magic.Format
    Marker = $magic.Marker
    Base64Chars = $base64.Length
    DetectCode = $detectCode
    DetectFaces = $detectFaces
    SearchCode = $searchCode
    SearchUsers = $searchUsers
    Verdict = $verdict
    DetectMsg = $detectMsg
    SearchMsg = $searchMsg
  })
}

$rows | Format-Table -AutoSize

Write-Host ""
foreach ($row in $rows) {
  Write-Host (
    "RESULT source={0} bytes={1} format={2} marker={3} base64_chars={4} detect_code={5} detect_faces={6} search_code={7} search_users={8} verdict={9} detect_msg=""{10}"" search_msg=""{11}""" -f
    $row.Source,
    $row.Bytes,
    $row.Format,
    $row.Marker,
    $row.Base64Chars,
    $row.DetectCode,
    $row.DetectFaces,
    $row.SearchCode,
    $row.SearchUsers,
    $row.Verdict,
    $row.DetectMsg,
    $row.SearchMsg
  )
}
